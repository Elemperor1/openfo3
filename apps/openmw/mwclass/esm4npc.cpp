#include "esm4npc.hpp"

#include <components/esm4/loadarmo.hpp>
#include <components/esm4/loadclot.hpp>
#include <components/esm4/loadlvli.hpp>
#include <components/esm4/loadlvln.hpp>
#include <components/esm4/loadnpc.hpp>
#include <components/esm4/loadotft.hpp>
#include <components/esm4/loadrace.hpp>

#include <components/misc/resourcehelpers.hpp>

#include "../mwworld/customdata.hpp"
#include "../mwworld/esmstore.hpp"

#include "esm4base.hpp"

namespace MWClass
{
    namespace
    {
        uint16_t getTemplateFlags(const ESM4::Npc& rec)
        {
            if (rec.mIsTES4)
                return rec.mBaseConfig.tes4.templateFlags;
            if (rec.mIsFONV || ESM4Npc::isFo3Like(rec))
                return rec.mBaseConfig.fo3.templateFlags;
            if (rec.mIsFO4)
                return rec.mBaseConfig.fo4.templateFlags;
            return rec.mBaseConfig.tes5.templateFlags;
        }

        bool getFemaleFlag(const ESM4::Npc& rec)
        {
            if (rec.mIsTES4)
                return rec.mBaseConfig.tes4.flags & ESM4::Npc::TES4_Female;
            if (rec.mIsFONV || ESM4Npc::isFo3Like(rec))
                return rec.mBaseConfig.fo3.flags & ESM4::Npc::FO3_Female;
            if (rec.mIsFO4)
                return rec.mBaseConfig.fo4.flags & ESM4::Npc::TES5_Female;
            return rec.mBaseConfig.tes5.flags & ESM4::Npc::TES5_Female;
        }
    }

    template <class LevelledRecord, class TargetRecord>
    static std::vector<const TargetRecord*> withBaseTemplates(
        const TargetRecord* rec, int level = MWClass::ESM4Impl::sDefaultLevel)
    {
        std::vector<const TargetRecord*> res{ rec };
        while (true)
        {
            const TargetRecord* newRec
                = MWClass::ESM4Impl::resolveLevelled<ESM4::LevelledNpc, ESM4::Npc>(rec->mBaseTemplate, level);
            if (!newRec || newRec == rec)
                return res;
            res.push_back(rec = newRec);
        }
    }

    static const ESM4::Npc* chooseTemplate(const std::vector<const ESM4::Npc*>& recs, uint16_t flag)
    {
        for (const auto* rec : recs)
        {
            if (!(getTemplateFlags(*rec) & flag))
                return rec;
        }
        return nullptr;
    }

    class ESM4NpcCustomData : public MWWorld::TypedCustomData<ESM4NpcCustomData>
    {
    public:
        const ESM4::Npc* mTraits = nullptr;
        const ESM4::Npc* mBaseData = nullptr;
        const ESM4::Race* mRace = nullptr;
        bool mIsFemale = false;

        // TODO: Use InventoryStore instead (currently doesn't support ESM4 objects)
        std::vector<const ESM4::Armor*> mEquippedArmor;
        std::vector<const ESM4::Clothing*> mEquippedClothing;

        ESM4NpcCustomData& asESM4NpcCustomData() override { return *this; }
        const ESM4NpcCustomData& asESM4NpcCustomData() const override { return *this; }
    };

