#include "loadperk.hpp"

#include <limits>
#include <stdexcept>

#include "reader.hpp"

namespace ESM4
{
    namespace
    {
        void adjustRawFormId(std::uint32_t& value, Reader& reader)
        {
            ESM::FormId32 formId = value;
            reader.adjustFormId(formId);
            value = formId;
        }

        void adjustConditionFormIds(TargetCondition& condition, Reader& reader)
        {
            switch (condition.functionIndex)
            {
                case FUN_GetItemCount:
                case FUN_GetInFaction:
                case FUN_GetIsID:
                case FUN_GetIsRace:
                case FUN_GetPCInFaction:
                case FUN_GetPCIsRace:
                case FUN_GetEquipped:
                case FUN_HasPerk:
                case FUN_GetQuestCompleted:
                    adjustRawFormId(condition.param1, reader);
                    break;
                case FUN_GetIsClass:
                case FUN_GetPCIsClass:
                    adjustRawFormId(condition.param1, reader);
                    break;
                default:
                    break;
            }
        }

        TargetCondition loadCondition(Reader& reader, const ESM4::SubRecordHeader& subHdr, std::uint8_t runOnOverride)
        {
            TargetCondition condition{};
            if (subHdr.dataSize == 20)
            {
                reader.get(&condition, 20);
                condition.runOn = runOnOverride;
            }
            else if (subHdr.dataSize == 24)
            {
                reader.get(&condition, 24);
            }
            else if (subHdr.dataSize == sizeof(TargetCondition))
            {
                reader.get(condition);
                if (condition.reference)
                    reader.adjustFormId(condition.reference);
            }
            else
            {
                throw std::runtime_error("Unsupported PERK CTDA size " + std::to_string(subHdr.dataSize));
            }

            adjustConditionFormIds(condition, reader);
            return condition;
        }

        PerkEntryPointData loadEntryPointData(Reader& reader, std::uint8_t functionType, std::uint8_t entryPointFunction,
            const ESM4::SubRecordHeader& subHdr)
        {
            if (functionType == 4)
            {
                reader.skipSubRecordData();
                return std::monostate{};
            }

            if (functionType == 1)
            {
                float value = 0.f;
                reader.get(value);
                return value;
            }

            if (functionType == 2 && entryPointFunction != 5)
            {
                std::array<float, 2> values{};
                reader.get(values[0]);
                reader.get(values[1]);
                return values;
            }

            if (functionType == 3)
            {
                ESM::FormId formId;
                reader.getFormId(formId);
                return formId;
            }

            if (functionType == 5 || (functionType == 2 && entryPointFunction == 5))
            {
                PerkActorValueModifier modifier;
                reader.get(modifier.actorValue);
                reader.get(modifier.value);
                return modifier;
            }

            std::vector<std::uint8_t> raw(subHdr.dataSize);
            if (!raw.empty())
                reader.get(raw.data(), raw.size());
            return raw;
        }

        void skipRemainingSubRecordData(Reader& reader, const ESM4::SubRecordHeader& subHdr, std::size_t consumed)
        {
            if (consumed < subHdr.dataSize)
                reader.skipSubRecordData(static_cast<std::uint32_t>(subHdr.dataSize - consumed));
        }
    }

