#ifndef ESM4_PERK_H
#define ESM4_PERK_H

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "script.hpp"
#include <components/esm/defs.hpp>
#include <components/esm/formid.hpp>

namespace ESM4
{
    class Reader;

#pragma pack(push, 1)
    struct PerkData
    {
        std::uint8_t trait = 0;
        std::uint8_t minLevel = 0;
        std::uint8_t ranks = 0;
        std::uint8_t playable = 0;
        std::uint8_t hidden = 0;
    };

    struct PerkEffectHeader
    {
        std::uint8_t type = 0;
        std::uint8_t rank = 0;
        std::uint8_t priority = 0;
    };

    struct PerkEffectQuestStage
    {
        ESM::FormId32 quest;
        std::int8_t stage = 0;
        std::array<std::uint8_t, 3> unused{};
    };

    struct PerkEffectEntryPointData
    {
        std::uint8_t entryPoint = 0;
        std::uint8_t function = 0;
        std::uint8_t conditionTabCount = 0;
    };

    struct PerkActorValueModifier
    {
        std::uint32_t actorValue = 0;
        float value = 0.f;
    };
#pragma pack(pop)

    struct PerkEffectConditionBlock
    {
        std::uint8_t runOn = 0;
        std::vector<TargetCondition> conditions;
    };

    using PerkEntryPointData = std::variant<std::monostate, std::vector<std::uint8_t>, float, std::array<float, 2>,
        ESM::FormId, PerkActorValueModifier>;

    struct PerkEffect
    {
        PerkEffectHeader mHeader;
        std::variant<std::monostate, PerkEffectQuestStage, ESM::FormId> mData;
        bool mHasEntryPointData = false;
        PerkEffectEntryPointData mEntryPointDataHeader;
        PerkEntryPointData mEntryPointData;
        std::vector<PerkEffectConditionBlock> mConditionBlocks;
        std::uint8_t mEntryPointFunctionType = 0;
        std::string mButtonLabel;
        std::uint16_t mScriptFlags = 0;
    };

    struct Perk
    {
        ESM::FormId mId;
        std::uint32_t mFlags = 0;

        std::string mEditorId;
        std::string mFullName;
        std::string mDescription;
        std::string mIcon;
        std::string mSmallIcon;

        std::vector<TargetCondition> mConditions;
        PerkData mData;
        std::vector<PerkEffect> mEffects;

        void load(Reader& reader);
        static constexpr ESM::RecNameInts sRecordId = ESM::RecNameInts::REC_PERK4;
    };
}

#endif