    ESM4NpcCustomData& ESM4Npc::getCustomData(const MWWorld::ConstPtr& ptr)
    {
        // Note: the argument is ConstPtr because this function is used in `getModel` and `getName`
        // which are virtual and work with ConstPtr. `getModel` and `getName` use custom data
        // because they require a lot of work including levelled records resolving and it would be
        // stupid to not to cache the results. Maybe we should stop using ConstPtr at all
        // to avoid such workarounds.
        MWWorld::RefData& refData = const_cast<MWWorld::RefData&>(ptr.getRefData());

        if (auto* data = refData.getCustomData())
            return data->asESM4NpcCustomData();

        auto data = std::make_unique<ESM4NpcCustomData>();

        const MWWorld::ESMStore* store = MWBase::Environment::get().getESMStore();
        const MWWorld::LiveCellRef<ESM4::Npc>* ref = ptr.get<ESM4::Npc>();
        const ESM4::Npc* const base = ref ? ref->mBase : nullptr;
        if (base == nullptr)
        {
            Log(Debug::Warning) << "ESM4 NPC base record is missing";
            ESM4NpcCustomData& res = *data;
            refData.setCustomData(std::move(data));
            return res;
        }

        auto npcRecs = withBaseTemplates<ESM4::LevelledNpc, ESM4::Npc>(base);

        data->mTraits = chooseTemplate(npcRecs, ESM4::Npc::Template_UseTraits);

        if (data->mTraits == nullptr)
        {
            Log(Debug::Warning) << "Traits are not found for ESM4 NPC base record: \"" << base->mEditorId << "\" ("
                                << ESM::RefId(base->mId) << "), falling back to the base record";
            data->mTraits = base;
        }

        data->mBaseData = chooseTemplate(npcRecs, ESM4::Npc::Template_UseBaseData);

        if (data->mBaseData == nullptr)
        {
            Log(Debug::Warning) << "Base data is not found for ESM4 NPC base record: \"" << base->mEditorId << "\" ("
                                << ESM::RefId(base->mId) << "), falling back to the base record";
            data->mBaseData = base;
        }

        if (data->mTraits != nullptr)
        {
            data->mRace = store->get<ESM4::Race>().find(data->mTraits->mRace);
            if (data->mRace == nullptr && data->mBaseData != nullptr)
                data->mRace = store->get<ESM4::Race>().find(data->mBaseData->mRace);

            if (data->mRace == nullptr)
            {
                Log(Debug::Warning) << "Race is not found for ESM4 NPC base record: \"" << base->mEditorId << "\" ("
                                    << ESM::RefId(base->mId) << ")";
            }

            data->mIsFemale = getFemaleFlag(*data->mTraits);
        }

        if (auto inv = chooseTemplate(npcRecs, ESM4::Npc::Template_UseInventory))
        {
            for (const ESM4::InventoryItem& item : inv->mInventory)
            {
                if (auto* armor
                    = ESM4Impl::resolveLevelled<ESM4::LevelledItem, ESM4::Armor>(ESM::FormId::fromUint32(item.item)))
                    data->mEquippedArmor.push_back(armor);
                else if (data->mTraits != nullptr && data->mTraits->mIsTES4)
                {
                    const auto* clothing = ESM4Impl::resolveLevelled<ESM4::LevelledItem, ESM4::Clothing>(
                        ESM::FormId::fromUint32(item.item));
                    if (clothing)
                        data->mEquippedClothing.push_back(clothing);
                }
            }
            if (!inv->mDefaultOutfit.isZeroOrUnset())
            {
                if (const ESM4::Outfit* outfit = store->get<ESM4::Outfit>().search(inv->mDefaultOutfit))
                {
                    for (ESM::FormId itemId : outfit->mInventory)
                        if (auto* armor = ESM4Impl::resolveLevelled<ESM4::LevelledItem, ESM4::Armor>(itemId))
                            data->mEquippedArmor.push_back(armor);
                }
                else
                    Log(Debug::Error) << "Outfit not found: " << ESM::RefId(inv->mDefaultOutfit);
            }
        }

        ESM4NpcCustomData& res = *data;
        refData.setCustomData(std::move(data));
        return res;
    }

    const std::vector<const ESM4::Armor*>& ESM4Npc::getEquippedArmor(const MWWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mEquippedArmor;
    }

    const std::vector<const ESM4::Clothing*>& ESM4Npc::getEquippedClothing(const MWWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mEquippedClothing;
    }

    const ESM4::Npc* ESM4Npc::getTraitsRecord(const MWWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mTraits;
    }

    const ESM4::Race* ESM4Npc::getRace(const MWWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mRace;
    }

    bool ESM4Npc::isFemale(const MWWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mIsFemale;
    }

    std::string_view ESM4Npc::getModel(const MWWorld::ConstPtr& ptr) const
    {
        const ESM4NpcCustomData& data = getCustomData(ptr);
        if (data.mTraits == nullptr)
            return {};
        if (data.mTraits->mIsTES4)
            return data.mTraits->mModel;
        if (data.mTraits->mIsFONV || isFo3Like(*data.mTraits))
        {
            if (!data.mTraits->mModel.empty())
                return data.mTraits->mModel;
        }
        if (data.mRace != nullptr)
            return data.mIsFemale ? data.mRace->mModelFemale : data.mRace->mModelMale;
        if (!data.mTraits->mModel.empty())
            return data.mTraits->mModel;
        return {};
    }

    std::string_view ESM4Npc::getName(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM4::Npc>* ref = ptr.get<ESM4::Npc>();
        const ESM4::Npc* const baseData = getCustomData(ptr).mBaseData;
        if (baseData == nullptr)
        {
            if (ref != nullptr && ref->mBase != nullptr)
            {
                if (!ref->mBase->mFullName.empty())
                    return ref->mBase->mFullName;
                return ref->mBase->mEditorId;
            }
            return {};
        }
        return baseData->mFullName;
    }

    bool ESM4Npc::isFo3Like(const ESM4::Npc& npc)
    {
        return npc.mIsFO3;
    }
}