    void Perk::load(Reader& reader)
    {
        mId = reader.getFormIdFromHeader();
        mFlags = reader.hdr().record.flags;

        std::size_t currentEffect = std::numeric_limits<std::size_t>::max();
        std::size_t currentConditionBlock = std::numeric_limits<std::size_t>::max();

        while (reader.getSubRecordHeader())
        {
            const ESM4::SubRecordHeader& subHdr = reader.subRecordHeader();
            switch (subHdr.typeId)
            {
                case ESM::fourCC("EDID"):
                    reader.getZString(mEditorId);
                    break;
                case ESM::fourCC("FULL"):
                    reader.getLocalizedString(mFullName);
                    break;
                case ESM::fourCC("DESC"):
                    reader.getLocalizedString(mDescription);
                    break;
                case ESM::fourCC("ICON"):
                    reader.getZString(mIcon);
                    break;
                case ESM::fourCC("MICO"):
                    reader.getZString(mSmallIcon);
                    break;
                case ESM::fourCC("CTDA"):
                {
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        mConditions.push_back(loadCondition(reader, subHdr, 0));
                    }
                    else
                    {
                        if (currentConditionBlock == std::numeric_limits<std::size_t>::max())
                        {
                            mEffects[currentEffect].mConditionBlocks.emplace_back();
                            currentConditionBlock = mEffects[currentEffect].mConditionBlocks.size() - 1;
                        }
                        PerkEffectConditionBlock& block = mEffects[currentEffect].mConditionBlocks[currentConditionBlock];
                        block.conditions.push_back(loadCondition(reader, subHdr, block.runOn));
                    }
                    break;
                }
                case ESM::fourCC("DATA"):
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        if (subHdr.dataSize >= sizeof(PerkData))
                        {
                            reader.get(&mData, sizeof(PerkData));
                            skipRemainingSubRecordData(reader, subHdr, sizeof(PerkData));
                        }
                        else
                            reader.skipSubRecordData();
                    }
                    else
                    {
                        PerkEffect& effect = mEffects[currentEffect];
                        switch (effect.mHeader.type)
                        {
                            case 0:
                            {
                                PerkEffectQuestStage stage{};
                                if (subHdr.dataSize >= sizeof(PerkEffectQuestStage))
                                {
                                    reader.get(stage);
                                    skipRemainingSubRecordData(reader, subHdr, sizeof(PerkEffectQuestStage));
                                }
                                else
                                {
                                    reader.skipSubRecordData();
                                    break;
                                }
                                reader.adjustFormId(stage.quest);
                                effect.mData = stage;
                                break;
                            }
                            case 1:
                            {
                                ESM::FormId ability;
                                reader.getFormId(ability);
                                skipRemainingSubRecordData(reader, subHdr, sizeof(ESM::FormId32));
                                effect.mData = ability;
                                break;
                            }
                            case 2:
                            {
                                if (subHdr.dataSize >= sizeof(PerkEffectEntryPointData))
                                {
                                    reader.get(&effect.mEntryPointDataHeader, sizeof(PerkEffectEntryPointData));
                                    effect.mHasEntryPointData = true;
                                    skipRemainingSubRecordData(reader, subHdr, sizeof(PerkEffectEntryPointData));
                                }
                                else
                                    reader.skipSubRecordData();
                                break;
                            }
                            default:
                                reader.skipSubRecordData();
                                break;
                        }
                    }
                    break;
                case ESM::fourCC("PRKE"):
                {
                    if (subHdr.dataSize < sizeof(PerkEffectHeader))
                    {
                        reader.skipSubRecordData();
                        currentEffect = std::numeric_limits<std::size_t>::max();
                        currentConditionBlock = std::numeric_limits<std::size_t>::max();
                        break;
                    }

                    PerkEffect effect;
                    reader.get(&effect.mHeader, sizeof(PerkEffectHeader));
                    skipRemainingSubRecordData(reader, subHdr, sizeof(PerkEffectHeader));
                    mEffects.push_back(effect);
                    currentEffect = mEffects.size() - 1;
                    currentConditionBlock = std::numeric_limits<std::size_t>::max();
                    break;
                }
                case ESM::fourCC("PRKC"):
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        reader.skipSubRecordData();
                        break;
                    }
                    mEffects[currentEffect].mConditionBlocks.emplace_back();
                    currentConditionBlock = mEffects[currentEffect].mConditionBlocks.size() - 1;
                    if (subHdr.dataSize >= sizeof(std::uint8_t))
                    {
                        reader.get(mEffects[currentEffect].mConditionBlocks[currentConditionBlock].runOn);
                        skipRemainingSubRecordData(reader, subHdr, sizeof(std::uint8_t));
                    }
                    else
                        reader.skipSubRecordData();
                    break;
                case ESM::fourCC("EPFT"):
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        reader.skipSubRecordData();
                        break;
                    }
                    if (subHdr.dataSize >= sizeof(std::uint8_t))
                    {
                        reader.get(mEffects[currentEffect].mEntryPointFunctionType);
                        skipRemainingSubRecordData(reader, subHdr, sizeof(std::uint8_t));
                    }
                    else
                        reader.skipSubRecordData();
                    break;
                case ESM::fourCC("EPFD"):
                {
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        reader.skipSubRecordData();
                        break;
                    }
                    PerkEffect& effect = mEffects[currentEffect];
                    if (effect.mHasEntryPointData)
                    {
                        effect.mEntryPointData = loadEntryPointData(
                            reader, effect.mEntryPointFunctionType, effect.mEntryPointDataHeader.function, subHdr);
                    }
                    else
                    {
                        reader.skipSubRecordData();
                    }
                    break;
                }
                case ESM::fourCC("EPF2"):
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        reader.skipSubRecordData();
                        break;
                    }
                    reader.getZString(mEffects[currentEffect].mButtonLabel);
                    break;
                case ESM::fourCC("EPF3"):
                    if (currentEffect == std::numeric_limits<std::size_t>::max())
                    {
                        reader.skipSubRecordData();
                        break;
                    }
                    if (subHdr.dataSize >= sizeof(std::uint16_t))
                    {
                        reader.get(mEffects[currentEffect].mScriptFlags);
                        skipRemainingSubRecordData(reader, subHdr, sizeof(std::uint16_t));
                    }
                    else
                        reader.skipSubRecordData();
                    break;
                case ESM::fourCC("PRKF"):
                    reader.skipSubRecordData();
                    currentEffect = std::numeric_limits<std::size_t>::max();
                    currentConditionBlock = std::numeric_limits<std::size_t>::max();
                    break;
                case ESM::fourCC("SCHR"):
                case ESM::fourCC("SCDA"):
                case ESM::fourCC("SCTX"):
                case ESM::fourCC("SCRO"):
                case ESM::fourCC("SCRV"):
                case ESM::fourCC("SCVR"):
                case ESM::fourCC("SLSD"):
                    reader.skipSubRecordData();
                    break;
                default:
                    reader.skipSubRecordData();
                    break;
            }
        }
    }
}
