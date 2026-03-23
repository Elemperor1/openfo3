#ifndef APPS_OPENFO3_CONTENTSTORE_H
#define APPS_OPENFO3_CONTENTSTORE_H

#include <components/esm/exteriorcelllocation.hpp>
#include <components/esm/formid.hpp>
#include <components/esm/refid.hpp>
#include <components/esm/util.hpp>
#include <components/esm4/loadacti.hpp>
#include <components/esm4/loadalch.hpp>
#include <components/esm4/loadammo.hpp>
#include <components/esm4/loadarmo.hpp>
#include <components/esm4/loadavif.hpp>
#include <components/esm4/loadbook.hpp>
#include <components/esm4/loadcell.hpp>
#include <components/esm4/loadclas.hpp>
#include <components/esm4/loadcont.hpp>
#include <components/esm4/loaddobj.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadflst.hpp>
#include <components/esm4/loadglob.hpp>
#include <components/esm4/loadgmst.hpp>
#include <components/esm4/loadkeym.hpp>
#include <components/esm4/loadland.hpp>
#include <components/esm4/loadligh.hpp>
#include <components/esm4/loadlvli.hpp>
#include <components/esm4/loadltex.hpp>
#include <components/esm4/loadmisc.hpp>
#include <components/esm4/loadmstt.hpp>
#include <components/esm4/loadnote.hpp>
#include <components/esm4/loadnpc.hpp>
#include <components/esm4/loadperk.hpp>
#include <components/esm4/loadrace.hpp>
#include <components/esm4/loadrefr.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/loadterm.hpp>
#include <components/esm4/loadtxst.hpp>
#include <components/esm4/loadweap.hpp>
#include <components/esm4/loadwrld.hpp>
#include <components/files/collections.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace OpenFO3
{
    enum class ObjectKind
    {
        Static,
        Door,
        Container,
        Activator,
        Terminal,
        MovableStatic,
        Book,
        Note,
        Aid,
        Ammo,
        Armor,
        Key,
        Light,
        Weapon,
        Misc,
    };

    std::string_view toString(ObjectKind kind);
    bool isInteractiveKind(ObjectKind kind);
    bool isReadableKind(ObjectKind kind);
    bool isBlockingInteractiveKind(ObjectKind kind);
    bool isWalkableSupportKind(ObjectKind kind);
    bool isLockableKind(ObjectKind kind);

    struct BaseRecordData
    {
        ObjectKind mKind = ObjectKind::Static;
        std::string mEditorId;
        std::string mFullName;
        std::string mModel;
        std::string mText;
        std::string mResultText;
        std::string mIcon;
        std::string mCategory;
        int mValue = 0;
        float mWeight = 0.f;
        int mItemCount = 0;
        bool mReadable = false;
        bool mNoTake = false;
        std::vector<ESM4::InventoryItem> mContainerItems;
    };

    struct WorldBounds
    {
        bool mValid = false;
        int mMinX = 0;
        int mMaxX = 0;
        int mMinY = 0;
        int mMaxY = 0;

        void include(float worldX, float worldY)
        {
            const ESM::ExteriorCellLocation cell = ESM::positionToExteriorCellLocation(worldX, worldY);
            if (!mValid)
            {
                mMinX = mMaxX = cell.mX;
                mMinY = mMaxY = cell.mY;
                mValid = true;
                return;
            }

            mMinX = std::min(mMinX, cell.mX);
            mMaxX = std::max(mMaxX, cell.mX);
            mMinY = std::min(mMinY, cell.mY);
            mMaxY = std::max(mMaxY, cell.mY);
        }
    };

    using ExteriorBucketKey = std::tuple<ESM::RefId, int, int>;

    ExteriorBucketKey makeExteriorBucketKey(ESM::RefId worldspace, int x, int y);
    ESM::RefId bucketWorldspace(const ExteriorBucketKey& key);
    int bucketX(const ExteriorBucketKey& key);
    int bucketY(const ExteriorBucketKey& key);
    std::string bucketLabel(const ExteriorBucketKey& key);

    struct BootstrapCell
    {
        ESM::RefId mId;
        const ESM4::Cell* mCell = nullptr;
    };

    class LoadedContent
    {
    public:
        void load(const Files::Collections& fileCollections, const std::vector<std::string>& contentFiles,
            const VFS::Manager* vfs, const ToUTF8::StatelessUtf8Encoder* encoder);

        std::optional<BootstrapCell> chooseBootstrapCell() const;

        const BaseRecordData* findBase(ESM::FormId id) const;
        const ESM4::LevelledItem* findLevelledItem(ESM::FormId id) const;
        const ESM4::FormIdList* findFormList(ESM::FormId id) const;
        const ESM4::Land* findLand(ESM::RefId worldspace, int x, int y) const;
        bool hasLand(ESM::RefId worldspace, int x, int y) const;
        const std::map<ESM::FormId, ESM4::Reference>* getReferences(ESM::RefId cellId) const;
        const ESM4::Reference* findReference(ESM::FormId id) const;
        const ESM4::LandTexture* findLandTexture(ESM::FormId id) const;
        const ESM4::TextureSet* findTextureSet(ESM::FormId id) const;
        const ESM4::Cell* findCell(ESM::RefId id) const;
        std::optional<ESM::RefId> findExteriorCellId(ESM::RefId worldspace, int x, int y) const;
        const std::vector<const ESM4::Reference*>* getExteriorSpatialReferences(const ExteriorBucketKey& key) const;
        const WorldBounds* getWorldBounds(ESM::RefId worldspace) const;

        const ESM4::GameSetting* findGameSetting(std::string_view editorId) const;
        std::optional<int> getGameSettingInt(std::string_view editorId) const;
        std::optional<float> getGameSettingFloat(std::string_view editorId) const;
        std::optional<bool> getGameSettingBool(std::string_view editorId) const;

        const ESM4::GlobalVariable* findGlobal(std::string_view editorId) const;
        std::optional<float> getGlobalFloat(std::string_view editorId) const;

        const ESM4::DefaultObj* getDefaultObjects() const;
        const ESM4::Race* findRace(ESM::FormId id) const;
        const ESM4::Class* findClass(ESM::FormId id) const;
        const ESM4::Npc* findNpc(ESM::FormId id) const;
        const ESM4::Npc* findPrototypePlayerNpc() const;
        const ESM4::ActorValueInfo* findActorValue(ESM::FormId id) const;
        const ESM4::ActorValueInfo* findActorValue(std::string_view editorId) const;
        const ESM4::Perk* findPerk(ESM::FormId id) const;
        const ESM4::Perk* findPerk(std::string_view editorId) const;
        std::vector<const ESM4::Perk*> getPlayablePerks() const;

    private:
        bool handleRecord(ESM4::Reader& reader);
        void rebuildDerivedIndices();

        std::map<ESM::FormId, BaseRecordData> mBaseRecords;
        std::map<ESM::FormId, ESM4::LevelledItem> mLevelledItems;
        std::map<ESM::FormId, ESM4::FormIdList> mFormLists;
        std::map<ESM::RefId, ESM4::Cell> mCells;
        std::map<ESM::FormId, ESM4::World> mWorlds;
        std::map<ESM::FormId, ESM4::Land> mLands;
        std::map<ESM::FormId, ESM4::LandTexture> mLandTextures;
        std::map<ESM::FormId, ESM4::TextureSet> mTextureSets;
        std::map<ESM::RefId, std::map<ESM::FormId, ESM4::Reference>> mReferencesByCell;

        std::map<ESM::FormId, ESM4::GameSetting> mGameSettings;
        std::map<ESM::FormId, ESM4::GlobalVariable> mGlobals;
        std::map<ESM::FormId, ESM4::DefaultObj> mDefaultObjects;
        std::map<ESM::FormId, ESM4::Race> mRaces;
        std::map<ESM::FormId, ESM4::Class> mClasses;
        std::map<ESM::FormId, ESM4::Npc> mNpcs;
        std::map<ESM::FormId, ESM4::ActorValueInfo> mActorValues;
        std::map<ESM::FormId, ESM4::Perk> mPerks;

        std::map<std::tuple<ESM::RefId, int, int>, ESM::FormId> mLandByCell;
        std::map<ESM::FormId, const ESM4::Reference*> mReferenceById;
        std::map<ESM::RefId, WorldBounds> mWorldBounds;
        std::map<std::tuple<ESM::RefId, int, int>, ESM::RefId> mExteriorCellByCoord;
        std::map<ExteriorBucketKey, std::vector<const ESM4::Reference*>> mExteriorRefsBySpatialCell;

        std::map<std::string, ESM::FormId> mGameSettingsByEditorId;
        std::map<std::string, ESM::FormId> mGlobalsByEditorId;
        std::map<std::string, ESM::FormId> mActorValuesByEditorId;
        std::map<std::string, ESM::FormId> mPerksByEditorId;
        std::optional<ESM::FormId> mDefaultObjectId;
        std::optional<ESM::FormId> mPrototypePlayerNpcId;
    };
}

#endif
