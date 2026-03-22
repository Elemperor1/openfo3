#include "engine.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/exteriorcelllocation.hpp>
#include <components/esm/refid.hpp>
#include <components/esm/util.hpp>
#include <components/esm4/loadacti.hpp>
#include <components/esm4/loadalch.hpp>
#include <components/esm4/loadammo.hpp>
#include <components/esm4/loadarmo.hpp>
#include <components/esm4/loadbook.hpp>
#include <components/esm4/loadcell.hpp>
#include <components/esm4/loadcont.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadflst.hpp>
#include <components/esm4/loadkeym.hpp>
#include <components/esm4/loadland.hpp>
#include <components/esm4/loadligh.hpp>
#include <components/esm4/loadlvli.hpp>
#include <components/esm4/loadltex.hpp>
#include <components/esm4/loadmisc.hpp>
#include <components/esm4/loadmstt.hpp>
#include <components/esm4/loadnote.hpp>
#include <components/esm4/loadrefr.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/loadterm.hpp>
#include <components/esm4/loadtxst.hpp>
#include <components/esm4/loadweap.hpp>
#include <components/esm4/loadwrld.hpp>
#include <components/esm4/readerutils.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/constrainedfilestream.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/osguservalues.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifosg/nifloader.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/cullsafeboundsvisitor.hpp>
#include <components/sceneutil/glextensions.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/sceneutil/stateupdater.hpp>
#include <components/sceneutil/util.hpp>
#include <components/shader/removedalphafunc.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/registerarchives.hpp>
#include <components/esmterrain/storage.hpp>
#include <components/terrain/quadtreeworld.hpp>
#include <components/terrain/terraingrid.hpp>

#include <osg/Camera>
#include <osg/BlendFunc>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Geode>
#include <osg/LightSource>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/LineWidth>
#include <osg/Material>
#include <osg/Notify>
#include <osg/Drawable>
#include <osg/ShapeDrawable>
#include <osg/StateSet>
#include <osgText/Text>
#include <osgUtil/RenderBin>
#include <osgViewer/Viewer>
#include <osgParticle/ParticleSystem>

#include <SDL.h>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr float sNearClip = 5.f;
    constexpr float sFarClip = 500000.f;
    constexpr float sMouseSensitivity = 0.0025f;
    constexpr float sWalkSpeed = 900.f;
    constexpr float sSprintMultiplier = 3.f;
    constexpr float sLookDistance = 512.f;
    constexpr float sPlayerRadius = 32.f;
    constexpr float sEyeOffset = 72.f;
    constexpr float sFocusProxyRadiusMin = 48.f;
    constexpr float sFarVisualMinBoundRadius = 64.f;
    constexpr float sLandmarkVisualMinBoundRadius = 256.f;
    constexpr float sTaggedLandmarkMinBoundRadius = 96.f;
    constexpr float sWalkableNormalMinZ = 0.55f;
    constexpr float sGroundProbeDistance = 512.f;
    constexpr float sMaxStepUp = 48.f;
    constexpr float sMaxStepDown = 128.f;
    constexpr float sGroundProbeFootOffsetScale = 0.6f;
    constexpr float sSupportContinuityTolerance = 12.f;
    constexpr float sMinimumWalkableSupportRadius = 24.f;
    constexpr float sPostTravelFocusSuppressSeconds = 0.25f;
    constexpr std::size_t sInvalidIndex = std::numeric_limits<std::size_t>::max();
    constexpr int sStableTerrainHalfGridCap = 6;
    constexpr int sStableNearFullHalfGridCap = 3;
    constexpr int sStableLandmarkHalfGridCap = 10;
    constexpr float sBootstrapHoverHeight = 24.f;
    constexpr float sBootstrapTargetLift = 36.f;
    constexpr float sCellLoadingThreshold = 1024.f;
    constexpr int sPanelListVisibleItems = 14;
    constexpr int sPanelTextVisibleLines = 18;
    constexpr std::uint32_t sTerrainNodeMask = 0x1;
    constexpr std::uint32_t sObjectNodeMask = 0x2;
    constexpr std::uint32_t sHiddenNodeMask = 0x4;

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

    std::string_view toString(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Static:
                return "static";
            case ObjectKind::Door:
                return "door";
            case ObjectKind::Container:
                return "container";
            case ObjectKind::Activator:
                return "activator";
            case ObjectKind::Terminal:
                return "terminal";
            case ObjectKind::MovableStatic:
                return "movable static";
            case ObjectKind::Book:
                return "book";
            case ObjectKind::Note:
                return "note";
            case ObjectKind::Aid:
                return "aid";
            case ObjectKind::Ammo:
                return "ammo";
            case ObjectKind::Armor:
                return "armor";
            case ObjectKind::Key:
                return "key";
            case ObjectKind::Light:
                return "light";
            case ObjectKind::Weapon:
                return "weapon";
            case ObjectKind::Misc:
                return "misc";
        }
        return "object";
    }

    bool isInteractiveKind(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Door:
            case ObjectKind::Container:
            case ObjectKind::Activator:
            case ObjectKind::Terminal:
            case ObjectKind::Book:
            case ObjectKind::Note:
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Weapon:
            case ObjectKind::Misc:
                return true;
            case ObjectKind::Static:
            case ObjectKind::MovableStatic:
                return false;
        }
        return false;
    }

    bool isReadableKind(ObjectKind kind)
    {
        return kind == ObjectKind::Book || kind == ObjectKind::Note;
    }

    bool isBlockingInteractiveKind(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Door:
            case ObjectKind::Container:
            case ObjectKind::Activator:
            case ObjectKind::Terminal:
                return true;
            case ObjectKind::Book:
            case ObjectKind::Note:
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Weapon:
            case ObjectKind::Misc:
            case ObjectKind::Static:
            case ObjectKind::MovableStatic:
                return false;
        }

        return false;
    }

    bool isWalkableSupportKind(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Static:
            case ObjectKind::MovableStatic:
            case ObjectKind::Activator:
                return true;
            case ObjectKind::Door:
            case ObjectKind::Container:
            case ObjectKind::Terminal:
            case ObjectKind::Book:
            case ObjectKind::Note:
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Weapon:
            case ObjectKind::Misc:
                return false;
        }

        return false;
    }

    int bootstrapScoreForKind(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Door:
                return 40;
            case ObjectKind::Container:
                return 32;
            case ObjectKind::Terminal:
                return 28;
            case ObjectKind::Book:
            case ObjectKind::Note:
                return 18;
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Weapon:
            case ObjectKind::Misc:
                return 10;
            case ObjectKind::Activator:
                return 8;
            case ObjectKind::Static:
            case ObjectKind::MovableStatic:
                return 0;
        }
        return 0;
    }

    int settlementBootstrapBonus(const ESM4::Cell& cell)
    {
        std::string label = cell.mFullName;
        if (!cell.mEditorId.empty())
        {
            if (!label.empty())
                label.push_back(' ');
            label += cell.mEditorId;
        }

        const std::string lower = Misc::StringUtils::lowerCase(label);
        if (lower.empty())
            return 0;

        static constexpr std::array<std::pair<std::string_view, int>, 11> sPreferredSettlements = { {
            { "megaton", 50000 },
            { "big town", 46000 },
            { "canterbury commons", 44000 },
            { "rivet city", 42000 },
            { "tenpenny tower", 40000 },
            { "underworld", 38000 },
            { "arefu", 36000 },
            { "paradise falls", 34000 },
            { "temple of union", 32000 },
            { "republic of dave", 30000 },
            { "oasis", 28000 },
        } };

        for (const auto& [keyword, bonus] : sPreferredSettlements)
        {
            if (lower.find(keyword) != std::string::npos)
                return bonus;
        }

        static constexpr std::array<std::string_view, 6> sGenericSettlementKeywords = {
            "town", "village", "commons", "market", "settlement", "plaza",
        };
        for (std::string_view keyword : sGenericSettlementKeywords)
        {
            if (lower.find(keyword) != std::string::npos)
                return 18000;
        }

        return 0;
    }

    int initialViewScoreForKind(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Door:
                return 80;
            case ObjectKind::Container:
                return 72;
            case ObjectKind::Terminal:
                return 68;
            case ObjectKind::Book:
            case ObjectKind::Note:
                return 56;
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Weapon:
            case ObjectKind::Misc:
                return 44;
            case ObjectKind::Activator:
                return 36;
            case ObjectKind::MovableStatic:
                return 16;
            case ObjectKind::Static:
                return 8;
        }
        return 0;
    }

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

    int settlementBootstrapBonus(const BaseRecordData& base, const ESM4::Reference& ref)
    {
        std::string label;
        auto append = [&](std::string_view value) {
            if (value.empty())
                return;
            if (!label.empty())
                label.push_back(' ');
            label.append(value);
        };

        append(ref.mFullName);
        append(base.mFullName);
        append(ref.mEditorId);
        append(base.mEditorId);
        append(base.mModel);

        const std::string lower = Misc::StringUtils::lowerCase(label);
        if (lower.empty())
            return 0;

        static constexpr std::array<std::pair<std::string_view, int>, 11> sPreferredSettlementRefs = { {
            { "megaton", 1400 },
            { "bigtown", 1200 },
            { "big town", 1200 },
            { "canterbury", 1100 },
            { "rivetcity", 1100 },
            { "rivet city", 1100 },
            { "tenpenny", 1000 },
            { "underworld", 1000 },
            { "arefu", 950 },
            { "paradisefalls", 900 },
            { "paradise falls", 900 },
        } };

        for (const auto& [keyword, bonus] : sPreferredSettlementRefs)
        {
            if (lower.find(keyword) != std::string::npos)
                return bonus;
        }

        return 0;
    }

    struct WorldBounds
    {
        bool mValid = false;
        float mMinX = 0.f;
        float mMaxX = 0.f;
        float mMinY = 0.f;
        float mMaxY = 0.f;

        void include(float x, float y)
        {
            if (!mValid)
            {
                mValid = true;
                mMinX = mMaxX = x;
                mMinY = mMaxY = y;
                return;
            }

            mMinX = std::min(mMinX, x);
            mMaxX = std::max(mMaxX, x);
            mMinY = std::min(mMinY, y);
            mMaxY = std::max(mMaxY, y);
        }
    };

    using ExteriorBucketKey = std::tuple<ESM::RefId, int, int>;

    ExteriorBucketKey makeExteriorBucketKey(ESM::RefId worldspace, int x, int y)
    {
        return std::make_tuple(worldspace, x, y);
    }

    ESM::RefId bucketWorldspace(const ExteriorBucketKey& key)
    {
        return std::get<0>(key);
    }

    int bucketX(const ExteriorBucketKey& key)
    {
        return std::get<1>(key);
    }

    int bucketY(const ExteriorBucketKey& key)
    {
        return std::get<2>(key);
    }

    std::string bucketLabel(const ExteriorBucketKey& key)
    {
        std::ostringstream stream;
        stream << bucketWorldspace(key).serializeText() << " (" << bucketX(key) << ", " << bucketY(key) << ")";
        return stream.str();
    }

    struct BootstrapCell
    {
        ESM::RefId mId;
        const ESM4::Cell* mCell = nullptr;
    };

    enum class BucketLoadMode
    {
        LandmarkVisual,
        FarVisual,
        NearFull,
        Interaction,
    };

    std::string_view toString(BucketLoadMode mode)
    {
        switch (mode)
        {
            case BucketLoadMode::LandmarkVisual:
                return "LandmarkVisual";
            case BucketLoadMode::FarVisual:
                return "FarVisual";
            case BucketLoadMode::NearFull:
                return "NearFull";
            case BucketLoadMode::Interaction:
                return "Interaction";
        }

        return "Unknown";
    }

    bool isVisualOnlyBucketMode(BucketLoadMode mode)
    {
        return mode == BucketLoadMode::LandmarkVisual || mode == BucketLoadMode::FarVisual;
    }

    bool isModeAtLeast(BucketLoadMode mode, BucketLoadMode minimum)
    {
        return static_cast<int>(mode) >= static_cast<int>(minimum);
    }

    struct SceneObjectMetadata
    {
        ObjectKind mKind = ObjectKind::Static;
        ESM::FormId mBaseId;
        ESM::FormId mReferenceId;
        ESM::RefId mCellId;
        ExteriorBucketKey mBucketKey = ExteriorBucketKey{};
        std::string mName;
        std::string mEditorId;
        std::string mModelPath;
        std::string mText;
        std::string mResultText;
        std::string mIcon;
        std::string mCategory;
        int mValue = 0;
        float mWeight = 0.f;
        int mItemCount = 0;
        ESM4::TeleportDest mDoor;
        bool mLocked = false;
        int mLockLevel = 0;
        ESM::FormId mKey;
        ESM::FormId mTargetRef;
        ESM::Position mTransform;
        float mScale = 1.f;
        osg::Vec3f mBoundCenter = osg::Vec3f(0.f, 0.f, 0.f);
        float mBoundRadius = 0.f;
        BucketLoadMode mMinLoadMode = BucketLoadMode::NearFull;
        std::size_t mObjectIndex = sInvalidIndex;
        std::size_t mFocusableIndex = sInvalidIndex;
        std::size_t mCollisionEntryIndex = sInvalidIndex;
        bool mRenderable = false;
        bool mEffectOnly = false;
        osg::Vec3f mFocusProxyCenter = osg::Vec3f(0.f, 0.f, 0.f);
        float mFocusProxyRadius = 0.f;
        bool mHasFocusProxy = false;
        SceneUtil::PositionAttitudeTransform* mNode = nullptr;
        btCollisionObject* mCollisionObject = nullptr;
        bool mPickedUp = false;
        bool mReadable = false;
        bool mNoTake = false;
    };

    bool isPlausibleWalkableSupport(const SceneObjectMetadata& metadata)
    {
        return isWalkableSupportKind(metadata.mKind) && metadata.mBoundRadius >= sMinimumWalkableSupportRadius;
    }

    bool isLockableKind(ObjectKind kind)
    {
        return kind == ObjectKind::Door || kind == ObjectKind::Container || kind == ObjectKind::Terminal;
    }

    struct InventoryStack
    {
        ESM::FormId mBaseId;
        ObjectKind mKind = ObjectKind::Misc;
        std::string mName;
        std::string mEditorId;
        std::string mText;
        std::string mIcon;
        std::string mCategory;
        int mValue = 0;
        float mWeight = 0.f;
        int mCount = 0;
        bool mReadable = false;
        bool mNoTake = false;
    };

    std::string chooseDisplayName(
        const BaseRecordData& base, const ESM4::Reference& ref, std::string_view fallbackKind)
    {
        if (!ref.mFullName.empty())
            return ref.mFullName;
        if (!base.mFullName.empty())
            return base.mFullName;
        if (!ref.mEditorId.empty())
            return ref.mEditorId;
        if (!base.mEditorId.empty())
            return base.mEditorId;
        return std::string(fallbackKind);
    }

    VFS::Path::Normalized makeMeshPath(std::string_view model)
    {
        return Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model));
    }

    struct FocusHit
    {
        SceneObjectMetadata* mMetadata = nullptr;
        osg::Vec3f mPoint;
        float mDistance = 0.f;
    };

    struct InitialViewTarget
    {
        const SceneObjectMetadata* mMetadata = nullptr;
        osg::Vec3f mClusterCenter = osg::Vec3f(0.f, 0.f, 0.f);
        int mClusterSize = 0;
        bool mHasCluster = false;
    };

    enum class SceneMode
    {
        Exterior,
        Interior,
    };

    enum class PanelMode
    {
        None,
        Info,
        Container,
        Terminal,
        Inventory,
        Read,
        Access,
    };

    enum class AccessPromptActionType
    {
        UseKey,
        SkillUnlock,
        Close,
    };

    struct AccessPromptAction
    {
        AccessPromptActionType mType = AccessPromptActionType::Close;
        std::string mLabel;
    };

    struct PlayerCapabilities
    {
        int mLockpick = 50;
        int mScience = 50;
    };

    struct ResolvedDoorDestination
    {
        SceneMode mSceneMode = SceneMode::Exterior;
        ESM::RefId mCellId;
        ESM::RefId mWorldspace;
        ESM::Position mPlacement;
    };

    struct NodeRenderStats
    {
        int mRenderableDrawables = 0;
        int mEffectDrawables = 0;
        int mParticleSystems = 0;
        int mRepairedMaskedNodes = 0;

        NodeRenderStats& operator+=(const NodeRenderStats& other)
        {
            mRenderableDrawables += other.mRenderableDrawables;
            mEffectDrawables += other.mEffectDrawables;
            mParticleSystems += other.mParticleSystems;
            mRepairedMaskedNodes += other.mRepairedMaskedNodes;
            return *this;
        }

        [[nodiscard]] bool hasRenderableGeometry() const { return mRenderableDrawables > 0; }
        [[nodiscard]] bool isEffectOnly() const
        {
            return mRenderableDrawables == 0 && (mEffectDrawables > 0 || mParticleSystems > 0);
        }
    };

    struct CellSceneStats
    {
        osg::ref_ptr<osg::Group> mRoot;
        std::size_t mInstantiated = 0;
        std::size_t mCollisionCount = 0;
        std::size_t mRenderableCount = 0;
        std::size_t mEffectOnlyCount = 0;
        osg::BoundingSphere mBounds;
        bool mHasBounds = false;
        bool mSuspiciousBounds = false;
        BucketLoadMode mLoadMode = BucketLoadMode::FarVisual;
    };

    struct LoadedInteriorScene
    {
        ESM::RefId mCellId;
        osg::ref_ptr<osg::Group> mRoot;
        std::vector<SceneObjectMetadata*> mSceneObjects;
        std::size_t mRefCount = 0;
        std::size_t mInstantiated = 0;
        std::size_t mCollisionCount = 0;
        std::size_t mRenderableCount = 0;
        std::size_t mEffectOnlyCount = 0;
        osg::BoundingSphere mBounds;
        bool mHasBounds = false;
        bool mLoaded = false;
    };

    struct LoadedObjectBucket
    {
        BucketLoadMode mMode = BucketLoadMode::FarVisual;
        bool mHasNonInteractiveCollision = false;
        std::vector<SceneObjectMetadata*> mSceneObjects;
    };

    struct SceneMutationStats
    {
        int mModeOnlyBuckets = 0;
        int mRefsInstantiated = 0;
        int mCollisionsAdded = 0;
        int mCollisionsRemoved = 0;
    };

    bool hasSoftEffectFlag(const osg::Node& node)
    {
        bool softEffect = false;
        return node.getUserValue(Misc::OsgUserValues::sXSoftEffect, softEffect) && softEffect;
    }

    bool usesDistortionRenderBin(const osg::StateSet* stateSet)
    {
        if (stateSet == nullptr)
            return false;

        return (stateSet->getRenderBinMode() & osg::StateSet::INHERIT_RENDERBIN_DETAILS) == 0
            && stateSet->getBinName() == "Distortion";
    }

    bool isLikelyEffectModelPath(std::string_view modelPath)
    {
        const std::string lower = Misc::StringUtils::lowerCase(modelPath);
        return lower.starts_with("meshes/effects/") || lower.find("/effects/") != std::string::npos
            || lower.find("/fx") != std::string::npos || lower.find("forcefield") != std::string::npos;
    }

    bool isLikelyLandmarkModelPath(std::string_view modelPath)
    {
        static constexpr std::string_view sLandmarkSubstrings[] = {
            "bridge",
            "overpass",
            "highway",
            "monorail",
            "pylon",
            "tower",
            "buildingkit",
            "facade",
            "suburban",
            "urban",
        };

        const std::string lower = Misc::StringUtils::lowerCase(modelPath);
        for (const std::string_view substring : sLandmarkSubstrings)
        {
            if (lower.find(substring) != std::string::npos)
                return true;
        }

        return false;
    }

    std::string normalizeReadableText(std::string text)
    {
        Misc::StringUtils::replaceAll(text, "\r\n", "\n");
        Misc::StringUtils::replaceAll(text, "\r", "\n");
        Misc::StringUtils::replaceAll(text, "\t", "    ");
        Misc::StringUtils::trim(text);
        return text;
    }

    bool isMissingReadablePlaceholder(std::string_view text)
    {
        return Misc::StringUtils::ciEqual(text, "No readable text was parsed for this record.");
    }

    bool isGenericBookText(std::string_view text, std::string_view title)
    {
        if (text.empty())
            return true;
        if (Misc::StringUtils::ciEqual(text, "This is a book."))
            return true;
        if (Misc::StringUtils::ciEqual(text, "This is a book"))
            return true;
        if (!title.empty() && Misc::StringUtils::ciEqual(text, title))
            return true;
        return false;
    }

    std::string skillNameForBookEditorId(std::string_view editorId)
    {
        static constexpr std::pair<std::string_view, std::string_view> sSkillBooks[] = {
            { "BookSkillBarter", "Barter" },
            { "BookSkillBigGuns", "Big Guns" },
            { "BookSkillEnergyWeapons", "Energy Weapons" },
            { "BookSkillExplosives", "Explosives" },
            { "BookSkillLockpicking", "Lockpick" },
            { "BookSkillMedicine", "Medicine" },
            { "BookSkillMelee", "Melee Weapons" },
            { "BookSkillRepair", "Repair" },
            { "BookSkillScience", "Science" },
            { "BookSkillSmallGuns", "Small Guns" },
            { "BookSkillSneak", "Sneak" },
            { "BookSkillSpeech", "Speech" },
            { "BookSkillUnarmed", "Unarmed" },
        };

        for (const auto& [id, skill] : sSkillBooks)
        {
            if (Misc::StringUtils::ciEqual(editorId, id))
                return std::string(skill);
        }

        return {};
    }

    std::string preferredBookText(std::string_view title, const ESM4::Book& record)
    {
        std::string description = normalizeReadableText(record.mDescription);
        if (!isGenericBookText(description, title))
            return description;

        std::string text = normalizeReadableText(record.mText);
        if (!isGenericBookText(text, title))
            return text;

        return {};
    }

    NodeRenderStats analyzeSceneNode(osg::Node& node, bool effectPath, bool repairMaskedNodes)
    {
        effectPath = effectPath || hasSoftEffectFlag(node) || usesDistortionRenderBin(node.getStateSet());

        NodeRenderStats stats;
        if (auto* geode = node.asGeode())
        {
            for (unsigned int i = 0; i < geode->getNumDrawables(); ++i)
            {
                osg::Drawable* drawable = geode->getDrawable(i);
                if (drawable == nullptr)
                    continue;

                const bool effectDrawable = effectPath || usesDistortionRenderBin(drawable->getStateSet());
                if (dynamic_cast<osgParticle::ParticleSystem*>(drawable) != nullptr)
                    ++stats.mParticleSystems;
                else if (effectDrawable)
                    ++stats.mEffectDrawables;
                else
                    ++stats.mRenderableDrawables;
            }
        }

        if (auto* group = node.asGroup())
        {
            for (unsigned int i = 0; i < group->getNumChildren(); ++i)
            {
                osg::Node* child = group->getChild(i);
                if (child != nullptr)
                    stats += analyzeSceneNode(*child, effectPath, repairMaskedNodes);
            }
        }

        if (repairMaskedNodes && stats.hasRenderableGeometry() && node.getNodeMask() == 0u)
        {
            node.setNodeMask(sObjectNodeMask);
            ++stats.mRepairedMaskedNodes;
        }

        return stats;
    }

    std::optional<float> raySphereHitDistance(
        const osg::Vec3f& from, const osg::Vec3f& to, const osg::Vec3f& center, float radius)
    {
        const osg::Vec3f delta = to - from;
        const float maxDistance = delta.length();
        if (maxDistance <= 0.f)
            return std::nullopt;

        const osg::Vec3f direction = delta / maxDistance;
        const osg::Vec3f originToCenter = from - center;
        const float b = direction * originToCenter;
        const float c = originToCenter.length2() - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.f)
            return std::nullopt;

        const float sqrtDiscriminant = std::sqrt(discriminant);
        float hitDistance = -b - sqrtDiscriminant;
        if (hitDistance < 0.f)
            hitDistance = -b + sqrtDiscriminant;
        if (hitDistance < 0.f || hitDistance > maxDistance)
            return std::nullopt;

        return hitDistance;
    }

    class LoadedContent
    {
    public:
        void load(const Files::Collections& fileCollections, const std::vector<std::string>& contentFiles,
            const VFS::Manager* vfs, const ToUTF8::StatelessUtf8Encoder* encoder)
        {
            std::map<std::string, int> fileToIndex;
            for (std::size_t i = 0; i < contentFiles.size(); ++i)
            {
                const std::string lower = Misc::StringUtils::lowerCase(contentFiles[i]);
                fileToIndex[lower] = static_cast<int>(i);
                fileToIndex[Misc::StringUtils::lowerCase(std::filesystem::path(contentFiles[i]).filename().string())]
                    = static_cast<int>(i);
            }

            for (std::size_t i = 0; i < contentFiles.size(); ++i)
            {
                const std::filesystem::path path = fileCollections.getPath(contentFiles[i]);
                Log(Debug::Info) << "Loading FO3 content file: " << path;
                ESM4::Reader reader(
                    Files::openConstrainedFileStream(path), path, vfs, encoder, true);
                reader.setModIndex(static_cast<std::uint32_t>(i));
                reader.updateModIndices(fileToIndex);

                ESM4::ReaderUtils::readAll(
                    reader,
                    [&](ESM4::Reader& activeReader) { return handleRecord(activeReader); },
                    [](ESM4::Reader&) {});
            }

            rebuildDerivedIndices();
        }

        std::optional<BootstrapCell> chooseBootstrapCell() const
        {
            std::optional<BootstrapCell> bestCell;
            int bestScore = std::numeric_limits<int>::min();

            for (const auto& [cellId, cell] : mCells)
            {
                if (!cell.isExterior() || !hasLand(cell.mParent, cell.mX, cell.mY))
                    continue;

                if (const auto* refs = getReferences(cellId); refs != nullptr)
                {
                    int interactiveScore = 0;
                    int renderableRefs = 0;
                    int doorCount = 0;
                    int containerCount = 0;
                    int terminalCount = 0;
                    int settlementRefBonus = 0;
                    for (const auto& [refId, ref] : *refs)
                    {
                        (void)refId;
                        const BaseRecordData* base = findBase(ref.mBaseObj);
                        if (base == nullptr)
                            continue;

                        interactiveScore += bootstrapScoreForKind(base->mKind);
                        settlementRefBonus += settlementBootstrapBonus(*base, ref);
                        if (!base->mModel.empty())
                            ++renderableRefs;
                        if (base->mKind == ObjectKind::Door)
                            ++doorCount;
                        else if (base->mKind == ObjectKind::Container)
                            ++containerCount;
                        else if (base->mKind == ObjectKind::Terminal)
                            ++terminalCount;
                    }

                    if (interactiveScore == 0 && renderableRefs == 0)
                        continue;

                    const int settlementBonus = settlementBootstrapBonus(cell);
                    const int totalScore = settlementBonus + settlementRefBonus + interactiveScore * 8
                        + renderableRefs * 3 + doorCount * 160 + containerCount * 48 + terminalCount * 64;

                    if (!bestCell.has_value() || totalScore > bestScore)
                    {
                        bestCell = BootstrapCell{ cellId, &cell };
                        bestScore = totalScore;
                    }
                }
            }

            if (bestCell.has_value())
                return bestCell;

            for (const auto& [cellId, cell] : mCells)
            {
                if (cell.isExterior() && hasLand(cell.mParent, cell.mX, cell.mY))
                    return BootstrapCell{ cellId, &cell };
            }

            return std::nullopt;
        }

        const BaseRecordData* findBase(ESM::FormId id) const
        {
            const auto it = mBaseRecords.find(id);
            return it == mBaseRecords.end() ? nullptr : &it->second;
        }

        const ESM4::LevelledItem* findLevelledItem(ESM::FormId id) const
        {
            const auto it = mLevelledItems.find(id);
            return it == mLevelledItems.end() ? nullptr : &it->second;
        }

        const ESM4::FormIdList* findFormList(ESM::FormId id) const
        {
            const auto it = mFormLists.find(id);
            return it == mFormLists.end() ? nullptr : &it->second;
        }

        const ESM4::Land* findLand(ESM::RefId worldspace, int x, int y) const
        {
            const auto it = mLandByCell.find(std::make_tuple(worldspace, x, y));
            if (it == mLandByCell.end())
                return nullptr;

            const auto landIt = mLands.find(it->second);
            return landIt == mLands.end() ? nullptr : &landIt->second;
        }

        bool hasLand(ESM::RefId worldspace, int x, int y) const
        {
            return mLandByCell.find(std::make_tuple(worldspace, x, y)) != mLandByCell.end();
        }

        const std::map<ESM::FormId, ESM4::Reference>* getReferences(ESM::RefId cellId) const
        {
            const auto it = mReferencesByCell.find(cellId);
            return it == mReferencesByCell.end() ? nullptr : &it->second;
        }

        const ESM4::Reference* findReference(ESM::FormId id) const
        {
            const auto it = mReferenceById.find(id);
            return it == mReferenceById.end() ? nullptr : it->second;
        }

        const ESM4::LandTexture* findLandTexture(ESM::FormId id) const
        {
            const auto it = mLandTextures.find(id);
            return it == mLandTextures.end() ? nullptr : &it->second;
        }

        const ESM4::Cell* findCell(ESM::RefId id) const
        {
            const auto it = mCells.find(id);
            return it == mCells.end() ? nullptr : &it->second;
        }

        std::optional<ESM::RefId> findExteriorCellId(ESM::RefId worldspace, int x, int y) const
        {
            const auto it = mExteriorCellByCoord.find(std::make_tuple(worldspace, x, y));
            if (it == mExteriorCellByCoord.end())
                return std::nullopt;
            return it->second;
        }

        const std::vector<const ESM4::Reference*>* getExteriorSpatialReferences(const ExteriorBucketKey& key) const
        {
            const auto it = mExteriorRefsBySpatialCell.find(key);
            return it == mExteriorRefsBySpatialCell.end() ? nullptr : &it->second;
        }

        const ESM4::TextureSet* findTextureSet(ESM::FormId id) const
        {
            const auto it = mTextureSets.find(id);
            return it == mTextureSets.end() ? nullptr : &it->second;
        }

        const WorldBounds* getWorldBounds(ESM::RefId worldspace) const
        {
            const auto it = mWorldBounds.find(worldspace);
            return it == mWorldBounds.end() ? nullptr : &it->second;
        }

    private:
        std::map<ESM::FormId, BaseRecordData> mBaseRecords;
        std::map<ESM::FormId, ESM4::LevelledItem> mLevelledItems;
        std::map<ESM::FormId, ESM4::FormIdList> mFormLists;
        std::map<ESM::RefId, ESM4::Cell> mCells;
        std::map<ESM::FormId, ESM4::World> mWorlds;
        std::map<ESM::FormId, ESM4::Land> mLands;
        std::map<ESM::FormId, ESM4::LandTexture> mLandTextures;
        std::map<ESM::FormId, ESM4::TextureSet> mTextureSets;
        std::map<ESM::RefId, std::map<ESM::FormId, ESM4::Reference>> mReferencesByCell;

        std::map<std::tuple<ESM::RefId, int, int>, ESM::FormId> mLandByCell;
        std::map<ESM::FormId, const ESM4::Reference*> mReferenceById;
        std::map<ESM::RefId, WorldBounds> mWorldBounds;
        std::map<std::tuple<ESM::RefId, int, int>, ESM::RefId> mExteriorCellByCoord;
        std::map<ExteriorBucketKey, std::vector<const ESM4::Reference*>> mExteriorRefsBySpatialCell;

        bool handleRecord(ESM4::Reader& reader)
        {
            if ((reader.hdr().record.flags & ESM::FLAG_Ignored) != 0)
                return false;

            reader.getRecordData();

            switch (reader.hdr().record.typeId)
            {
                case ESM4::REC_WRLD:
                {
                    ESM4::World record;
                    record.load(reader);
                    mWorlds[record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_CELL:
                {
                    ESM4::Cell record;
                    record.load(reader);
                    mCells[record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_LAND:
                {
                    ESM4::Land record;
                    record.load(reader);
                    mLands[record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_LTEX:
                {
                    ESM4::LandTexture record;
                    record.load(reader);
                    mLandTextures[record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_TXST:
                {
                    ESM4::TextureSet record;
                    record.load(reader);
                    mTextureSets[record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_REFR:
                {
                    ESM4::Reference record;
                    record.load(reader);
                    mReferencesByCell[record.mParent][record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_STAT:
                {
                    ESM4::Static record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Static,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mCategory = std::string(toString(ObjectKind::Static)) };
                    return true;
                }
                case ESM4::REC_MSTT:
                {
                    ESM4::MovableStatic record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::MovableStatic,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mCategory = std::string(toString(ObjectKind::MovableStatic)) };
                    return true;
                }
                case ESM4::REC_DOOR:
                {
                    ESM4::Door record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Door,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mCategory = std::string(toString(ObjectKind::Door)) };
                    return true;
                }
                case ESM4::REC_CONT:
                {
                    ESM4::Container record;
                    record.load(reader);
                    int itemCount = 0;
                    for (const auto& item : record.mInventory)
                        itemCount += static_cast<int>(item.count);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Container,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mCategory = std::string(toString(ObjectKind::Container)),
                        .mItemCount = itemCount,
                        .mContainerItems = record.mInventory };
                    return true;
                }
                case ESM4::REC_ACTI:
                {
                    ESM4::Activator record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Activator,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mCategory = std::string(toString(ObjectKind::Activator)),
                        .mText = record.mActivationPrompt };
                    return true;
                }
                case ESM4::REC_TERM:
                {
                    ESM4::Terminal record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Terminal,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mCategory = std::string(toString(ObjectKind::Terminal)),
                        .mText = record.mText,
                        .mResultText = record.mResultText };
                    return true;
                }
                case ESM4::REC_BOOK:
                {
                    ESM4::Book record;
                    record.load(reader);
                    const std::string title = !record.mFullName.empty() ? record.mFullName : record.mEditorId;
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Book,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mText = preferredBookText(title, record),
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Book)),
                        .mValue = static_cast<int>(record.mData.value),
                        .mWeight = record.mData.weight,
                        .mReadable = true,
                        .mNoTake = (record.mData.flags & ESM4::Book::Flag_NoTake) != 0 };
                    return true;
                }
                case ESM4::REC_NOTE:
                {
                    ESM4::Note record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Note,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mText = record.mText,
                        .mCategory = std::string(toString(ObjectKind::Note)),
                        .mReadable = true };
                    return true;
                }
                case ESM4::REC_MISC:
                {
                    ESM4::MiscItem record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Misc,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Misc)),
                        .mValue = static_cast<int>(record.mData.value),
                        .mWeight = record.mData.weight };
                    return true;
                }
                case ESM4::REC_ALCH:
                {
                    ESM4::Potion record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Aid,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Aid)),
                        .mValue = static_cast<int>(record.mItem.value),
                        .mWeight = record.mData.weight };
                    return true;
                }
                case ESM4::REC_AMMO:
                {
                    ESM4::Ammunition record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Ammo,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName.empty() ? record.mShortName : record.mFullName,
                        .mModel = record.mModel,
                        .mText = record.mText,
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Ammo)),
                        .mValue = static_cast<int>(record.mData.mValue),
                        .mWeight = record.mData.mWeight };
                    return true;
                }
                case ESM4::REC_ARMO:
                {
                    ESM4::Armor record;
                    record.load(reader);
                    const std::string& model = !record.mModel.empty() ? record.mModel
                        : (!record.mModelMaleWorld.empty() ? record.mModelMaleWorld
                                                           : (!record.mModelMale.empty() ? record.mModelMale
                                                                                         : record.mModelFemaleWorld));
                    const std::string& icon = !record.mIconMale.empty() ? record.mIconMale : record.mIconFemale;
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Armor,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = model,
                        .mText = record.mText,
                        .mIcon = icon,
                        .mCategory = std::string(toString(ObjectKind::Armor)),
                        .mValue = static_cast<int>(record.mData.value),
                        .mWeight = record.mData.weight };
                    return true;
                }
                case ESM4::REC_KEYM:
                {
                    ESM4::Key record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Key,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Key)),
                        .mValue = static_cast<int>(record.mData.value),
                        .mWeight = record.mData.weight };
                    return true;
                }
                case ESM4::REC_LIGH:
                {
                    ESM4::Light record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Light,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Light)),
                        .mValue = static_cast<int>(record.mData.value),
                        .mWeight = record.mData.weight };
                    return true;
                }
                case ESM4::REC_WEAP:
                {
                    ESM4::Weapon record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Weapon,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mText = record.mText,
                        .mIcon = record.mIcon,
                        .mCategory = std::string(toString(ObjectKind::Weapon)),
                        .mValue = static_cast<int>(record.mData.value),
                        .mWeight = record.mData.weight };
                    return true;
                }
                case ESM4::REC_LVLI:
                {
                    ESM4::LevelledItem record;
                    record.load(reader);
                    mLevelledItems[record.mId] = std::move(record);
                    return true;
                }
                case ESM4::REC_FLST:
                {
                    ESM4::FormIdList record;
                    record.load(reader);
                    mFormLists[record.mId] = std::move(record);
                    return true;
                }
                default:
                    return false;
            }
        }

        void rebuildDerivedIndices()
        {
            mLandByCell.clear();
            mReferenceById.clear();
            mWorldBounds.clear();
            mExteriorCellByCoord.clear();
            mExteriorRefsBySpatialCell.clear();

            for (const auto& [cellId, cell] : mCells)
            {
                if (!cell.isExterior())
                    continue;

                mExteriorCellByCoord[std::make_tuple(cell.mParent, cell.mX, cell.mY)] = cellId;
            }

            for (const auto& [cellId, refs] : mReferencesByCell)
            {
                for (const auto& [refId, ref] : refs)
                {
                    (void)refId;
                    mReferenceById[ref.mId] = &ref;
                }
            }

            for (const auto& [landId, land] : mLands)
            {
                const ESM::RefId cellId = ESM::RefId::formIdRefId(land.mCell);
                const auto cellIt = mCells.find(cellId);
                if (cellIt == mCells.end() || !cellIt->second.isExterior())
                    continue;

                const ESM4::Cell& cell = cellIt->second;
                mLandByCell[std::make_tuple(cell.mParent, cell.mX, cell.mY)] = landId;
                mWorldBounds[cell.mParent].include(static_cast<float>(cell.mX), static_cast<float>(cell.mY));
            }

            std::size_t rebucketedRefs = 0;
            std::size_t farRebucketedRefs = 0;
            std::size_t loggedFarRebucketedRefs = 0;
            for (const auto& [cellId, refs] : mReferencesByCell)
            {
                const auto cellIt = mCells.find(cellId);
                if (cellIt == mCells.end() || !cellIt->second.isExterior())
                    continue;

                const ESM4::Cell& cell = cellIt->second;
                const ESM::RefId worldspace = cell.mParent;
                for (const auto& [refId, ref] : refs)
                {
                    (void)refId;
                    const ESM::ExteriorCellLocation spatialCell
                        = ESM::positionToExteriorCellLocation(ref.mPos.pos[0], ref.mPos.pos[1], worldspace);
                    const ExteriorBucketKey bucketKey = makeExteriorBucketKey(worldspace, spatialCell.mX, spatialCell.mY);
                    mExteriorRefsBySpatialCell[bucketKey].push_back(&ref);

                    const int dx = std::abs(spatialCell.mX - cell.mX);
                    const int dy = std::abs(spatialCell.mY - cell.mY);
                    if (dx == 0 && dy == 0)
                        continue;

                    ++rebucketedRefs;
                    if (std::max(dx, dy) <= 1)
                        continue;

                    ++farRebucketedRefs;
                    if (loggedFarRebucketedRefs < 12)
                    {
                        Log(Debug::Info) << "OpenFO3 spatially re-bucketed ref " << ESM::RefId(ref.mId).serializeText()
                                         << " from source cell (" << cell.mX << ", " << cell.mY << ") to bucket ("
                                         << spatialCell.mX << ", " << spatialCell.mY << ")";
                        ++loggedFarRebucketedRefs;
                    }
                }
            }

            if (rebucketedRefs > 0)
            {
                Log(Debug::Info) << "OpenFO3 spatially re-bucketed " << rebucketedRefs
                                 << " exterior refs by world position; far-outlier refs=" << farRebucketedRefs;
            }
        }
    };

    class Fo3TerrainStorage final : public ESMTerrain::Storage
    {
    public:
        Fo3TerrainStorage(const VFS::Manager* vfs, const LoadedContent& content, ESM::RefId worldspace)
            : ESMTerrain::Storage(vfs)
            , mContent(content)
            , mWorldspace(worldspace)
        {
        }

        osg::ref_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellLocation) override
        {
            if (const ESM4::Land* land = mContent.findLand(cellLocation.mWorldspace, cellLocation.mX, cellLocation.mY))
                return new ESMTerrain::LandObject(*land,
                    ESM4::Land::LAND_VNML | ESM4::Land::LAND_VHGT | ESM4::Land::LAND_VCLR | ESM4::Land::LAND_VTEX);
            return nullptr;
        }

        const std::string* getLandTexture(std::uint16_t, int) override
        {
            return nullptr;
        }

        const ESM4::LandTexture* getEsm4LandTexture(ESM::RefId id) const override
        {
            const auto* formId = id.getIf<ESM::FormId>();
            return formId == nullptr ? nullptr : mContent.findLandTexture(*formId);
        }

        const ESM4::TextureSet* getEsm4TextureSet(ESM::RefId id) const override
        {
            const auto* formId = id.getIf<ESM::FormId>();
            return formId == nullptr ? nullptr : mContent.findTextureSet(*formId);
        }

        void getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId worldspace) override
        {
            if (worldspace != mWorldspace)
            {
                minX = maxX = minY = maxY = 0.f;
                return;
            }

            if (const WorldBounds* bounds = mContent.getWorldBounds(worldspace); bounds != nullptr && bounds->mValid)
            {
                minX = bounds->mMinX;
                maxX = bounds->mMaxX;
                minY = bounds->mMinY;
                maxY = bounds->mMaxY;
                return;
            }

            minX = maxX = minY = maxY = 0.f;
        }

    private:
        const LoadedContent& mContent;
        ESM::RefId mWorldspace;
    };

    class CollisionScene
    {
    public:
        struct RayHit
        {
            SceneObjectMetadata* mMetadata = nullptr;
            osg::Vec3f mPoint;
            osg::Vec3f mNormal = osg::Vec3f(0.f, 0.f, 1.f);
            float mDistance = 0.f;
        };

        CollisionScene()
            : mDispatcher(&mConfiguration)
            , mWorld(&mDispatcher, &mBroadphase, &mConfiguration)
        {
        }

        void addStatic(Resource::BulletShapeManager& manager, VFS::Path::NormalizedView mesh, const ESM::Position& pos,
            float scale, SceneObjectMetadata& metadata)
        {
            osg::ref_ptr<Resource::BulletShapeInstance> shape;
            try
            {
                shape = manager.getInstance(mesh);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Skipping collision for '" << mesh << "': " << e.what();
                return;
            }
            if (shape == nullptr || shape->mCollisionShape == nullptr)
                return;

            shape->setLocalScaling(btVector3(scale, scale, scale));

            auto object = std::make_unique<btCollisionObject>();
            object->setCollisionShape(shape->mCollisionShape.get());
            object->setWorldTransform(Misc::Convert::makeBulletTransform(pos));
            object->setCollisionFlags(object->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
            object->setUserPointer(&metadata);

            metadata.mCollisionObject = object.get();
            metadata.mCollisionEntryIndex = mEntries.size();
            mWorld.addCollisionObject(object.get());

            StaticCollisionEntry entry;
            entry.mShape = std::move(shape);
            entry.mObject = std::move(object);
            entry.mMetadata = &metadata;
            mEntries.push_back(std::move(entry));
        }

        void remove(SceneObjectMetadata& metadata)
        {
            if (metadata.mCollisionObject == nullptr)
                return;

            btCollisionObject* const collisionObject = metadata.mCollisionObject;
            collisionObject->setUserPointer(nullptr);
            mWorld.removeCollisionObject(collisionObject);
            std::size_t index = metadata.mCollisionEntryIndex;
            if (index >= mEntries.size() || mEntries[index].mObject.get() != collisionObject)
            {
                const auto it = std::find_if(mEntries.begin(), mEntries.end(), [&](const StaticCollisionEntry& entry) {
                    return entry.mObject.get() == collisionObject;
                });
                if (it == mEntries.end())
                {
                    metadata.mCollisionObject = nullptr;
                    metadata.mCollisionEntryIndex = sInvalidIndex;
                    return;
                }
                index = static_cast<std::size_t>(std::distance(mEntries.begin(), it));
            }

            const std::size_t lastIndex = mEntries.size() - 1;
            if (index != lastIndex)
            {
                mEntries[index] = std::move(mEntries[lastIndex]);
                if (mEntries[index].mMetadata != nullptr)
                    mEntries[index].mMetadata->mCollisionEntryIndex = index;
            }
            mEntries.pop_back();
            metadata.mCollisionObject = nullptr;
            metadata.mCollisionEntryIndex = sInvalidIndex;
        }

        std::optional<RayHit> raycast(const osg::Vec3f& from, const osg::Vec3f& to) const
        {
            btCollisionWorld::ClosestRayResultCallback callback(Misc::Convert::toBullet(from), Misc::Convert::toBullet(to));
            mWorld.rayTest(Misc::Convert::toBullet(from), Misc::Convert::toBullet(to), callback);
            if (!callback.hasHit() || callback.m_collisionObject == nullptr)
                return std::nullopt;

            auto* metadata = static_cast<SceneObjectMetadata*>(callback.m_collisionObject->getUserPointer());
            if (metadata == nullptr)
                return std::nullopt;

            const osg::Vec3f point = Misc::Convert::makeOsgVec3f(callback.m_hitPointWorld);
            const osg::Vec3f normal = Misc::Convert::makeOsgVec3f(callback.m_hitNormalWorld);
            return RayHit{ metadata, point, normal, (point - from).length() };
        }

        std::optional<RayHit> probeDown(const osg::Vec3f& from, float distance) const
        {
            if (distance <= 0.f)
                return std::nullopt;

            return raycast(from, from - osg::Vec3f(0.f, 0.f, distance));
        }

        std::optional<RayHit> probeGround(const osg::Vec3f& from, float distance) const
        {
            if (distance <= 0.f)
                return std::nullopt;

            btCollisionWorld::AllHitsRayResultCallback callback(
                Misc::Convert::toBullet(from), Misc::Convert::toBullet(from - osg::Vec3f(0.f, 0.f, distance)));
            mWorld.rayTest(callback.m_rayFromWorld, callback.m_rayToWorld, callback);
            if (!callback.hasHit())
                return std::nullopt;

            std::vector<std::pair<btScalar, int>> orderedHits;
            orderedHits.reserve(callback.m_collisionObjects.size());
            for (int i = 0; i < callback.m_collisionObjects.size(); ++i)
                orderedHits.emplace_back(callback.m_hitFractions[i], i);
            std::sort(orderedHits.begin(), orderedHits.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; });

            for (const auto& [fraction, index] : orderedHits)
            {
                auto* metadata = static_cast<SceneObjectMetadata*>(callback.m_collisionObjects[index]->getUserPointer());
                if (metadata == nullptr || !isPlausibleWalkableSupport(*metadata))
                    continue;

                const osg::Vec3f normal = Misc::Convert::makeOsgVec3f(callback.m_hitNormalWorld[index]);
                if (!std::isfinite(normal.x()) || !std::isfinite(normal.y()) || !std::isfinite(normal.z())
                    || normal.z() < sWalkableNormalMinZ)
                {
                    continue;
                }

                const osg::Vec3f point = Misc::Convert::makeOsgVec3f(callback.m_hitPointWorld[index]);
                return RayHit{ metadata, point, normal, distance * static_cast<float>(fraction) };
            }

            return std::nullopt;
        }

        osg::Vec3f sweep(const osg::Vec3f& from, const osg::Vec3f& to, float radius) const
        {
            btSphereShape shape(radius);
            btTransform start(btQuaternion::getIdentity(), Misc::Convert::toBullet(from));
            btTransform end(btQuaternion::getIdentity(), Misc::Convert::toBullet(to));
            btCollisionWorld::ClosestConvexResultCallback callback(start.getOrigin(), end.getOrigin());
            mWorld.convexSweepTest(&shape, start, end, callback);

            if (!callback.hasHit())
                return to;

            const btScalar fraction = std::max<btScalar>(0.0, callback.m_closestHitFraction - btScalar(0.05));
            const btVector3 delta = end.getOrigin() - start.getOrigin();
            return Misc::Convert::makeOsgVec3f(start.getOrigin() + delta * fraction);
        }

    private:
        struct StaticCollisionEntry
        {
            osg::ref_ptr<Resource::BulletShapeInstance> mShape;
            std::unique_ptr<btCollisionObject> mObject;
            SceneObjectMetadata* mMetadata = nullptr;
        };

        btDefaultCollisionConfiguration mConfiguration;
        btCollisionDispatcher mDispatcher;
        btDbvtBroadphase mBroadphase;
        btCollisionWorld mWorld;
        std::vector<StaticCollisionEntry> mEntries;
    };
}

struct OpenFO3::Engine::Impl
{
    explicit Impl(Files::ConfigurationManager& configurationManager)
        : mCfgMgr(configurationManager)
    {
    }

    ~Impl()
    {
        mTerrain.reset();
        mTerrainStorage.reset();
        mCollisionScene.reset();
        mBulletShapeManager.reset();
        mResourceSystem.reset();
        mVFS.reset();
        mViewer = nullptr;
        mSceneRoot = nullptr;
        mWorldRoot = nullptr;
        mOverlayCamera = nullptr;
        mStatusText = nullptr;
        mFocusText = nullptr;
        mPanelRoot = nullptr;
        mPanelFrameGeode = nullptr;
        mPanelFrameBackground = nullptr;
        mPanelFrameOutline = nullptr;
        mPanelTextGeode = nullptr;
        mPanelTitleText = nullptr;
        mPanelBodyText = nullptr;
        mPanelFooterText = nullptr;

        if (mWindow != nullptr)
        {
            SDL_DestroyWindow(mWindow);
            mWindow = nullptr;
        }

        if (mSdlInitialized)
            SDL_Quit();
    }

    void setDataDirs(const Files::PathContainer& dataDirs)
    {
        mDataDirs = dataDirs;
        rebuildFileCollections();
    }

    void addArchive(const std::string& archive)
    {
        mArchives.push_back(archive);
    }

    void setResourceDir(const std::filesystem::path& resourceDir)
    {
        mResDir = resourceDir;
        if (!mDataDirs.empty())
            rebuildFileCollections();
    }

    void addContentFile(const std::string& file)
    {
        mContentFiles.push_back(file);
    }

    void setEncoding(const ToUTF8::FromType& encoding)
    {
        mEncoding = encoding;
    }

    void setGrabMouse(bool grab)
    {
        mGrabMouse = grab;
    }

    void go()
    {
        ensureSdlInitialized();
        mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(mEncoding);

        createViewer();
        createWindow();
        applyMouseMode(mGrabMouse);
        initializeResources();
        loadContent();
        buildWorld();
        runLoop();
    }

    std::filesystem::path resolveResourceDataDir() const
    {
        const std::array<std::filesystem::path, 3> candidates = {
            mResDir / "vfs",
            mResDir / "data",
            mResDir / "resources" / "vfs",
        };

        for (const auto& candidate : candidates)
        {
            if (!candidate.empty() && std::filesystem::is_directory(candidate))
                return candidate;
        }

        std::ostringstream stream;
        stream << "OpenFO3 could not locate a resource data directory under " << mResDir
               << " (looked for vfs, data, and resources/vfs)";
        throw std::runtime_error(stream.str());
    }

    void rebuildFileCollections()
    {
        Files::PathContainer allDataDirs = mDataDirs;
        const std::filesystem::path resourceDataDir = resolveResourceDataDir();
        Log(Debug::Info) << "Using OpenFO3 resource data directory " << resourceDataDir;
        allDataDirs.insert(allDataDirs.begin(), resourceDataDir);
        mFileCollections = Files::Collections(allDataDirs);
    }

    void ensureSdlInitialized()
    {
        if (mSdlInitialized)
            return;

        SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");

        const Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
        if (SDL_WasInit(flags) == 0)
        {
            SDL_SetMainReady();
            if (SDL_Init(flags) != 0)
                throw std::runtime_error("Could not initialize SDL: " + std::string(SDL_GetError()));
        }
        mSdlInitialized = true;
    }

    void createViewer()
    {
        if (osgUtil::RenderBin::getRenderBinPrototype("Distortion") == nullptr)
        {
            osg::ref_ptr<osgUtil::RenderBin> distortionBin
                = new osgUtil::RenderBin(osgUtil::RenderBin::SORT_BACK_TO_FRONT);
            osgUtil::RenderBin::addRenderBinPrototype("Distortion", distortionBin);
        }

        mViewer = new osgViewer::Viewer;
        mViewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
        mViewer->setLightingMode(osg::View::NO_LIGHT);

        mLightRoot = new SceneUtil::LightManager;
        mLightRoot->setName("OpenFO3 Root");
        mLightRoot->setStartLight(1);
        mSceneRoot = mLightRoot;

        mWorldRoot = new osg::Group;
        mWorldRoot->setName("World Root");
        mSceneRoot->addChild(mWorldRoot);

        mOverlayCamera = new osg::Camera;
        mOverlayCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        mOverlayCamera->setRenderOrder(osg::Camera::POST_RENDER);
        mOverlayCamera->setClearMask(GL_DEPTH_BUFFER_BIT);
        mOverlayCamera->setAllowEventFocus(false);

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        mStatusText = createOverlayText(20.f);
        mFocusText = createOverlayText(24.f);
        geode->addDrawable(mStatusText);
        geode->addDrawable(mFocusText);
        mOverlayCamera->addChild(geode);

        mPanelRoot = new osg::Group;
        mPanelRoot->setName("Interaction Panel");
        mPanelRoot->setNodeMask(0u);

        mPanelFrameGeode = new osg::Geode;
        osg::StateSet* frameState = mPanelFrameGeode->getOrCreateStateSet();
        frameState->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        frameState->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        frameState->setMode(GL_BLEND, osg::StateAttribute::ON);
        frameState->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
        frameState->setAttributeAndModes(new osg::BlendFunc(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE_MINUS_SRC_ALPHA),
            osg::StateAttribute::ON);

        mPanelFrameBackground = new osg::Geometry;
        mPanelFrameBackground->setUseDisplayList(false);
        mPanelFrameBackground->setUseVertexBufferObjects(true);
        mPanelFrameBackgroundVertices = new osg::Vec3Array(4);
        (*mPanelFrameBackgroundVertices)[0].set(0.f, 0.f, 0.f);
        (*mPanelFrameBackgroundVertices)[1].set(0.f, 0.f, 0.f);
        (*mPanelFrameBackgroundVertices)[2].set(0.f, 0.f, 0.f);
        (*mPanelFrameBackgroundVertices)[3].set(0.f, 0.f, 0.f);
        mPanelFrameBackground->setVertexArray(mPanelFrameBackgroundVertices);
        osg::ref_ptr<osg::Vec4Array> frameColors = new osg::Vec4Array;
        frameColors->push_back(osg::Vec4(0.05f, 0.07f, 0.09f, 0.88f));
        mPanelFrameBackground->setColorArray(frameColors, osg::Array::BIND_OVERALL);
        mPanelFrameBackground->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_FAN, 0, 4));
        mPanelFrameGeode->addDrawable(mPanelFrameBackground);

        mPanelFrameOutline = new osg::Geometry;
        mPanelFrameOutline->setUseDisplayList(false);
        mPanelFrameOutline->setUseVertexBufferObjects(true);
        mPanelFrameOutlineVertices = new osg::Vec3Array(5);
        mPanelFrameOutline->setVertexArray(mPanelFrameOutlineVertices);
        osg::ref_ptr<osg::Vec4Array> outlineColors = new osg::Vec4Array;
        outlineColors->push_back(osg::Vec4(0.70f, 0.82f, 0.98f, 0.95f));
        mPanelFrameOutline->setColorArray(outlineColors, osg::Array::BIND_OVERALL);
        mPanelFrameOutline->addPrimitiveSet(new osg::DrawArrays(GL_LINE_STRIP, 0, 5));
        mPanelFrameGeode->addDrawable(mPanelFrameOutline);

        mPanelTextGeode = new osg::Geode;
        mPanelTextGeode->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        mPanelTextGeode->getOrCreateStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        mPanelTitleText = createOverlayText(28.f);
        mPanelTitleText->setColor(osg::Vec4(0.96f, 0.99f, 1.f, 1.f));
        mPanelBodyText = createOverlayText(19.f);
        mPanelBodyText->setColor(osg::Vec4(0.94f, 0.96f, 0.98f, 1.f));
        mPanelFooterText = createOverlayText(16.f);
        mPanelFooterText->setColor(osg::Vec4(0.78f, 0.84f, 0.90f, 1.f));
        mPanelTextGeode->addDrawable(mPanelTitleText);
        mPanelTextGeode->addDrawable(mPanelBodyText);
        mPanelTextGeode->addDrawable(mPanelFooterText);

        mPanelRoot->addChild(mPanelFrameGeode);
        mPanelRoot->addChild(mPanelTextGeode);
        mOverlayCamera->addChild(mPanelRoot);
        mSceneRoot->addChild(mOverlayCamera);

        mViewer->setSceneData(mSceneRoot);
    }

    osg::ref_ptr<osgText::Text> createOverlayText(float size)
    {
        osg::ref_ptr<osgText::Text> text = new osgText::Text;
        text->setCharacterSize(size);
        text->setAxisAlignment(osgText::TextBase::SCREEN);
        text->setColor(osg::Vec4(0.95f, 0.95f, 0.95f, 1.f));
        text->setBackdropType(osgText::Text::OUTLINE);
        text->setBackdropColor(osg::Vec4(0.05f, 0.05f, 0.05f, 0.95f));
        return text;
    }

    void createWindow()
    {
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        mWindow = SDL_CreateWindow("OpenFO3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, mWindowWidth,
            mWindowHeight, flags);
        if (mWindow == nullptr)
            throw std::runtime_error("Failed to create SDL window: " + std::string(SDL_GetError()));

        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        SDL_GetWindowPosition(mWindow, &traits->x, &traits->y);
        SDL_GL_GetDrawableSize(mWindow, &traits->width, &traits->height);
        traits->windowName = "OpenFO3";
        traits->windowDecoration = true;
        traits->screenNum = SDL_GetWindowDisplayIndex(mWindow);
        traits->vsync = 0;
        traits->inheritedWindowData = new SDLUtil::GraphicsWindowSDL2::WindowData(mWindow);

        osg::ref_ptr<SDLUtil::GraphicsWindowSDL2> graphicsWindow
            = new SDLUtil::GraphicsWindowSDL2(traits, SDLUtil::VSyncMode::Disabled);
        if (!graphicsWindow->valid())
            throw std::runtime_error("Failed to create OSG graphics context");

        osg::ref_ptr<osg::Camera> camera = mViewer->getCamera();
        camera->setGraphicsContext(graphicsWindow);
        camera->setViewport(0, 0, traits->width, traits->height);
        camera->setClearColor(osg::Vec4(0.55f, 0.68f, 0.84f, 1.f));
        camera->setProjectionMatrixAsPerspective(75.f, static_cast<double>(traits->width) / traits->height,
            sNearClip, sFarClip);
        camera->setComputeNearFarMode(osg::Camera::DO_NOT_COMPUTE_NEAR_FAR);
        camera->setCullMask(~sHiddenNodeMask);

        osg::ref_ptr<SceneUtil::OperationSequence> realizeOperations = new SceneUtil::OperationSequence(false);
        realizeOperations->add(new SceneUtil::GetGLExtensionsOperation());
        mViewer->setRealizeOperation(realizeOperations);

        updateOverlayLayout(traits->width, traits->height);
        mViewer->realize();

        osg::GraphicsContext* const graphicsContext = camera->getGraphicsContext();
        if (graphicsContext != nullptr && !SceneUtil::glExtensionsReady())
        {
            graphicsContext->makeCurrent();
            SceneUtil::GetGLExtensionsOperation registerExtensions;
            registerExtensions(graphicsContext);
            graphicsContext->releaseContext();
        }
        Log(Debug::Info) << "OpenFO3 GL extensions ready=" << (SceneUtil::glExtensionsReady() ? "1" : "0");
    }

    void applyMouseMode(bool relative)
    {
        SDL_ShowCursor(relative ? SDL_DISABLE : SDL_ENABLE);
        SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);
        mMouseLookActive = relative;
    }

    void initializeResources()
    {
        mVFS = std::make_unique<VFS::Manager>();
        VFS::registerArchives(mVFS.get(), mFileCollections, mArchives, true, &mEncoder->getStatelessEncoder());

        mResourceSystem = std::make_unique<Resource::ResourceSystem>(
            mVFS.get(), 5.0, &mEncoder->getStatelessEncoder());
        Nif::Reader::setLoadUnsupportedFiles(true);
        Resource::SceneManager* sceneManager = mResourceSystem->getSceneManager();
        sceneManager->setShaderPath(mResDir / "shaders");
        sceneManager->setUnRefImageDataAfterApply(false);
        sceneManager->setFilterSettings("linear", "linear", "nearest", 4.0f);
        NifOsg::Loader::setHiddenNodeMask(sHiddenNodeMask);

        if (mLightRoot != nullptr)
        {
            sceneManager->setLightingMethod(mLightRoot->getLightingMethod());
            sceneManager->setSupportedLightingMethods(mLightRoot->getSupportedLightingMethods());

            Shader::ShaderManager::DefineMap globalDefines = Shader::getDefaultDefines();
            Shader::ShaderManager::DefineMap shadowDefines = SceneUtil::ShadowManager::getShadowsDisabledDefines();
            Shader::ShaderManager::DefineMap lightDefines = mLightRoot->getLightDefines();
            for (const auto& [key, value] : shadowDefines)
                globalDefines[key] = value;
            for (const auto& [key, value] : lightDefines)
                globalDefines[key] = value;
            sceneManager->getShaderManager().setGlobalDefines(globalDefines);
            Log(Debug::Info) << "OpenFO3 shader bootstrap: lightingMethod="
                             << static_cast<int>(mLightRoot->getLightingMethod()) << " useUBO="
                             << globalDefines["useUBO"] << " shadows_enabled=" << globalDefines["shadows_enabled"];

            mStateUpdater = new SceneUtil::StateUpdater;
            mStateUpdater->setAmbientColor(osg::Vec4f(0.35f, 0.35f, 0.35f, 1.f));
            mStateUpdater->setFogColor(osg::Vec4f(0.55f, 0.68f, 0.84f, 1.f));
            mStateUpdater->setFogStart(sFarClip * 0.2f);
            mStateUpdater->setFogEnd(sFarClip);
            mLightRoot->addUpdateCallback(mStateUpdater);

            mSharedUniformStateUpdater = new SceneUtil::SharedUniformStateUpdater(0.f);
            mSharedUniformStateUpdater->setNear(sNearClip);
            mSharedUniformStateUpdater->setFar(sFarClip);
            int drawableWidth = 0;
            int drawableHeight = 0;
            SDL_GL_GetDrawableSize(mWindow, &drawableWidth, &drawableHeight);
            mSharedUniformStateUpdater->setScreenRes(
                static_cast<float>(std::max(1, drawableWidth)), static_cast<float>(std::max(1, drawableHeight)));
            mSceneRoot->addUpdateCallback(mSharedUniformStateUpdater);

            mPerViewUniformStateUpdater = new SceneUtil::PerViewUniformStateUpdater(sceneManager);
            mPerViewUniformStateUpdater->setProjectionMatrix(mViewer->getCamera()->getProjectionMatrix());
            mWorldRoot->addCullCallback(mPerViewUniformStateUpdater);

            osg::StateSet* const rootState = mLightRoot->getOrCreateStateSet();
            rootState->setMode(GL_CULL_FACE, osg::StateAttribute::ON);
            rootState->setMode(GL_NORMALIZE, osg::StateAttribute::ON);
            rootState->setAttribute(Shader::RemovedAlphaFunc::getInstance(GL_ALWAYS));
            rootState->setMode(GL_ALPHA_TEST, osg::StateAttribute::OFF);
            rootState->setMode(
                GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::PROTECTED | osg::StateAttribute::OVERRIDE);
            osg::ref_ptr<osg::Material> defaultMat = new osg::Material;
            defaultMat->setColorMode(osg::Material::OFF);
            defaultMat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4f(1.f, 1.f, 1.f, 1.f));
            defaultMat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4f(1.f, 1.f, 1.f, 1.f));
            defaultMat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4f(0.f, 0.f, 0.f, 0.f));
            rootState->setAttribute(defaultMat);
            rootState->addUniform(new osg::Uniform("emissiveMult", 1.f));
            rootState->addUniform(new osg::Uniform("specStrength", 1.f));
            rootState->addUniform(new osg::Uniform("distortionStrength", 0.f));
            sceneManager->setUpNormalsRTForStateSet(rootState, true);
        }
        else
            Log(Debug::Warning) << "OpenFO3 shader bootstrap skipped because no light root is available";

        mBulletShapeManager = std::make_unique<Resource::BulletShapeManager>(
            mVFS.get(), sceneManager, mResourceSystem->getNifFileManager(), 5.0);
    }

    void loadContent()
    {
        mContent = std::make_unique<LoadedContent>();
        mContent->load(mFileCollections, mContentFiles, mVFS.get(), &mEncoder->getStatelessEncoder());
    }

    void createExteriorSceneSystems()
    {
        mTerrainStorage = std::make_unique<Fo3TerrainStorage>(mVFS.get(), *mContent, mWorldspace);
        if (mUseDistantTerrain)
        {
            const float lodFactor = Settings::terrain().mLodFactor;
            const int compMapResolution = Settings::terrain().mCompositeMapResolution;
            const float compMapLevel = static_cast<float>(std::pow(2.f, Settings::terrain().mCompositeMapLevel.get()));
            const int vertexLodMod = Settings::terrain().mVertexLodMod;
            const float maxCompGeometrySize = Settings::terrain().mMaxCompositeGeometrySize;
            const bool debugChunks = Settings::terrain().mDebugChunks;
            mTerrain = std::make_unique<Terrain::QuadTreeWorld>(mWorldRoot.get(), mSceneRoot.get(), mResourceSystem.get(),
                mTerrainStorage.get(), sTerrainNodeMask, ~0u, 0u, compMapResolution, compMapLevel, lodFactor,
                vertexLodMod, maxCompGeometrySize, debugChunks, mWorldspace, 5.0);
        }
        else
        {
            mTerrain = std::make_unique<Terrain::TerrainGrid>(mWorldRoot.get(), mSceneRoot.get(), mResourceSystem.get(),
                mTerrainStorage.get(), sTerrainNodeMask, mWorldspace, 5.0);
        }

        mTerrain->setViewDistance(mViewDistance);
        mTerrain->enableHeightCullCallback(Settings::terrain().mWaterCulling);
        mBucketSceneStats.clear();
        mLoadedTerrainCells.clear();
        mLoadedObjectBuckets.clear();
        mObjectGridCenterInitialized = false;
        mTerrainGridCenterInitialized = false;
        if (mCollisionScene == nullptr)
            mCollisionScene = std::make_unique<CollisionScene>();
    }

    void buildWorld()
    {
        const std::optional<BootstrapCell> bootstrap = mContent->chooseBootstrapCell();
        if (!bootstrap.has_value())
            throw std::runtime_error("OpenFO3 could not find an exterior Fallout 3 cell with terrain data");

        mActiveCell = bootstrap->mId;
        mWorldspace = bootstrap->mCell->mParent;
        Log(Debug::Info) << "OpenFO3 bootstrap cell " << mActiveCell.serializeText() << " worldspace "
                         << mWorldspace.serializeText() << " coords=(" << bootstrap->mCell->mX << ", "
                         << bootstrap->mCell->mY << ")";

        addSunLight();

        mInteractionHalfGridSize = 1;
        const bool requestedDistantTerrain = Settings::terrain().mDistantTerrain;
        mUseDistantTerrain = false;
        const float cellSize = static_cast<float>(ESM::getCellSize(mWorldspace));
        mViewDistance = std::max(Settings::camera().mViewingDistance.get(), cellSize);
        const int requestedTerrainHalfGridSize = std::max(Constants::ESM4CellGridRadius,
            static_cast<int>(std::ceil(mViewDistance / cellSize)));
        const int requestedLocalTerrainHalfGridSize = std::clamp(requestedTerrainHalfGridSize,
            Constants::ESM4CellGridRadius, sStableTerrainHalfGridCap);
        mTerrainHalfGridSize = requestedLocalTerrainHalfGridSize;
        mFarVisualHalfGridSize = mTerrainHalfGridSize;
        mNearFullHalfGridSize = std::min(sStableNearFullHalfGridCap, mTerrainHalfGridSize);
        mLandmarkHalfGridSize = std::clamp(requestedTerrainHalfGridSize, mFarVisualHalfGridSize,
            sStableLandmarkHalfGridCap);
        if (requestedDistantTerrain)
            Log(Debug::Info) << "OpenFO3 forcing distant terrain off for prototype stability";
        Log(Debug::Info) << "OpenFO3 streaming config: viewingDistance=" << mViewDistance
                         << " requestedTerrainHalfGridSize=" << requestedTerrainHalfGridSize
                         << " requestedLocalTerrainHalfGridSize=" << requestedLocalTerrainHalfGridSize
                         << " terrainHalfGridSize=" << mTerrainHalfGridSize
                         << " farVisualHalfGridSize=" << mFarVisualHalfGridSize
                         << " nearFullHalfGridSize=" << mNearFullHalfGridSize
                         << " interactionHalfGridSize=" << mInteractionHalfGridSize
                         << " landmarkHalfGridSize=" << mLandmarkHalfGridSize
                         << " requestedDistantTerrain=" << (requestedDistantTerrain ? 1 : 0)
                         << " distantTerrain=" << (mUseDistantTerrain ? 1 : 0);

        mCollisionScene = std::make_unique<CollisionScene>();
        createExteriorSceneSystems();

        const int centerX = bootstrap->mCell->mX;
        const int centerY = bootstrap->mCell->mY;
        updateActiveExteriorGrid(osg::Vec2i(centerX, centerY), true);
        mSceneMode = SceneMode::Exterior;
        initializeBootstrapCamera(centerX, centerY, cellSize);
        updateCameraMatrix();
        updateStatusText();
    }

    osg::Vec4i terrainGridCenterToBounds(const osg::Vec2i& centerCell) const
    {
        return osg::Vec4i(centerCell.x() - mTerrainHalfGridSize, centerCell.y() - mTerrainHalfGridSize,
            centerCell.x() + mTerrainHalfGridSize + 1, centerCell.y() + mTerrainHalfGridSize + 1);
    }

    std::optional<osg::Vec2i> findNearestLandCell(const osg::Vec2i& centerCell, int maxRadius) const
    {
        if (mContent->hasLand(mWorldspace, centerCell.x(), centerCell.y()))
            return centerCell;

        for (int radius = 1; radius <= maxRadius; ++radius)
        {
            std::optional<osg::Vec2i> best;
            int bestDistanceSquared = std::numeric_limits<int>::max();
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dy)) != radius)
                        continue;

                    const osg::Vec2i candidate(centerCell.x() + dx, centerCell.y() + dy);
                    if (!mContent->hasLand(mWorldspace, candidate.x(), candidate.y()))
                        continue;

                    const int distanceSquared = dx * dx + dy * dy;
                    if (!best.has_value() || distanceSquared < bestDistanceSquared)
                    {
                        best = candidate;
                        bestDistanceSquared = distanceSquared;
                    }
                }
            }

            if (best.has_value())
                return best;
        }

        return std::nullopt;
    }

    osg::Vec2i resolveTerrainGridCenter(const osg::Vec2i& objectCenter) const
    {
        if (const std::optional<osg::Vec2i> nearestLand = findNearestLandCell(objectCenter, mTerrainHalfGridSize);
            nearestLand.has_value())
        {
            return *nearestLand;
        }

        if (mTerrainGridCenterInitialized)
            return mCurrentTerrainGridCenter;

        return objectCenter;
    }

    osg::Vec2i getNewObjectGridCenterForCamera(const osg::Vec3f& position) const
    {
        if (mObjectGridCenterInitialized)
        {
            const osg::Vec2f center = ESM::indexToPosition(
                ESM::ExteriorCellLocation(mCurrentObjectGridCenter.x(), mCurrentObjectGridCenter.y(), mWorldspace), true);
            const float distance = std::max(std::abs(center.x() - position.x()), std::abs(center.y() - position.y()));
            const float maxDistance = static_cast<float>(ESM::getCellSize(mWorldspace)) * 0.5f + sCellLoadingThreshold;
            if (distance <= maxDistance)
                return mCurrentObjectGridCenter;
        }

        const ESM::ExteriorCellLocation cellPos
            = ESM::positionToExteriorCellLocation(position.x(), position.y(), mWorldspace);
        return osg::Vec2i(cellPos.mX, cellPos.mY);
    }

    void unloadOwnedSceneObjects(const std::vector<SceneObjectMetadata*>& sceneObjects)
    {
        if (sceneObjects.empty())
            return;

        std::vector<ESM::FormId> ownedReferenceIds;
        ownedReferenceIds.reserve(sceneObjects.size());
        for (SceneObjectMetadata* metadata : sceneObjects)
        {
            if (metadata != nullptr)
                ownedReferenceIds.push_back(metadata->mReferenceId);
        }

        for (ESM::FormId referenceId : ownedReferenceIds)
        {
            SceneObjectMetadata* metadata = findObjectByReferenceId(referenceId);
            if (metadata != nullptr)
                destroySceneObject(*metadata);
        }
    }

    void unloadObjectBucket(const ExteriorBucketKey& bucketKey)
    {
        if (const auto bucketIt = mLoadedObjectBuckets.find(bucketKey); bucketIt != mLoadedObjectBuckets.end())
        {
            unloadOwnedSceneObjects(bucketIt->second.mSceneObjects);
        }

        mLoadedObjectBuckets.erase(bucketKey);

        if (const auto statsIt = mBucketSceneStats.find(bucketKey); statsIt != mBucketSceneStats.end())
        {
            if (statsIt->second.mRoot != nullptr && statsIt->second.mRoot->getNumParents() > 0)
                statsIt->second.mRoot->getParent(0)->removeChild(statsIt->second.mRoot);
            mBucketSceneStats.erase(statsIt);
        }
    }

    void unloadInteriorScene()
    {
        if (!mLoadedInteriorScene.mLoaded)
            return;

        unloadOwnedSceneObjects(mLoadedInteriorScene.mSceneObjects);
        if (mLoadedInteriorScene.mRoot != nullptr && mLoadedInteriorScene.mRoot->getNumParents() > 0)
            mLoadedInteriorScene.mRoot->getParent(0)->removeChild(mLoadedInteriorScene.mRoot);
        mLoadedInteriorScene = LoadedInteriorScene{};
    }

    void clearExteriorScene()
    {
        while (!mLoadedObjectBuckets.empty())
        {
            const ExteriorBucketKey bucketKey = mLoadedObjectBuckets.begin()->first;
            unloadObjectBucket(bucketKey);
        }

        if (mTerrain != nullptr)
        {
            for (const auto& [x, y] : mLoadedTerrainCells)
                mTerrain->unloadCell(x, y);
        }

        mLoadedTerrainCells.clear();
        mBucketSceneStats.clear();
        mTerrain.reset();
        mTerrainStorage.reset();
        mObjectGridCenterInitialized = false;
        mTerrainGridCenterInitialized = false;
    }

    void clearActiveScene()
    {
        closePanel();
        unloadInteriorScene();
        clearExteriorScene();
        mObjectByReferenceId.clear();
        mFocusableObjects.clear();
        mCollisionScene = std::make_unique<CollisionScene>();
        resetGroundSupportHistory();
    }

    void updateActiveExteriorGrid(const osg::Vec2i& centerCell, bool bootstrap)
    {
        const auto updateStart = std::chrono::steady_clock::now();
        SceneMutationStats mutationStats;
        const osg::Vec2i terrainCenter = resolveTerrainGridCenter(centerCell);
        if (mObjectGridCenterInitialized && centerCell == mCurrentObjectGridCenter && mTerrainGridCenterInitialized
            && terrainCenter == mCurrentTerrainGridCenter)
            return;

        mFocusedObject = nullptr;

        const auto withinTerrainGrid = [&](const std::pair<int, int>& cell) {
            return std::abs(cell.first - terrainCenter.x()) <= mTerrainHalfGridSize
                && std::abs(cell.second - terrainCenter.y()) <= mTerrainHalfGridSize;
        };
        const auto withinLandmarkGrid = [&](const ExteriorBucketKey& key) {
            return std::abs(bucketX(key) - centerCell.x()) <= mLandmarkHalfGridSize
                && std::abs(bucketY(key) - centerCell.y()) <= mLandmarkHalfGridSize;
        };

        if (mTerrain != nullptr)
            mTerrain->setActiveGrid(terrainGridCenterToBounds(terrainCenter));

        int unloadedTerrainCells = 0;
        for (auto it = mLoadedTerrainCells.begin(); it != mLoadedTerrainCells.end();)
        {
            if (withinTerrainGrid(*it))
            {
                ++it;
                continue;
            }

            if (mTerrain != nullptr)
                mTerrain->unloadCell(it->first, it->second);
            it = mLoadedTerrainCells.erase(it);
            ++unloadedTerrainCells;
        }

        int unloadedObjectBuckets = 0;
        for (auto it = mLoadedObjectBuckets.begin(); it != mLoadedObjectBuckets.end();)
        {
            if (withinLandmarkGrid(it->first))
            {
                ++it;
                continue;
            }

            const ExteriorBucketKey bucketKey = it->first;
            ++it;
            unloadObjectBucket(bucketKey);
            ++unloadedObjectBuckets;
        }

        int loadedTerrainCells = 0;
        for (int dy = -mTerrainHalfGridSize; dy <= mTerrainHalfGridSize; ++dy)
        {
            for (int dx = -mTerrainHalfGridSize; dx <= mTerrainHalfGridSize; ++dx)
            {
                const std::pair<int, int> coord(terrainCenter.x() + dx, terrainCenter.y() + dy);
                if (mContent->hasLand(mWorldspace, coord.first, coord.second)
                    && !mLoadedTerrainCells.contains(coord))
                {
                    mTerrain->loadCell(coord.first, coord.second);
                    mLoadedTerrainCells.insert(coord);
                    ++loadedTerrainCells;
                }
            }
        }

        int loadedObjectBuckets = 0;
        for (int dy = -mLandmarkHalfGridSize; dy <= mLandmarkHalfGridSize; ++dy)
        {
            for (int dx = -mLandmarkHalfGridSize; dx <= mLandmarkHalfGridSize; ++dx)
            {
                const ExteriorBucketKey bucketKey = makeExteriorBucketKey(mWorldspace, centerCell.x() + dx, centerCell.y() + dy);
                const auto* refs = mContent->getExteriorSpatialReferences(bucketKey);
                if (refs == nullptr || refs->empty())
                    continue;

                const int bucketDistance = std::max(std::abs(dx), std::abs(dy));
                const BucketLoadMode requiredMode = bucketDistance <= mInteractionHalfGridSize
                    ? BucketLoadMode::Interaction
                    : (bucketDistance <= mNearFullHalfGridSize
                            ? BucketLoadMode::NearFull
                            : (bucketDistance <= mFarVisualHalfGridSize ? BucketLoadMode::FarVisual
                                                                        : BucketLoadMode::LandmarkVisual));
                const bool shouldHaveNonInteractiveCollision = (bucketDistance <= mNearFullHalfGridSize);
                if (!mLoadedObjectBuckets.contains(bucketKey))
                {
                    LoadedObjectBucket& loadedBucket = mLoadedObjectBuckets[bucketKey];
                    loadedBucket = LoadedObjectBucket{
                        .mMode = requiredMode,
                        .mHasNonInteractiveCollision = shouldHaveNonInteractiveCollision,
                    };
                    populateExteriorBucket(bucketKey, loadedBucket.mSceneObjects, requiredMode,
                        shouldHaveNonInteractiveCollision, &mutationStats);
                    ++loadedObjectBuckets;
                }
                else
                {
                    LoadedObjectBucket& loadedBucket = mLoadedObjectBuckets[bucketKey];
                    const bool modeChanged = loadedBucket.mMode != requiredMode
                        || loadedBucket.mHasNonInteractiveCollision != shouldHaveNonInteractiveCollision;
                    if (!modeChanged)
                        continue;

                    if (isModeAtLeast(requiredMode, loadedBucket.mMode))
                    {
                        populateExteriorBucket(bucketKey, loadedBucket.mSceneObjects, requiredMode,
                            shouldHaveNonInteractiveCollision, &mutationStats);
                    }
                    applyExteriorBucketMode(bucketKey, loadedBucket, requiredMode, shouldHaveNonInteractiveCollision,
                        &mutationStats);
                }
            }
        }

        mCurrentObjectGridCenter = centerCell;
        mObjectGridCenterInitialized = true;
        mCurrentTerrainGridCenter = terrainCenter;
        mTerrainGridCenterInitialized = true;
        if (const std::optional<ESM::RefId> activeCell = mContent->findExteriorCellId(mWorldspace, centerCell.x(), centerCell.y());
            activeCell.has_value())
        {
            mActiveCell = *activeCell;
        }

        if (bootstrap)
        {
            Log(Debug::Info) << "OpenFO3 loaded " << mLoadedTerrainCells.size() << " terrain cells around bootstrap cell";
            Log(Debug::Info) << "OpenFO3 populated render buckets for " << mLoadedObjectBuckets.size()
                             << " exterior cells around bootstrap cell";
        }
        else
        {
            const auto updateEnd = std::chrono::steady_clock::now();
            const double updateMs
                = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
            Log(Debug::Info) << "OpenFO3 updated active grid objectCenter=(" << centerCell.x() << ", " << centerCell.y()
                             << ") terrainCenter=(" << terrainCenter.x() << ", " << terrainCenter.y()
                             << ") terrainRadius=" << mTerrainHalfGridSize
                             << " landmarkRadius=" << mLandmarkHalfGridSize
                             << " farVisualRadius=" << mFarVisualHalfGridSize
                             << " nearFullRadius=" << mNearFullHalfGridSize
                             << " interactionRadius=" << mInteractionHalfGridSize
                             << " loadedTerrain=" << loadedTerrainCells
                             << " unloadedTerrain=" << unloadedTerrainCells << " loadedObjectBuckets="
                             << loadedObjectBuckets << " unloadedObjectBuckets=" << unloadedObjectBuckets
                             << " modeOnlyBuckets=" << mutationStats.mModeOnlyBuckets
                             << " refsInstantiated=" << mutationStats.mRefsInstantiated
                             << " collisionsAdded=" << mutationStats.mCollisionsAdded
                             << " collisionsRemoved=" << mutationStats.mCollisionsRemoved
                             << " updateMs=" << updateMs;
        }
    }

    void initializeBootstrapCamera(int centerX, int centerY, float cellSize)
    {
        const osg::Vec3f cellCenter((centerX + 0.5f) * cellSize, (centerY + 0.5f) * cellSize, 0.f);
        const float centerHeight = terrainHeightAt(cellCenter.x(), cellCenter.y());
        mCameraPosition = osg::Vec3f(cellCenter.x(), cellCenter.y(), centerHeight + sEyeOffset + sBootstrapHoverHeight);

        const auto clampTerrainHeight = [](float sample, float fallback) {
            if (!std::isfinite(sample))
                return fallback;
            if (!std::isfinite(fallback))
                return sample;
            if (std::abs(sample - fallback) > 2048.f)
                return fallback;
            return sample;
        };

        if (const std::optional<InitialViewTarget> target = chooseInitialViewTarget(cellCenter); target.has_value())
        {
            const osg::Vec3f referencePos
                = target->mMetadata->mRenderable ? target->mMetadata->mBoundCenter : target->mMetadata->mTransform.asVec3();
            const osg::BoundingSphere bound
                = target->mMetadata->mNode != nullptr ? target->mMetadata->mNode->getBound() : osg::BoundingSphere();

            bool useBoundCenter = false;
            float targetRadius = 96.f;
            osg::Vec3f aimTarget = target->mHasCluster ? target->mClusterCenter : referencePos;
            if (bound.valid() && std::isfinite(bound.radius()) && bound.radius() > 0.f)
            {
                const osg::Vec3f boundCenter = bound.center();
                if (std::isfinite(boundCenter.x()) && std::isfinite(boundCenter.y()) && std::isfinite(boundCenter.z()))
                {
                    const float centerDelta = (boundCenter - referencePos).length();
                    const float centerTolerance = std::max(bound.radius() * 2.f, 512.f);
                    if (!target->mHasCluster && centerDelta <= centerTolerance)
                    {
                        useBoundCenter = true;
                        aimTarget = boundCenter;
                        targetRadius = bound.radius();
                    }
                }
            }

            osg::Vec3f offset = referencePos - cellCenter;
            offset.z() = 0.f;
            if (offset.length2() < 1.f)
                offset.set(1.f, 1.f, 0.f);
            offset.normalize();

            const float backoff = std::clamp(std::max(targetRadius * 2.5f, 240.f), 240.f, 900.f);
            const float targetGround = clampTerrainHeight(terrainHeightAt(referencePos.x(), referencePos.y()), referencePos.z());
            mCameraPosition = referencePos - offset * backoff;
            mCameraPosition.z() = std::max(targetGround + std::max(targetRadius * 1.2f, 180.f),
                referencePos.z() + std::max(targetRadius * 0.6f, 96.f));

            const float cameraGround = clampTerrainHeight(terrainHeightAt(mCameraPosition.x(), mCameraPosition.y()), targetGround);
            mCameraPosition.z() = std::max(mCameraPosition.z(), cameraGround + sEyeOffset + sBootstrapHoverHeight);
            orientCameraTowards(aimTarget + osg::Vec3f(0.f, 0.f, std::max(targetRadius * 0.25f, sBootstrapTargetLift)));
            Log(Debug::Info) << "OpenFO3 initial camera target " << target->mMetadata->mReferenceId << " kind="
                             << toString(target->mMetadata->mKind) << " name=\"" << target->mMetadata->mName << "\""
                             << " ref=(" << referencePos.x() << ", " << referencePos.y() << ", " << referencePos.z() << ")"
                             << " aim=(" << aimTarget.x() << ", " << aimTarget.y() << ", " << aimTarget.z() << ")"
                             << " clusterSize=" << target->mClusterSize
                             << " useBoundCenter=" << (useBoundCenter ? "1" : "0")
                             << " camera=(" << mCameraPosition.x() << ", " << mCameraPosition.y() << ", "
                             << mCameraPosition.z() << ")";
        }
        else if (const std::optional<osg::BoundingSphere> sceneBounds = computeLoadedSceneBounds(); sceneBounds.has_value())
        {
            const osg::Vec3f sceneCenter = sceneBounds->center();
            const float viewDistance = std::clamp(sceneBounds->radius() * 1.15f, 900.f, 8000.f);
            mCameraPosition = sceneCenter
                + osg::Vec3f(-viewDistance * 0.85f, -viewDistance * 0.55f, viewDistance * 0.42f);
            mCameraPosition.z() = std::max(
                mCameraPosition.z(), terrainHeightAt(mCameraPosition.x(), mCameraPosition.y()) + sEyeOffset + sBootstrapHoverHeight);
            orientCameraTowards(sceneCenter + osg::Vec3f(0.f, 0.f, std::max(sceneBounds->radius() * 0.08f, sBootstrapTargetLift)));
            addBootstrapDebugMarker(sceneCenter + osg::Vec3f(0.f, 0.f, 128.f), osg::Vec4f(1.f, 0.15f, 0.15f, 1.f));
            Log(Debug::Info) << "OpenFO3 initial camera target scene bounds center=(" << sceneCenter.x() << ", "
                             << sceneCenter.y() << ", " << sceneCenter.z() << ") radius=" << sceneBounds->radius();
        }
        else
        {
            mYaw = 0.f;
            mPitch = -0.55f;
            Log(Debug::Info) << "OpenFO3 initial camera target fallback at cell center";
        }
    }

    std::optional<osg::BoundingSphere> computeLoadedSceneBounds() const
    {
        bool valid = false;
        osg::Vec3f minCorner(0.f, 0.f, 0.f);
        osg::Vec3f maxCorner(0.f, 0.f, 0.f);

        for (const auto& [bucketKey, stats] : mBucketSceneStats)
        {
            (void)bucketKey;
            if (stats.mRenderableCount == 0 || !stats.mHasBounds || stats.mSuspiciousBounds)
                continue;

            const osg::Vec3f center = stats.mBounds.center();
            if (!std::isfinite(center.x()) || !std::isfinite(center.y()) || !std::isfinite(center.z()))
                continue;

            const osg::Vec3f extent(stats.mBounds.radius(), stats.mBounds.radius(), stats.mBounds.radius());
            if (!valid)
            {
                minCorner = center - extent;
                maxCorner = center + extent;
                valid = true;
                continue;
            }

            minCorner.x() = std::min(minCorner.x(), center.x() - extent.x());
            minCorner.y() = std::min(minCorner.y(), center.y() - extent.y());
            minCorner.z() = std::min(minCorner.z(), center.z() - extent.z());
            maxCorner.x() = std::max(maxCorner.x(), center.x() + extent.x());
            maxCorner.y() = std::max(maxCorner.y(), center.y() + extent.y());
            maxCorner.z() = std::max(maxCorner.z(), center.z() + extent.z());
        }

        if (!valid)
            return std::nullopt;

        const osg::Vec3f center = (minCorner + maxCorner) * 0.5f;
        const float radius = std::max((maxCorner - center).length(), 1.f);
        return osg::BoundingSphere(center, radius);
    }

    std::optional<InitialViewTarget> chooseInitialViewTarget(const osg::Vec3f& origin) const
    {
        struct ClusterBucket
        {
            osg::Vec3f mSum = osg::Vec3f(0.f, 0.f, 0.f);
            int mRenderableCount = 0;
            int mInteractiveCount = 0;
            int mLargeRenderableCount = 0;
            float mLargestRadius = 0.f;
        };

        constexpr float clusterSize = 1536.f;
        constexpr int minRenderableRefs = 12;
        constexpr float meaningfulRadius = 128.f;

        std::map<std::pair<int, int>, ClusterBucket> buckets;

        for (const auto& metadata : mObjects)
        {
            if (metadata == nullptr || metadata->mPickedUp || metadata->mNode == nullptr || !metadata->mRenderable
                || metadata->mEffectOnly)
                continue;
            const auto loadedBucketIt = mLoadedObjectBuckets.find(metadata->mBucketKey);
            if (loadedBucketIt == mLoadedObjectBuckets.end() || isVisualOnlyBucketMode(loadedBucketIt->second.mMode))
                continue;
            const auto cellStatsIt = mBucketSceneStats.find(metadata->mBucketKey);
            if (cellStatsIt != mBucketSceneStats.end() && cellStatsIt->second.mSuspiciousBounds)
                continue;

            const osg::Vec3f targetPos = metadata->mRenderable ? metadata->mBoundCenter : metadata->mTransform.asVec3();
            if (!std::isfinite(targetPos.x()) || !std::isfinite(targetPos.y()) || !std::isfinite(targetPos.z()))
                continue;

            const std::pair<int, int> key(
                static_cast<int>(std::floor(targetPos.x() / clusterSize)),
                static_cast<int>(std::floor(targetPos.y() / clusterSize)));
            ClusterBucket& bucket = buckets[key];
            bucket.mSum += targetPos;
            ++bucket.mRenderableCount;
            if (isInteractiveKind(metadata->mKind))
                ++bucket.mInteractiveCount;
            if (metadata->mBoundRadius >= meaningfulRadius)
            {
                ++bucket.mLargeRenderableCount;
                bucket.mLargestRadius = std::max(bucket.mLargestRadius, metadata->mBoundRadius);
            }
        }

        if (buckets.empty())
            return std::nullopt;

        std::pair<int, int> bestBucketKey{};
        osg::Vec3f bestBucketCenter(0.f, 0.f, 0.f);
        int bestBucketCount = 0;
        float bestBucketScore = -std::numeric_limits<float>::infinity();

        for (const auto& [key, bucket] : buckets)
        {
            if (bucket.mRenderableCount < minRenderableRefs || bucket.mLargeRenderableCount == 0)
                continue;

            const osg::Vec3f center = bucket.mSum / static_cast<float>(bucket.mRenderableCount);
            const float distance = (center - origin).length();
            const float score = static_cast<float>(bucket.mRenderableCount) * 1000.f
                + bucket.mLargestRadius * 12.f + static_cast<float>(bucket.mInteractiveCount) * 80.f
                - distance * 0.2f;
            if (score > bestBucketScore)
            {
                bestBucketScore = score;
                bestBucketKey = key;
                bestBucketCenter = center;
                bestBucketCount = bucket.mRenderableCount;
            }
        }

        if (bestBucketCount == 0)
            return std::nullopt;

        const SceneObjectMetadata* best = nullptr;
        float bestScore = -std::numeric_limits<float>::infinity();

        for (const auto& metadata : mObjects)
        {
            if (metadata == nullptr || metadata->mPickedUp || metadata->mNode == nullptr || !metadata->mRenderable
                || metadata->mEffectOnly)
                continue;
            const auto loadedBucketIt = mLoadedObjectBuckets.find(metadata->mBucketKey);
            if (loadedBucketIt == mLoadedObjectBuckets.end() || isVisualOnlyBucketMode(loadedBucketIt->second.mMode))
                continue;
            const auto cellStatsIt = mBucketSceneStats.find(metadata->mBucketKey);
            if (cellStatsIt != mBucketSceneStats.end() && cellStatsIt->second.mSuspiciousBounds)
                continue;

            const osg::Vec3f targetPos = metadata->mRenderable ? metadata->mBoundCenter : metadata->mTransform.asVec3();
            const std::pair<int, int> key(
                static_cast<int>(std::floor(targetPos.x() / clusterSize)),
                static_cast<int>(std::floor(targetPos.y() / clusterSize)));
            if (key != bestBucketKey)
                continue;

            float score = metadata->mBoundRadius * 12.f - (targetPos - bestBucketCenter).length() * 0.15f;
            if (!isInteractiveKind(metadata->mKind))
                score += 400.f;
            else
                score += static_cast<float>(initialViewScoreForKind(metadata->mKind));
            if (metadata->mBoundRadius >= meaningfulRadius)
                score += 600.f;

            if (score > bestScore)
            {
                bestScore = score;
                best = metadata.get();
            }
        }

        if (best == nullptr)
            return std::nullopt;

        return InitialViewTarget{
            .mMetadata = best,
            .mClusterCenter = bestBucketCenter,
            .mClusterSize = bestBucketCount,
            .mHasCluster = true,
        };
    }

    void orientCameraTowards(const osg::Vec3f& target)
    {
        const osg::Vec3f delta = target - mCameraPosition;
        const float horizontal = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (horizontal <= 1e-4f && std::abs(delta.z()) <= 1e-4f)
            return;

        mYaw = std::atan2(delta.x(), delta.y());
        mPitch = std::clamp(std::atan2(delta.z(), std::max(horizontal, 1e-4f)), -1.2f, 0.35f);
    }

    void addSunLight()
    {
        osg::ref_ptr<osg::Light> light = new osg::Light;
        light->setLightNum(0);
        light->setPosition(osg::Vec4(-0.4f, 0.6f, 1.f, 0.f));
        light->setAmbient(osg::Vec4(0.35f, 0.35f, 0.35f, 1.f));
        light->setDiffuse(osg::Vec4(0.9f, 0.88f, 0.8f, 1.f));
        light->setSpecular(osg::Vec4(0.3f, 0.3f, 0.3f, 1.f));

        osg::ref_ptr<osg::LightSource> source = new osg::LightSource;
        source->setLight(light);
        if (mLightRoot != nullptr)
            mLightRoot->setSunlight(light);
        mSceneRoot->addChild(source);
    }

    osg::Group* ensureBucketRoot(const ExteriorBucketKey& bucketKey)
    {
        CellSceneStats& stats = mBucketSceneStats[bucketKey];
        if (stats.mRoot == nullptr)
        {
            stats.mRoot = new osg::Group;
            stats.mRoot->setName("Exterior Bucket Root " + bucketLabel(bucketKey));
            stats.mRoot->setNodeMask(sObjectNodeMask);
            mWorldRoot->addChild(stats.mRoot);
        }
        return stats.mRoot.get();
    }

    void updateBucketSceneStats(const ExteriorBucketKey& bucketKey, BucketLoadMode loadMode, std::size_t refCount,
        std::size_t instantiated, std::size_t collisionShapes, std::size_t renderableCount, std::size_t effectOnlyCount,
        std::size_t skippedMissingBase, std::size_t skippedMissingModel,
        const std::optional<osg::BoundingSphere>& populatedBounds)
    {
        auto it = mBucketSceneStats.find(bucketKey);
        if (it == mBucketSceneStats.end() || it->second.mRoot == nullptr)
            return;

        CellSceneStats& stats = it->second;
        stats.mInstantiated = instantiated;
        stats.mCollisionCount = collisionShapes;
        stats.mRenderableCount = renderableCount;
        stats.mEffectOnlyCount = effectOnlyCount;
        stats.mLoadMode = loadMode;

        stats.mHasBounds = false;
        stats.mSuspiciousBounds = false;
        osg::Vec3f center(0.f, 0.f, 0.f);
        float radius = 0.f;

        const osg::BoundingSphere bounds = populatedBounds.has_value() ? *populatedBounds : stats.mRoot->getBound();
        if (bounds.valid() && std::isfinite(bounds.radius()) && bounds.radius() > 0.f)
        {
            center = bounds.center();
            radius = bounds.radius();
            if (std::isfinite(center.x()) && std::isfinite(center.y()) && std::isfinite(center.z()))
            {
                stats.mBounds = bounds;
                stats.mHasBounds = true;
            }
        }

        std::ostringstream stream;
        stream << "OpenFO3 populated bucket " << bucketLabel(bucketKey) << " mode=" << toString(loadMode)
               << " refs=" << refCount
               << " instantiated=" << instantiated << " renderable=" << renderableCount
               << " effectOnly=" << effectOnlyCount << " collisions=" << collisionShapes
               << " skippedMissingBase=" << skippedMissingBase
               << " skippedMissingModel=" << skippedMissingModel;
        if (stats.mHasBounds)
        {
            stream << " boundsCenter=(" << center.x() << ", " << center.y() << ", " << center.z()
                   << ") radius=" << radius;
        }
        Log(Debug::Info) << stream.str();

        if (!stats.mHasBounds)
            return;

        const float cellSize = static_cast<float>(ESM::getCellSize(bucketWorldspace(bucketKey)));
        const osg::Vec3f expectedCenter(
            (bucketX(bucketKey) + 0.5f) * cellSize, (bucketY(bucketKey) + 0.5f) * cellSize, center.z());
        const float horizontalOffset = (osg::Vec2f(center.x(), center.y()) - osg::Vec2f(expectedCenter.x(), expectedCenter.y())).length();
        const float suspiciousDistance = cellSize * 1.6f;
        if (horizontalOffset > suspiciousDistance)
        {
            stats.mSuspiciousBounds = true;
            Log(Debug::Warning) << "OpenFO3 suspicious bounds for bucket " << bucketLabel(bucketKey)
                                << " expectedCenter=(" << expectedCenter.x() << ", " << expectedCenter.y()
                                << ") boundsCenter=(" << center.x() << ", " << center.y()
                                << ") horizontalOffset=" << horizontalOffset;
        }
    }

    osg::Group* ensureInteriorRoot(const ESM::RefId& cellId)
    {
        if (mLoadedInteriorScene.mRoot == nullptr)
        {
            mLoadedInteriorScene.mRoot = new osg::Group;
            mLoadedInteriorScene.mRoot->setName("Interior Cell Root " + cellId.serializeText());
            mLoadedInteriorScene.mRoot->setNodeMask(sObjectNodeMask);
            mWorldRoot->addChild(mLoadedInteriorScene.mRoot);
        }

        return mLoadedInteriorScene.mRoot.get();
    }

    void updateInteriorSceneStats(const ESM::RefId& cellId, std::size_t refCount, std::size_t instantiated,
        std::size_t collisionShapes, std::size_t renderableCount, std::size_t effectOnlyCount,
        std::size_t skippedMissingBase, std::size_t skippedMissingModel,
        const std::optional<osg::BoundingSphere>& populatedBounds)
    {
        if (!mLoadedInteriorScene.mLoaded)
            return;

        mLoadedInteriorScene.mRefCount = refCount;
        mLoadedInteriorScene.mInstantiated = instantiated;
        mLoadedInteriorScene.mCollisionCount = collisionShapes;
        mLoadedInteriorScene.mRenderableCount = renderableCount;
        mLoadedInteriorScene.mEffectOnlyCount = effectOnlyCount;
        mLoadedInteriorScene.mHasBounds = false;

        osg::Vec3f center(0.f, 0.f, 0.f);
        float radius = 0.f;
        const osg::BoundingSphere bounds = populatedBounds.has_value()
            ? *populatedBounds
            : (mLoadedInteriorScene.mRoot != nullptr ? mLoadedInteriorScene.mRoot->getBound() : osg::BoundingSphere());
        if (bounds.valid() && std::isfinite(bounds.radius()) && bounds.radius() > 0.f)
        {
            center = bounds.center();
            radius = bounds.radius();
            if (std::isfinite(center.x()) && std::isfinite(center.y()) && std::isfinite(center.z()))
            {
                mLoadedInteriorScene.mBounds = bounds;
                mLoadedInteriorScene.mHasBounds = true;
            }
        }

        std::ostringstream stream;
        stream << "OpenFO3 populated interior cell " << cellId.serializeText()
               << " refs=" << refCount
               << " instantiated=" << instantiated
               << " renderable=" << renderableCount
               << " effectOnly=" << effectOnlyCount
               << " collisions=" << collisionShapes
               << " skippedMissingBase=" << skippedMissingBase
               << " skippedMissingModel=" << skippedMissingModel;
        if (mLoadedInteriorScene.mHasBounds)
        {
            stream << " boundsCenter=(" << center.x() << ", " << center.y() << ", " << center.z()
                   << ") radius=" << radius;
        }
        Log(Debug::Info) << stream.str();
    }

    const NodeRenderStats& getCachedRenderStats(osg::Node& instance, std::string_view meshPath)
    {
        const std::string key(meshPath);
        auto [it, inserted] = mMeshRenderStatsCache.try_emplace(key);
        if (inserted)
            it->second = analyzeSceneNode(instance, false, true);
        else if (instance.getNodeMask() == 0u && it->second.hasRenderableGeometry())
            instance.setNodeMask(sObjectNodeMask);
        return it->second;
    }

    bool shouldObjectBeVisibleInMode(const SceneObjectMetadata& metadata, BucketLoadMode mode) const
    {
        return !metadata.mPickedUp && isModeAtLeast(mode, metadata.mMinLoadMode);
    }

    void syncExteriorSceneObjectState(
        SceneObjectMetadata& metadata, BucketLoadMode mode, bool allowNonInteractiveCollision, SceneMutationStats* stats)
    {
        const bool shouldBeVisible = shouldObjectBeVisibleInMode(metadata, mode);
        if (metadata.mNode != nullptr)
            metadata.mNode->setNodeMask(shouldBeVisible ? sObjectNodeMask : 0u);

        const bool shouldBeFocusable = shouldBeVisible && isInteractiveKind(metadata.mKind);
        setObjectFocusable(metadata, shouldBeFocusable);

        const bool shouldHaveCollision = shouldBeVisible
            && ((mode == BucketLoadMode::Interaction && isBlockingInteractiveKind(metadata.mKind))
                || (allowNonInteractiveCollision && metadata.mRenderable && !metadata.mEffectOnly
                    && !isInteractiveKind(metadata.mKind)));
        if (mCollisionScene != nullptr)
        {
            if (shouldHaveCollision && metadata.mCollisionObject == nullptr)
            {
                mCollisionScene->addStatic(*mBulletShapeManager, VFS::Path::Normalized(metadata.mModelPath),
                    metadata.mTransform, metadata.mScale, metadata);
                if (metadata.mCollisionObject != nullptr && stats != nullptr)
                    ++stats->mCollisionsAdded;
            }
            else if (!shouldHaveCollision && metadata.mCollisionObject != nullptr)
            {
                mCollisionScene->remove(metadata);
                if (stats != nullptr)
                    ++stats->mCollisionsRemoved;
            }
        }
    }

    void applyExteriorBucketMode(const ExteriorBucketKey& bucketKey, LoadedObjectBucket& bucket, BucketLoadMode mode,
        bool allowNonInteractiveCollision, SceneMutationStats* stats)
    {
        const bool modeChanged = bucket.mMode != mode || bucket.mHasNonInteractiveCollision != allowNonInteractiveCollision;
        bucket.mMode = mode;
        bucket.mHasNonInteractiveCollision = allowNonInteractiveCollision;
        if (modeChanged && stats != nullptr)
            ++stats->mModeOnlyBuckets;

        for (SceneObjectMetadata* metadata : bucket.mSceneObjects)
        {
            if (metadata == nullptr || findObjectByReferenceId(metadata->mReferenceId) != metadata)
                continue;
            syncExteriorSceneObjectState(*metadata, bucket.mMode, bucket.mHasNonInteractiveCollision, stats);
        }
    }

    static std::vector<std::string> splitPanelLines(const std::string& text)
    {
        std::string normalized = text;
        Misc::StringUtils::replaceAll(normalized, "\r\n", "\n");
        Misc::StringUtils::replaceAll(normalized, "\r", "\n");
        std::vector<std::string> lines;
        std::stringstream stream(normalized);
        std::string line;
        while (std::getline(stream, line))
            lines.push_back(line);
        if (lines.empty())
            lines.push_back("");
        return lines;
    }

    InventoryStack buildInventoryStackForBase(
        ESM::FormId baseId, int count, std::optional<ObjectKind> overrideKind = std::nullopt,
        std::string_view overrideName = {}) const
    {
        InventoryStack stack;
        stack.mBaseId = baseId;
        stack.mCount = std::max(count, 0);

        if (const BaseRecordData* base = mContent->findBase(baseId); base != nullptr)
        {
            stack.mKind = overrideKind.value_or(base->mKind);
            if (!overrideName.empty())
                stack.mName = std::string(overrideName);
            else if (!base->mFullName.empty())
                stack.mName = base->mFullName;
            else if (!base->mEditorId.empty())
                stack.mName = base->mEditorId;
            else
                stack.mName = std::string(toString(stack.mKind));
            stack.mEditorId = base->mEditorId;
            stack.mText = base->mText;
            stack.mIcon = base->mIcon;
            stack.mCategory = base->mCategory.empty() ? std::string(toString(stack.mKind)) : base->mCategory;
            stack.mValue = base->mValue;
            stack.mWeight = base->mWeight;
            stack.mReadable = base->mReadable || isReadableKind(stack.mKind);
            stack.mNoTake = base->mNoTake;
        }
        else
        {
            stack.mKind = overrideKind.value_or(ObjectKind::Misc);
            stack.mName = overrideName.empty() ? ESM::RefId::formIdRefId(baseId).serializeText() : std::string(overrideName);
            stack.mCategory = std::string(toString(stack.mKind));
        }
        return stack;
    }

    InventoryStack buildInventoryStackForWorldObject(const SceneObjectMetadata& metadata, int count) const
    {
        InventoryStack stack = buildInventoryStackForBase(metadata.mBaseId, count, metadata.mKind, metadata.mName);
        if (!metadata.mEditorId.empty())
            stack.mEditorId = metadata.mEditorId;
        if (!metadata.mText.empty())
            stack.mText = metadata.mText;
        if (!metadata.mIcon.empty())
            stack.mIcon = metadata.mIcon;
        if (!metadata.mCategory.empty())
            stack.mCategory = metadata.mCategory;
        stack.mValue = metadata.mValue;
        stack.mWeight = metadata.mWeight;
        stack.mReadable = metadata.mReadable || stack.mReadable;
        stack.mNoTake = metadata.mNoTake;
        return stack;
    }

    std::optional<InventoryStack> resolveContainerInventoryStack(ESM::FormId itemId, int count, int depth = 0) const
    {
        constexpr int sMaxInventoryResolutionDepth = 8;
        if (count <= 0 || itemId.isZeroOrUnset() || depth > sMaxInventoryResolutionDepth)
            return std::nullopt;

        if (mContent->findBase(itemId) != nullptr)
            return buildInventoryStackForBase(itemId, count);

        if (const ESM4::LevelledItem* levelled = mContent->findLevelledItem(itemId); levelled != nullptr)
        {
            if (levelled->useAll())
                return std::nullopt;

            const ESM4::LVLO* bestEntry = nullptr;
            for (const ESM4::LVLO& entry : levelled->mLvlObject)
            {
                const ESM::FormId entryId = ESM::FormId::fromUint32(entry.item);
                if (entryId.isZeroOrUnset())
                    continue;

                if (resolveContainerInventoryStack(entryId, 1, depth + 1).has_value()
                    && (bestEntry == nullptr || entry.level < bestEntry->level))
                {
                    bestEntry = &entry;
                }
            }

            if (bestEntry != nullptr)
            {
                const int resolvedCount = count * std::max<int>(bestEntry->count, 1);
                return resolveContainerInventoryStack(
                    ESM::FormId::fromUint32(bestEntry->item), resolvedCount, depth + 1);
            }
        }

        if (const ESM4::FormIdList* formList = mContent->findFormList(itemId); formList != nullptr)
        {
            for (ESM::FormId formId : formList->mObjects)
            {
                if (const std::optional<InventoryStack> resolved
                    = resolveContainerInventoryStack(formId, count, depth + 1);
                    resolved.has_value())
                {
                    return resolved;
                }
            }
        }

        return std::nullopt;
    }

    bool appendContainerInventoryEntry(std::vector<InventoryStack>& stacks, ESM::FormId itemId, int count, int depth = 0) const
    {
        constexpr int sMaxInventoryResolutionDepth = 8;
        if (count <= 0 || itemId.isZeroOrUnset() || depth > sMaxInventoryResolutionDepth)
            return false;

        if (mContent->findBase(itemId) != nullptr)
        {
            mergeStackIntoList(stacks, buildInventoryStackForBase(itemId, count));
            return true;
        }

        if (const ESM4::LevelledItem* levelled = mContent->findLevelledItem(itemId); levelled != nullptr)
        {
            if (levelled->useAll())
            {
                bool addedAny = false;
                for (const ESM4::LVLO& entry : levelled->mLvlObject)
                {
                    const ESM::FormId entryId = ESM::FormId::fromUint32(entry.item);
                    if (entryId.isZeroOrUnset())
                        continue;

                    addedAny = appendContainerInventoryEntry(
                                   stacks, entryId, count * std::max<int>(entry.count, 1), depth + 1)
                        || addedAny;
                }

                if (addedAny)
                    return true;
            }
            else if (const std::optional<InventoryStack> resolved = resolveContainerInventoryStack(itemId, count, depth + 1);
                resolved.has_value())
            {
                mergeStackIntoList(stacks, *resolved);
                return true;
            }
        }

        if (const ESM4::FormIdList* formList = mContent->findFormList(itemId); formList != nullptr)
        {
            for (ESM::FormId formId : formList->mObjects)
            {
                if (const std::optional<InventoryStack> resolved
                    = resolveContainerInventoryStack(formId, count, depth + 1);
                    resolved.has_value())
                {
                    mergeStackIntoList(stacks, *resolved);
                    return true;
                }
            }
        }

        mergeStackIntoList(stacks, buildInventoryStackForBase(itemId, count));
        return true;
    }

    void mergeStackIntoList(std::vector<InventoryStack>& stacks, const InventoryStack& stack) const
    {
        if (stack.mCount <= 0)
            return;

        for (InventoryStack& existing : stacks)
        {
            if (existing.mBaseId == stack.mBaseId)
            {
                existing.mCount += stack.mCount;
                if (existing.mEditorId.empty() && !stack.mEditorId.empty())
                    existing.mEditorId = stack.mEditorId;
                if (existing.mText.empty() && !stack.mText.empty())
                    existing.mText = stack.mText;
                if (existing.mIcon.empty() && !stack.mIcon.empty())
                    existing.mIcon = stack.mIcon;
                if (existing.mCategory.empty() && !stack.mCategory.empty())
                    existing.mCategory = stack.mCategory;
                if (existing.mValue == 0)
                    existing.mValue = stack.mValue;
                if (existing.mWeight == 0.f)
                    existing.mWeight = stack.mWeight;
                existing.mReadable = existing.mReadable || stack.mReadable;
                existing.mNoTake = existing.mNoTake || stack.mNoTake;
                return;
            }
        }

        stacks.push_back(stack);
    }

    void addToInventory(const InventoryStack& stack)
    {
        if (stack.mCount <= 0)
            return;

        InventoryStack& slot = mInventory[stack.mBaseId];
        if (slot.mCount == 0)
            slot = stack;
        else
        {
            slot.mCount += stack.mCount;
            if (slot.mEditorId.empty() && !stack.mEditorId.empty())
                slot.mEditorId = stack.mEditorId;
            if (slot.mText.empty() && !stack.mText.empty())
                slot.mText = stack.mText;
            if (slot.mIcon.empty() && !stack.mIcon.empty())
                slot.mIcon = stack.mIcon;
            if (slot.mCategory.empty() && !stack.mCategory.empty())
                slot.mCategory = stack.mCategory;
            if (slot.mValue == 0)
                slot.mValue = stack.mValue;
            if (slot.mWeight == 0.f)
                slot.mWeight = stack.mWeight;
            slot.mReadable = slot.mReadable || stack.mReadable;
            slot.mNoTake = slot.mNoTake || stack.mNoTake;
        }
    }

    int totalItemCount(const std::vector<InventoryStack>& stacks) const
    {
        int total = 0;
        for (const InventoryStack& stack : stacks)
            total += stack.mCount;
        return total;
    }

    int inventoryItemCount() const
    {
        int total = 0;
        for (const auto& [baseId, stack] : mInventory)
        {
            (void)baseId;
            total += stack.mCount;
        }
        return total;
    }

    std::vector<InventoryStack*> buildInventoryView()
    {
        std::vector<InventoryStack*> view;
        view.reserve(mInventory.size());
        for (auto& [baseId, stack] : mInventory)
        {
            (void)baseId;
            if (stack.mCount > 0)
                view.push_back(&stack);
        }

        std::sort(view.begin(), view.end(), [](const InventoryStack* left, const InventoryStack* right) {
            if (left == nullptr || right == nullptr)
                return left < right;
            if (left->mName != right->mName)
                return left->mName < right->mName;
            return left->mBaseId < right->mBaseId;
        });
        return view;
    }

    std::vector<InventoryStack>& resolveContainerState(const SceneObjectMetadata& metadata)
    {
        auto [it, inserted] = mContainerStates.try_emplace(metadata.mReferenceId);
        if (inserted)
        {
            if (const BaseRecordData* base = mContent->findBase(metadata.mBaseId); base != nullptr)
            {
                for (const ESM4::InventoryItem& item : base->mContainerItems)
                {
                    appendContainerInventoryEntry(
                        it->second, ESM::FormId::fromUint32(item.item), static_cast<int>(item.count));
                }
            }
        }
        return it->second;
    }

    std::vector<InventoryStack*> buildContainerView(const SceneObjectMetadata& metadata)
    {
        std::vector<InventoryStack*> view;
        std::vector<InventoryStack>& contents = resolveContainerState(metadata);
        view.reserve(contents.size());
        for (InventoryStack& stack : contents)
        {
            if (stack.mCount > 0)
                view.push_back(&stack);
        }

        std::sort(view.begin(), view.end(), [](const InventoryStack* left, const InventoryStack* right) {
            if (left == nullptr || right == nullptr)
                return left < right;
            if (left->mName != right->mName)
                return left->mName < right->mName;
            return left->mBaseId < right->mBaseId;
        });
        return view;
    }

    [[nodiscard]] bool isSessionUnlocked(ESM::FormId referenceId) const
    {
        return !referenceId.isZeroOrUnset() && mUnlockedReferenceIds.contains(referenceId);
    }

    [[nodiscard]] bool isLockedForSession(const SceneObjectMetadata& metadata) const
    {
        return isLockableKind(metadata.mKind) && metadata.mLocked && !isSessionUnlocked(metadata.mReferenceId);
    }

    [[nodiscard]] bool hasInventoryItem(ESM::FormId baseId) const
    {
        if (baseId.isZeroOrUnset())
            return false;
        const auto it = mInventory.find(baseId);
        return it != mInventory.end() && it->second.mCount > 0;
    }

    [[nodiscard]] bool canUseKey(const SceneObjectMetadata& metadata) const
    {
        return !metadata.mKey.isZeroOrUnset() && hasInventoryItem(metadata.mKey);
    }

    [[nodiscard]] int requiredAccessSkill(const SceneObjectMetadata& metadata) const
    {
        return std::clamp(metadata.mLockLevel, 0, 100);
    }

    [[nodiscard]] bool isKeyOnlyLock(const SceneObjectMetadata& metadata) const
    {
        return metadata.mLockLevel < 0;
    }

    [[nodiscard]] bool canUnlockWithSkill(const SceneObjectMetadata& metadata) const
    {
        if (!isLockedForSession(metadata) || isKeyOnlyLock(metadata))
            return false;

        const int required = requiredAccessSkill(metadata);
        if (metadata.mKind == ObjectKind::Terminal)
            return mPlayerCapabilities.mScience >= required;
        if (metadata.mKind == ObjectKind::Door || metadata.mKind == ObjectKind::Container)
            return mPlayerCapabilities.mLockpick >= required;
        return false;
    }

    [[nodiscard]] std::string describeKeyRequirement(ESM::FormId keyForm) const
    {
        if (keyForm.isZeroOrUnset())
            return "Requires key";

        if (const BaseRecordData* keyBase = mContent->findBase(keyForm); keyBase != nullptr)
        {
            if (!keyBase->mFullName.empty())
                return "Requires key: " + keyBase->mFullName;
            if (!keyBase->mEditorId.empty())
                return "Requires key: " + keyBase->mEditorId;
        }

        return "Requires key: " + ESM::RefId::formIdRefId(keyForm).serializeText();
    }

    [[nodiscard]] std::string describeAccessRequirement(const SceneObjectMetadata& metadata) const
    {
        if (isKeyOnlyLock(metadata))
            return describeKeyRequirement(metadata.mKey);

        std::ostringstream stream;
        if (metadata.mKind == ObjectKind::Terminal)
            stream << "Requires Science " << requiredAccessSkill(metadata);
        else
            stream << "Requires Lockpick " << requiredAccessSkill(metadata);

        if (!metadata.mKey.isZeroOrUnset())
            stream << "\n" << describeKeyRequirement(metadata.mKey);
        return stream.str();
    }

    std::vector<AccessPromptAction> buildAccessPromptActions(const SceneObjectMetadata& metadata) const
    {
        std::vector<AccessPromptAction> actions;
        if (canUseKey(metadata))
            actions.push_back({ AccessPromptActionType::UseKey, "Use Key" });
        if (canUnlockWithSkill(metadata))
        {
            actions.push_back({ AccessPromptActionType::SkillUnlock,
                metadata.mKind == ObjectKind::Terminal ? "Hack" : "Pick Lock" });
        }
        actions.push_back({ AccessPromptActionType::Close, "Close" });
        return actions;
    }

    std::string buildAccessPromptBody(
        const SceneObjectMetadata& metadata, const std::vector<AccessPromptAction>& actions)
    {
        clampPanelSelection(static_cast<int>(actions.size()));
        std::ostringstream stream;
        stream << (metadata.mKind == ObjectKind::Terminal ? "Terminal Locked" : "Locked") << "\n";
        stream << describeAccessRequirement(metadata);
        if (metadata.mKind == ObjectKind::Terminal)
            stream << "\nScience: " << mPlayerCapabilities.mScience;
        else
            stream << "\nLockpick: " << mPlayerCapabilities.mLockpick;

        stream << "\n\n";
        for (int i = 0; i < static_cast<int>(actions.size()); ++i)
        {
            stream << (i == mPanelSelectionIndex ? "> " : "  ") << actions[i].mLabel;
            if (i + 1 < static_cast<int>(actions.size()))
                stream << '\n';
        }
        return stream.str();
    }

    [[nodiscard]] bool isUnlockableLinkedReference(ESM::FormId referenceId) const
    {
        if (referenceId.isZeroOrUnset())
            return false;

        if (const SceneObjectMetadata* metadata = findObjectByReferenceId(referenceId); metadata != nullptr)
            return isLockableKind(metadata->mKind);

        const ESM4::Reference* ref = mContent->findReference(referenceId);
        if (ref == nullptr)
            return false;
        const BaseRecordData* base = mContent->findBase(ref->mBaseObj);
        return base != nullptr && isLockableKind(base->mKind);
    }

    void unlockReferenceForSession(ESM::FormId referenceId)
    {
        if (referenceId.isZeroOrUnset())
            return;
        mUnlockedReferenceIds.insert(referenceId);
    }

    void unlockInteractionTarget(const SceneObjectMetadata& metadata)
    {
        unlockReferenceForSession(metadata.mReferenceId);
        if (metadata.mKind == ObjectKind::Terminal && isUnlockableLinkedReference(metadata.mTargetRef))
            unlockReferenceForSession(metadata.mTargetRef);
    }

    void openAccessPrompt(SceneObjectMetadata& metadata)
    {
        clearMovementState();
        if (mGrabMouse)
            applyMouseMode(false);
        mPanelMode = PanelMode::Access;
        mPanelTitle = metadata.mName;
        mPanelTargetReferenceId = metadata.mReferenceId;
        mPanelTextLines.clear();
        mPanelSelectionIndex = 0;
        mPanelScrollOffset = 0;
        refreshActivePanel();
    }

    void openAccessDeniedPanel(const SceneObjectMetadata& metadata)
    {
        std::ostringstream stream;
        stream << metadata.mName << "\n\n" << describeAccessRequirement(metadata);
        openPanel(metadata.mKind == ObjectKind::Terminal ? "Terminal Locked" : "Locked", stream.str());
    }

    [[nodiscard]] bool isValidPanelWidgetState() const
    {
        return mPanelRoot != nullptr && mPanelTitleText != nullptr && mPanelBodyText != nullptr && mPanelFooterText != nullptr;
    }

    void addBootstrapDebugMarker(const osg::Vec3f& position, const osg::Vec4f& color)
    {
        if (mWorldRoot == nullptr)
            return;

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;

        osg::ref_ptr<osg::ShapeDrawable> sphere = new osg::ShapeDrawable(new osg::Sphere(position, 96.f));
        sphere->setColor(color);
        geode->addDrawable(sphere);

        osg::ref_ptr<osg::ShapeDrawable> pillar
            = new osg::ShapeDrawable(new osg::Box(position + osg::Vec3f(0.f, 0.f, 320.f), 48.f, 48.f, 640.f));
        pillar->setColor(osg::Vec4f(color.r(), color.g(), color.b(), 0.85f));
        geode->addDrawable(pillar);

        osg::StateSet* const stateSet = geode->getOrCreateStateSet();
        stateSet->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
        stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        geode->setNodeMask(sObjectNodeMask);
        geode->setName("OpenFO3 Bootstrap Debug Marker");
        mWorldRoot->addChild(geode);

        Log(Debug::Info) << "OpenFO3 bootstrap debug marker at (" << position.x() << ", " << position.y() << ", "
                         << position.z() << ")";
    }

    void populateExteriorBucket(const ExteriorBucketKey& bucketKey, std::vector<SceneObjectMetadata*>& bucketObjects,
        BucketLoadMode loadMode, bool allowNonInteractiveCollision, SceneMutationStats* mutationStats = nullptr)
    {
        const auto* refs = mContent->getExteriorSpatialReferences(bucketKey);
        if (refs == nullptr)
        {
            Log(Debug::Warning) << "OpenFO3 found no references for bucket " << bucketLabel(bucketKey);
            return;
        }

        osg::Group* const cellRoot = ensureBucketRoot(bucketKey);

        std::size_t skippedMissingBase = 0;
        std::size_t skippedMissingModel = 0;
        std::size_t instantiated = 0;
        std::size_t collisionShapes = 0;
        std::size_t renderableCount = 0;
        std::size_t effectOnlyCount = 0;
        bool hasPopulatedBounds = false;
        osg::Vec3f populatedMin(0.f, 0.f, 0.f);
        osg::Vec3f populatedMax(0.f, 0.f, 0.f);

        for (const auto* ref : *refs)
        {
            if (ref == nullptr)
                continue;
            if (mPickedUpReferenceIds.contains(ref->mId))
                continue;
            if (findObjectByReferenceId(ref->mId) != nullptr)
                continue;

            const BaseRecordData* base = mContent->findBase(ref->mBaseObj);
            if (base == nullptr)
            {
                ++skippedMissingBase;
                continue;
            }
            if (base->mModel.empty())
            {
                ++skippedMissingModel;
                continue;
            }

            const VFS::Path::Normalized meshPath = makeMeshPath(base->mModel);
            osg::ref_ptr<osg::Node> instance = mResourceSystem->getSceneManager()->getInstance(meshPath);
            if (instance == nullptr)
                continue;
            const NodeRenderStats& renderStats = getCachedRenderStats(*instance, meshPath.value());
            if (instance->getNodeMask() == 0u && renderStats.hasRenderableGeometry())
                Log(Debug::Warning) << "OpenFO3 invisible instance root remained hidden for " << meshPath << " ref="
                                    << ESM::RefId(ref->mId).serializeText();

            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> transform = new SceneUtil::PositionAttitudeTransform;
            transform->setPosition(ref->mPos.asVec3());
            transform->setAttitude(Misc::Convert::makeOsgQuat(ref->mPos));
            transform->setScale(osg::Vec3f(ref->mScale, ref->mScale, ref->mScale));
            transform->setNodeMask(sObjectNodeMask);
            transform->addChild(instance);

            auto metadata = std::make_unique<SceneObjectMetadata>();
            metadata->mKind = base->mKind;
            metadata->mBaseId = ref->mBaseObj;
            metadata->mReferenceId = ref->mId;
            metadata->mCellId = ref->mParent;
            metadata->mBucketKey = bucketKey;
            metadata->mName = chooseDisplayName(*base, *ref, toString(base->mKind));
            metadata->mEditorId = ref->mEditorId.empty() ? base->mEditorId : ref->mEditorId;
            metadata->mModelPath = meshPath.value();
            metadata->mText = base->mText;
            metadata->mResultText = base->mResultText;
            metadata->mIcon = base->mIcon;
            metadata->mCategory = base->mCategory;
            metadata->mValue = base->mValue;
            metadata->mWeight = base->mWeight;
            metadata->mItemCount = base->mItemCount;
            metadata->mReadable = base->mReadable;
            metadata->mNoTake = base->mNoTake;
            if (base->mKind == ObjectKind::Container)
            {
                if (const auto it = mContainerStates.find(ref->mId); it != mContainerStates.end())
                    metadata->mItemCount = totalItemCount(it->second);
            }
            metadata->mDoor = ref->mDoor;
            metadata->mLocked = ref->mIsLocked;
            metadata->mLockLevel = static_cast<int>(ref->mLockLevel);
            metadata->mKey = ref->mKey;
            metadata->mTargetRef = ref->mTargetRef;
            metadata->mTransform = ref->mPos;
            metadata->mScale = ref->mScale;
            metadata->mNode = transform.get();

            const osg::BoundingSphere bound = transform->getBound();
            const bool hasFiniteBounds
                = bound.valid() && std::isfinite(bound.radius()) && bound.radius() > 0.f
                && std::isfinite(bound.center().x()) && std::isfinite(bound.center().y()) && std::isfinite(bound.center().z());
            const bool likelyEffectModel = isLikelyEffectModelPath(metadata->mModelPath);
            const bool renderableFallback = hasFiniteBounds && !likelyEffectModel && !renderStats.isEffectOnly();
            metadata->mRenderable = hasFiniteBounds && (renderStats.hasRenderableGeometry() || renderableFallback);
            metadata->mEffectOnly = renderStats.isEffectOnly() || (likelyEffectModel && !metadata->mRenderable);
            metadata->mBoundCenter = bound.valid() ? bound.center() : ref->mPos.asVec3();
            metadata->mBoundRadius = bound.valid() && std::isfinite(bound.radius()) ? bound.radius() : 0.f;
            const bool landmarkTaggedRenderable = metadata->mBoundRadius >= sTaggedLandmarkMinBoundRadius
                && isLikelyLandmarkModelPath(metadata->mModelPath);
            const bool landmarkSizedRenderable = metadata->mBoundRadius >= sLandmarkVisualMinBoundRadius;
            metadata->mMinLoadMode = [&]() {
                if (metadata->mRenderable && !metadata->mEffectOnly && !isInteractiveKind(metadata->mKind))
                {
                    if (landmarkSizedRenderable || landmarkTaggedRenderable)
                        return BucketLoadMode::LandmarkVisual;
                    if (metadata->mBoundRadius >= sFarVisualMinBoundRadius)
                        return BucketLoadMode::FarVisual;
                }

                return isInteractiveKind(metadata->mKind) ? BucketLoadMode::Interaction : BucketLoadMode::NearFull;
            }();

            const bool includeInBucket = [&]() {
                return (metadata->mRenderable || metadata->mEffectOnly)
                    && isModeAtLeast(loadMode, metadata->mMinLoadMode);
            }();
            if (!includeInBucket)
                continue;

            cellRoot->addChild(transform);

            if (metadata->mRenderable)
            {
                ++renderableCount;
                const osg::Vec3f extent(metadata->mBoundRadius, metadata->mBoundRadius, metadata->mBoundRadius);
                if (!hasPopulatedBounds)
                {
                    populatedMin = metadata->mBoundCenter - extent;
                    populatedMax = metadata->mBoundCenter + extent;
                    hasPopulatedBounds = true;
                }
                else
                {
                    populatedMin.x() = std::min(populatedMin.x(), metadata->mBoundCenter.x() - extent.x());
                    populatedMin.y() = std::min(populatedMin.y(), metadata->mBoundCenter.y() - extent.y());
                    populatedMin.z() = std::min(populatedMin.z(), metadata->mBoundCenter.z() - extent.z());
                    populatedMax.x() = std::max(populatedMax.x(), metadata->mBoundCenter.x() + extent.x());
                    populatedMax.y() = std::max(populatedMax.y(), metadata->mBoundCenter.y() + extent.y());
                    populatedMax.z() = std::max(populatedMax.z(), metadata->mBoundCenter.z() + extent.z());
                }
            }
            else if (metadata->mEffectOnly)
                ++effectOnlyCount;

            if (isInteractiveKind(base->mKind))
            {
                metadata->mHasFocusProxy = true;
                metadata->mFocusProxyCenter = metadata->mRenderable ? metadata->mBoundCenter : ref->mPos.asVec3();
                const float focusRadius = metadata->mBoundRadius > 0.f ? metadata->mBoundRadius : 1.f;
                metadata->mFocusProxyRadius = std::max(
                    std::max(focusRadius * std::max(ref->mScale, 1.f), 1.f), sFocusProxyRadiusMin);
            }

            SceneObjectMetadata* const metadataPtr = metadata.get();
            bucketObjects.push_back(metadataPtr);
            registerObject(std::move(metadata));
            syncExteriorSceneObjectState(*metadataPtr, loadMode, allowNonInteractiveCollision, mutationStats);
            if (metadataPtr->mCollisionObject != nullptr)
                ++collisionShapes;
            if (mutationStats != nullptr)
                ++mutationStats->mRefsInstantiated;
            ++instantiated;
        }

        std::optional<osg::BoundingSphere> populatedBounds;
        if (hasPopulatedBounds)
        {
            const osg::Vec3f center = (populatedMin + populatedMax) * 0.5f;
            const float radius = std::max((populatedMax - center).length(), 1.f);
            populatedBounds = osg::BoundingSphere(center, radius);
        }

        updateBucketSceneStats(bucketKey, loadMode, refs->size(), instantiated, collisionShapes, renderableCount, effectOnlyCount,
            skippedMissingBase, skippedMissingModel, populatedBounds);
    }

    void populateInteriorCell(const ESM::RefId& cellId)
    {
        const auto* refs = mContent->getReferences(cellId);
        if (refs == nullptr)
        {
            Log(Debug::Warning) << "OpenFO3 found no references for interior cell " << cellId.serializeText();
            return;
        }

        osg::Group* const cellRoot = ensureInteriorRoot(cellId);
        mLoadedInteriorScene.mLoaded = true;
        mLoadedInteriorScene.mCellId = cellId;

        std::size_t skippedMissingBase = 0;
        std::size_t skippedMissingModel = 0;
        std::size_t instantiated = 0;
        std::size_t collisionShapes = 0;
        std::size_t renderableCount = 0;
        std::size_t effectOnlyCount = 0;
        bool hasPopulatedBounds = false;
        osg::Vec3f populatedMin(0.f, 0.f, 0.f);
        osg::Vec3f populatedMax(0.f, 0.f, 0.f);

        for (const auto& [refId, ref] : *refs)
        {
            (void)refId;
            if (mPickedUpReferenceIds.contains(ref.mId))
                continue;
            const BaseRecordData* base = mContent->findBase(ref.mBaseObj);
            if (base == nullptr)
            {
                ++skippedMissingBase;
                continue;
            }
            if (base->mModel.empty())
            {
                ++skippedMissingModel;
                continue;
            }

            const VFS::Path::Normalized meshPath = makeMeshPath(base->mModel);
            osg::ref_ptr<osg::Node> instance = mResourceSystem->getSceneManager()->getInstance(meshPath);
            if (instance == nullptr)
                continue;
            const NodeRenderStats& renderStats = getCachedRenderStats(*instance, meshPath.value());
            if (instance->getNodeMask() == 0u && renderStats.hasRenderableGeometry())
                Log(Debug::Warning) << "OpenFO3 invisible instance root remained hidden for " << meshPath << " ref="
                                    << ESM::RefId(ref.mId).serializeText();

            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> transform = new SceneUtil::PositionAttitudeTransform;
            transform->setPosition(ref.mPos.asVec3());
            transform->setAttitude(Misc::Convert::makeOsgQuat(ref.mPos));
            transform->setScale(osg::Vec3f(ref.mScale, ref.mScale, ref.mScale));
            transform->setNodeMask(sObjectNodeMask);
            transform->addChild(instance);

            auto metadata = std::make_unique<SceneObjectMetadata>();
            metadata->mKind = base->mKind;
            metadata->mBaseId = ref.mBaseObj;
            metadata->mReferenceId = ref.mId;
            metadata->mCellId = cellId;
            metadata->mName = chooseDisplayName(*base, ref, toString(base->mKind));
            metadata->mEditorId = ref.mEditorId.empty() ? base->mEditorId : ref.mEditorId;
            metadata->mModelPath = meshPath.value();
            metadata->mText = base->mText;
            metadata->mResultText = base->mResultText;
            metadata->mIcon = base->mIcon;
            metadata->mCategory = base->mCategory;
            metadata->mValue = base->mValue;
            metadata->mWeight = base->mWeight;
            metadata->mItemCount = base->mItemCount;
            metadata->mReadable = base->mReadable;
            metadata->mNoTake = base->mNoTake;
            if (base->mKind == ObjectKind::Container)
            {
                if (const auto it = mContainerStates.find(ref.mId); it != mContainerStates.end())
                    metadata->mItemCount = totalItemCount(it->second);
            }
            metadata->mDoor = ref.mDoor;
            metadata->mLocked = ref.mIsLocked;
            metadata->mLockLevel = static_cast<int>(ref.mLockLevel);
            metadata->mKey = ref.mKey;
            metadata->mTargetRef = ref.mTargetRef;
            metadata->mTransform = ref.mPos;
            metadata->mScale = ref.mScale;
            metadata->mNode = transform.get();

            const osg::BoundingSphere bound = transform->getBound();
            const bool hasFiniteBounds
                = bound.valid() && std::isfinite(bound.radius()) && bound.radius() > 0.f
                && std::isfinite(bound.center().x()) && std::isfinite(bound.center().y()) && std::isfinite(bound.center().z());
            const bool likelyEffectModel = isLikelyEffectModelPath(metadata->mModelPath);
            const bool renderableFallback = hasFiniteBounds && !likelyEffectModel && !renderStats.isEffectOnly();
            metadata->mRenderable = hasFiniteBounds && (renderStats.hasRenderableGeometry() || renderableFallback);
            metadata->mEffectOnly = renderStats.isEffectOnly() || (likelyEffectModel && !metadata->mRenderable);
            metadata->mBoundCenter = bound.valid() ? bound.center() : ref.mPos.asVec3();
            metadata->mBoundRadius = bound.valid() && std::isfinite(bound.radius()) ? bound.radius() : 0.f;
            metadata->mMinLoadMode = BucketLoadMode::NearFull;

            if (!(metadata->mRenderable || metadata->mEffectOnly))
                continue;

            cellRoot->addChild(transform);

            if (metadata->mRenderable)
            {
                ++renderableCount;
                const osg::Vec3f extent(metadata->mBoundRadius, metadata->mBoundRadius, metadata->mBoundRadius);
                if (!hasPopulatedBounds)
                {
                    populatedMin = metadata->mBoundCenter - extent;
                    populatedMax = metadata->mBoundCenter + extent;
                    hasPopulatedBounds = true;
                }
                else
                {
                    populatedMin.x() = std::min(populatedMin.x(), metadata->mBoundCenter.x() - extent.x());
                    populatedMin.y() = std::min(populatedMin.y(), metadata->mBoundCenter.y() - extent.y());
                    populatedMin.z() = std::min(populatedMin.z(), metadata->mBoundCenter.z() - extent.z());
                    populatedMax.x() = std::max(populatedMax.x(), metadata->mBoundCenter.x() + extent.x());
                    populatedMax.y() = std::max(populatedMax.y(), metadata->mBoundCenter.y() + extent.y());
                    populatedMax.z() = std::max(populatedMax.z(), metadata->mBoundCenter.z() + extent.z());
                }
            }
            else
                ++effectOnlyCount;

            if (isInteractiveKind(base->mKind))
            {
                metadata->mHasFocusProxy = true;
                metadata->mFocusProxyCenter = metadata->mRenderable ? metadata->mBoundCenter : ref.mPos.asVec3();
                const float focusRadius = metadata->mBoundRadius > 0.f ? metadata->mBoundRadius : 1.f;
                metadata->mFocusProxyRadius = std::max(
                    std::max(focusRadius * std::max(ref.mScale, 1.f), 1.f), sFocusProxyRadiusMin);
            }

            const bool addInteractiveCollision = isBlockingInteractiveKind(base->mKind);
            const bool addNonInteractiveCollision = metadata->mRenderable && !metadata->mEffectOnly;
            if (mCollisionScene != nullptr && (addInteractiveCollision || addNonInteractiveCollision))
            {
                mCollisionScene->addStatic(*mBulletShapeManager, meshPath, ref.mPos, ref.mScale, *metadata);
                if (metadata->mCollisionObject != nullptr)
                    ++collisionShapes;
            }

            SceneObjectMetadata* const metadataPtr = metadata.get();
            mLoadedInteriorScene.mSceneObjects.push_back(metadataPtr);
            registerObject(std::move(metadata));
            setObjectFocusable(*metadataPtr, metadataPtr->mHasFocusProxy);
            ++instantiated;
        }

        std::optional<osg::BoundingSphere> populatedBounds;
        if (hasPopulatedBounds)
        {
            const osg::Vec3f center = (populatedMin + populatedMax) * 0.5f;
            const float radius = std::max((populatedMax - center).length(), 1.f);
            populatedBounds = osg::BoundingSphere(center, radius);
        }

        updateInteriorSceneStats(cellId, refs->size(), instantiated, collisionShapes, renderableCount, effectOnlyCount,
            skippedMissingBase, skippedMissingModel, populatedBounds);
    }

    void runLoop()
    {
        using clock = std::chrono::steady_clock;
        auto previous = clock::now();

        while (!mQuit && !mViewer->done())
        {
            const auto now = clock::now();
            const float dt = std::chrono::duration<float>(now - previous).count();
            previous = now;

            pumpEvents();
            updateSimulation(std::clamp(dt, 0.f, 0.1f));
            mViewer->frame();
        }
    }

    void pumpEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0)
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    mQuit = true;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    {
                        const int width = event.window.data1;
                        const int height = std::max(1, event.window.data2);
                        mViewer->getCamera()->setViewport(0, 0, width, height);
                        mViewer->getCamera()->setProjectionMatrixAsPerspective(
                            75.f, static_cast<double>(width) / height, sNearClip, sFarClip);
                        if (mPerViewUniformStateUpdater != nullptr)
                            mPerViewUniformStateUpdater->setProjectionMatrix(mViewer->getCamera()->getProjectionMatrix());
                        if (mSharedUniformStateUpdater != nullptr)
                            mSharedUniformStateUpdater->setScreenRes(static_cast<float>(width), static_cast<float>(height));
                        updateOverlayLayout(width, height);
                    }
                    break;
                case SDL_KEYDOWN:
                    if (!event.key.repeat)
                        handleKey(event.key.keysym.sym, true);
                    break;
                case SDL_KEYUP:
                    handleKey(event.key.keysym.sym, false);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (!mGrabMouse && event.button.button == SDL_BUTTON_RIGHT)
                        applyMouseMode(true);
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (!mGrabMouse && event.button.button == SDL_BUTTON_RIGHT)
                        applyMouseMode(false);
                    break;
                case SDL_MOUSEMOTION:
                    if (mMouseLookActive && !mPanelOpen)
                    {
                        mYaw += event.motion.xrel * sMouseSensitivity;
                        mPitch = std::clamp(mPitch - event.motion.yrel * sMouseSensitivity, -1.45f, 1.45f);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void clearMovementState()
    {
        mMoveForward = false;
        mMoveBackward = false;
        mMoveLeft = false;
        mMoveRight = false;
        mMoveUp = false;
        mMoveDown = false;
        mSprint = false;
        mPendingInteract = false;
    }

    SceneObjectMetadata* findObjectByReferenceId(ESM::FormId referenceId) const
    {
        const auto it = mObjectByReferenceId.find(referenceId);
        return it == mObjectByReferenceId.end() ? nullptr : it->second;
    }

    void setObjectFocusable(SceneObjectMetadata& metadata, bool focusable)
    {
        if (focusable)
        {
            if (metadata.mFocusableIndex != sInvalidIndex)
                return;
            metadata.mFocusableIndex = mFocusableObjects.size();
            mFocusableObjects.push_back(&metadata);
            return;
        }

        if (metadata.mFocusableIndex == sInvalidIndex)
            return;

        const std::size_t index = metadata.mFocusableIndex;
        const std::size_t lastIndex = mFocusableObjects.size() - 1;
        if (index != lastIndex)
        {
            mFocusableObjects[index] = mFocusableObjects[lastIndex];
            if (mFocusableObjects[index] != nullptr)
                mFocusableObjects[index]->mFocusableIndex = index;
        }
        mFocusableObjects.pop_back();
        metadata.mFocusableIndex = sInvalidIndex;
    }

    void registerObject(std::unique_ptr<SceneObjectMetadata> metadata)
    {
        if (metadata == nullptr)
            return;

        SceneObjectMetadata* const ptr = metadata.get();
        ptr->mObjectIndex = mObjects.size();
        mObjectByReferenceId[ptr->mReferenceId] = ptr;
        mObjects.push_back(std::move(metadata));
    }

    void destroySceneObject(SceneObjectMetadata& metadata)
    {
        setObjectFocusable(metadata, false);
        if (mFocusedObject == &metadata)
            mFocusedObject = nullptr;
        if (mPanelOpen && metadata.mReferenceId == mPanelTargetReferenceId)
            closePanel();
        if (mCollisionScene != nullptr)
            mCollisionScene->remove(metadata);
        if (metadata.mNode != nullptr && metadata.mNode->getNumParents() > 0)
            metadata.mNode->getParent(0)->removeChild(metadata.mNode);

        mObjectByReferenceId.erase(metadata.mReferenceId);
        if (metadata.mObjectIndex == sInvalidIndex || metadata.mObjectIndex >= mObjects.size())
            return;

        const std::size_t index = metadata.mObjectIndex;
        const std::size_t lastIndex = mObjects.size() - 1;
        if (index != lastIndex)
        {
            mObjects[index] = std::move(mObjects[lastIndex]);
            if (mObjects[index] != nullptr)
                mObjects[index]->mObjectIndex = index;
        }
        mObjects.pop_back();
    }

    void clampPanelSelection(int itemCount)
    {
        if (itemCount <= 0)
        {
            mPanelSelectionIndex = 0;
            mPanelScrollOffset = 0;
            return;
        }

        mPanelSelectionIndex = std::clamp(mPanelSelectionIndex, 0, itemCount - 1);
        const int maxOffset = std::max(itemCount - sPanelListVisibleItems, 0);
        if (mPanelSelectionIndex < mPanelScrollOffset)
            mPanelScrollOffset = mPanelSelectionIndex;
        else if (mPanelSelectionIndex >= mPanelScrollOffset + sPanelListVisibleItems)
            mPanelScrollOffset = mPanelSelectionIndex - sPanelListVisibleItems + 1;
        mPanelScrollOffset = std::clamp(mPanelScrollOffset, 0, maxOffset);
    }

    void clampTextScroll()
    {
        const int maxOffset = std::max(static_cast<int>(mPanelTextLines.size()) - sPanelTextVisibleLines, 0);
        mPanelScrollOffset = std::clamp(mPanelScrollOffset, 0, maxOffset);
    }

    std::string buildVisibleTextBody() const
    {
        if (mPanelTextLines.empty())
            return {};

        std::ostringstream stream;
        const int end = std::min<int>(static_cast<int>(mPanelTextLines.size()), mPanelScrollOffset + sPanelTextVisibleLines);
        for (int i = mPanelScrollOffset; i < end; ++i)
        {
            stream << mPanelTextLines[i];
            if (i + 1 < end)
                stream << '\n';
        }
        if (end < static_cast<int>(mPanelTextLines.size()))
            stream << "\n\n[...]";
        return stream.str();
    }

    std::string buildInventoryBody(const std::vector<InventoryStack*>& view)
    {
        clampPanelSelection(static_cast<int>(view.size()));
        if (view.empty())
            return "Inventory is empty.";

        std::ostringstream stream;
        stream << "Stacks: " << view.size() << "  Total items: " << inventoryItemCount() << "\n\n";
        const int end = std::min<int>(static_cast<int>(view.size()), mPanelScrollOffset + sPanelListVisibleItems);
        for (int i = mPanelScrollOffset; i < end; ++i)
        {
            const InventoryStack& stack = *view[i];
            stream << (i == mPanelSelectionIndex ? "> " : "  ") << stack.mName;
            if (stack.mCount > 1)
                stream << " x" << stack.mCount;
            if (!stack.mCategory.empty())
                stream << " {" << stack.mCategory << "}";
            if (stack.mReadable)
                stream << " [Read]";
            if (i + 1 < end)
                stream << '\n';
        }
        return stream.str();
    }

    std::string buildContainerBody(SceneObjectMetadata& metadata, const std::vector<InventoryStack*>& view)
    {
        clampPanelSelection(static_cast<int>(view.size()));
        std::ostringstream stream;
        stream << "Contained items: " << metadata.mItemCount << "\n\n";
        if (view.empty())
        {
            stream << "Container is empty.";
            return stream.str();
        }

        const int end = std::min<int>(static_cast<int>(view.size()), mPanelScrollOffset + sPanelListVisibleItems);
        for (int i = mPanelScrollOffset; i < end; ++i)
        {
            const InventoryStack& stack = *view[i];
            stream << (i == mPanelSelectionIndex ? "> " : "  ") << stack.mName;
            if (stack.mCount > 1)
                stream << " x" << stack.mCount;
            if (!stack.mCategory.empty())
                stream << " {" << stack.mCategory << "}";
            if (stack.mReadable)
                stream << " [Read]";
            if (i + 1 < end)
                stream << '\n';
        }
        return stream.str();
    }

    std::string buildReadableText(const InventoryStack& stack) const
    {
        const std::string normalizedText = normalizeReadableText(stack.mText);
        if (stack.mKind == ObjectKind::Book)
        {
            if (!normalizedText.empty() && !isMissingReadablePlaceholder(normalizedText)
                && !isGenericBookText(normalizedText, stack.mName))
            {
                return normalizedText;
            }

            std::ostringstream stream;
            stream << stack.mName;

            const std::string skillName = skillNameForBookEditorId(stack.mEditorId);
            if (!skillName.empty())
                stream << "\n\nSkill book\nReading this book improves " << skillName << ".";
            else if (Misc::StringUtils::ciEqual(stack.mEditorId, "BookSkill01")
                || Misc::StringUtils::ciEqual(stack.mName, "You're SPECIAL"))
            {
                stream << "\n\nSpecial book\nThis item is used as a character-building book in Fallout 3.";
            }
            else
            {
                stream << "\n\nBook\nFallout 3 does not include readable page text for this book record.";
            }

            return stream.str();
        }

        if (!normalizedText.empty() && !isMissingReadablePlaceholder(normalizedText))
            return normalizedText;

        if (stack.mKind == ObjectKind::Note)
            return "This note does not contain readable text in the loaded Fallout 3 data.";

        return "No readable text was parsed for this record.";
    }

    void refreshActivePanel()
    {
        if (mPanelMode == PanelMode::None)
        {
            closePanel();
            return;
        }

        if (!isValidPanelWidgetState())
        {
            Log(Debug::Warning) << "OpenFO3 could not show panel because the overlay widgets are unavailable";
            mPanelMode = PanelMode::None;
            mPanelOpen = false;
            return;
        }

        std::string body;
        std::string footer;
        switch (mPanelMode)
        {
            case PanelMode::Info:
                clampTextScroll();
                body = buildVisibleTextBody();
                footer = "[W/S or Up/Down] scroll   [I] inventory   [Esc] close";
                break;
            case PanelMode::Terminal:
                clampTextScroll();
                body = buildVisibleTextBody();
                footer = "[W/S or Up/Down] scroll   [I] inventory   [Esc] close";
                break;
            case PanelMode::Read:
                clampTextScroll();
                body = buildVisibleTextBody();
                footer = "[W/S or Up/Down] scroll   [I] inventory   [Esc] close";
                break;
            case PanelMode::Inventory:
                body = buildInventoryBody(buildInventoryView());
                footer = "[W/S or Up/Down] select   [E/Enter] inspect   [R] read   [I] close   [Esc] close";
                break;
            case PanelMode::Container:
            {
                if (SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId); metadata != nullptr)
                {
                    body = buildContainerBody(*metadata, buildContainerView(*metadata));
                    footer = "[W/S or Up/Down] select   [E/Enter] inspect   [R] read   [A] take all   [I] inventory   [Esc] close";
                }
                else
                {
                    body = "This container is no longer available.";
                    footer = "[I] inventory   [Esc] close";
                }
                break;
            }
            case PanelMode::Access:
            {
                if (SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId); metadata != nullptr)
                {
                    const std::vector<AccessPromptAction> actions = buildAccessPromptActions(*metadata);
                    body = buildAccessPromptBody(*metadata, actions);
                    footer = "[W/S or Up/Down] select   [E/Enter] confirm   [I] inventory   [Esc] close";
                }
                else
                {
                    body = "This object is no longer available.";
                    footer = "[I] inventory   [Esc] close";
                }
                break;
            }
            case PanelMode::None:
                break;
        }

        mPanelTitleText->setText(mPanelTitle);
        mPanelBodyText->setText(body);
        mPanelFooterText->setText(footer);
        if (mPanelRoot != nullptr)
            mPanelRoot->setNodeMask(~0u);
        mPanelOpen = true;
    }

    void openTextPanel(PanelMode mode, std::string_view title, const std::string& body)
    {
        clearMovementState();
        if (mGrabMouse)
            applyMouseMode(false);
        mPanelMode = mode;
        mPanelTitle = std::string(title);
        mPanelTextLines = splitPanelLines(body);
        mPanelTargetReferenceId = {};
        mPanelSelectionIndex = 0;
        mPanelScrollOffset = 0;
        refreshActivePanel();
    }

    void openContainerPanel(SceneObjectMetadata& metadata)
    {
        clearMovementState();
        if (mGrabMouse)
            applyMouseMode(false);
        mPanelMode = PanelMode::Container;
        mPanelTitle = metadata.mName;
        mPanelTargetReferenceId = metadata.mReferenceId;
        mPanelTextLines.clear();
        mPanelSelectionIndex = 0;
        mPanelScrollOffset = 0;
        refreshActivePanel();
    }

    void openInventoryPanel()
    {
        clearMovementState();
        if (mGrabMouse)
            applyMouseMode(false);
        mPanelMode = PanelMode::Inventory;
        mPanelTitle = "Inventory";
        mPanelTargetReferenceId = {};
        mPanelTextLines.clear();
        mPanelSelectionIndex = 0;
        mPanelScrollOffset = 0;
        refreshActivePanel();
    }

    void openReadPanel(const InventoryStack& stack)
    {
        openTextPanel(PanelMode::Read, stack.mName, buildReadableText(stack));
    }

    void toggleInventoryPanel()
    {
        if (mPanelOpen && mPanelMode == PanelMode::Inventory)
        {
            closePanel();
            return;
        }

        openInventoryPanel();
    }

    void movePanelSelection(int delta)
    {
        if (delta == 0)
            return;

        switch (mPanelMode)
        {
            case PanelMode::Container:
            case PanelMode::Inventory:
            case PanelMode::Access:
                mPanelSelectionIndex += delta;
                break;
            case PanelMode::Info:
            case PanelMode::Terminal:
            case PanelMode::Read:
                mPanelScrollOffset += delta;
                break;
            case PanelMode::None:
                return;
        }

        refreshActivePanel();
    }

    void inspectSelectedInventoryEntry()
    {
        std::vector<InventoryStack*> view = buildInventoryView();
        clampPanelSelection(static_cast<int>(view.size()));
        if (view.empty())
            return;

        const InventoryStack& stack = *view[mPanelSelectionIndex];
        std::ostringstream stream;
        stream << stack.mName << "\n\nCount: " << stack.mCount << "\nKind: " << toString(stack.mKind);
        if (!stack.mCategory.empty())
            stream << "\nCategory: " << stack.mCategory;
        stream << "\nValue: " << stack.mValue << "\nWeight: " << std::fixed << std::setprecision(2) << stack.mWeight;
        if (stack.mReadable)
            stream << "\n\nPress R to read.";
        openTextPanel(PanelMode::Info, "Inventory", stream.str());
    }

    void inspectSelectedContainerEntry()
    {
        SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId);
        if (metadata == nullptr)
            return;

        std::vector<InventoryStack*> view = buildContainerView(*metadata);
        clampPanelSelection(static_cast<int>(view.size()));
        if (view.empty())
            return;

        const InventoryStack& stack = *view[mPanelSelectionIndex];
        std::ostringstream stream;
        stream << stack.mName << "\n\nCount: " << stack.mCount << "\nKind: " << toString(stack.mKind);
        if (!stack.mCategory.empty())
            stream << "\nCategory: " << stack.mCategory;
        stream << "\nValue: " << stack.mValue << "\nWeight: " << std::fixed << std::setprecision(2) << stack.mWeight;
        if (stack.mReadable)
            stream << "\n\nPress R to read.";
        stream << "\n\nPress A on the container list to take all available items.";
        openTextPanel(PanelMode::Info, metadata->mName, stream.str());
    }

    void readSelectedContainerEntry()
    {
        SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId);
        if (metadata == nullptr)
            return;

        std::vector<InventoryStack*> view = buildContainerView(*metadata);
        clampPanelSelection(static_cast<int>(view.size()));
        if (view.empty())
            return;

        const InventoryStack& stack = *view[mPanelSelectionIndex];
        if (stack.mReadable)
            openReadPanel(stack);
    }

    void readSelectedInventoryEntry()
    {
        std::vector<InventoryStack*> view = buildInventoryView();
        clampPanelSelection(static_cast<int>(view.size()));
        if (view.empty())
            return;

        const InventoryStack& stack = *view[mPanelSelectionIndex];
        if (stack.mReadable)
            openReadPanel(stack);
    }

    void takeSelectedContainerEntry()
    {
        SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId);
        if (metadata == nullptr)
            return;

        std::vector<InventoryStack*> view = buildContainerView(*metadata);
        clampPanelSelection(static_cast<int>(view.size()));
        if (view.empty())
            return;

        const ESM::FormId selectedBaseId = view[mPanelSelectionIndex]->mBaseId;
        std::vector<InventoryStack>& contents = resolveContainerState(*metadata);
        const auto it = std::find_if(contents.begin(), contents.end(), [&](const InventoryStack& stack) {
            return stack.mBaseId == selectedBaseId;
        });
        if (it == contents.end())
            return;
        if (it->mNoTake)
            return;

        addToInventory(*it);
        contents.erase(it);
        metadata->mItemCount = totalItemCount(contents);
        refreshActivePanel();
    }

    void takeAllContainerEntries()
    {
        SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId);
        if (metadata == nullptr)
            return;

        std::vector<InventoryStack>& contents = resolveContainerState(*metadata);
        std::vector<InventoryStack> remaining;
        remaining.reserve(contents.size());
        for (const InventoryStack& stack : contents)
        {
            if (stack.mNoTake)
                remaining.push_back(stack);
            else
                addToInventory(stack);
        }
        contents = std::move(remaining);
        metadata->mItemCount = 0;
        metadata->mItemCount = totalItemCount(contents);
        mPanelSelectionIndex = 0;
        mPanelScrollOffset = 0;
        refreshActivePanel();
    }

    void executeSelectedAccessAction()
    {
        SceneObjectMetadata* metadata = findObjectByReferenceId(mPanelTargetReferenceId);
        if (metadata == nullptr)
        {
            closePanel();
            return;
        }

        const std::vector<AccessPromptAction> actions = buildAccessPromptActions(*metadata);
        clampPanelSelection(static_cast<int>(actions.size()));
        if (actions.empty())
        {
            closePanel();
            return;
        }

        switch (actions[mPanelSelectionIndex].mType)
        {
            case AccessPromptActionType::UseKey:
            case AccessPromptActionType::SkillUnlock:
                unlockInteractionTarget(*metadata);
                closePanel();
                mFocusedObject = metadata;
                activateFocused();
                break;
            case AccessPromptActionType::Close:
                closePanel();
                break;
        }
    }

    void handlePanelKey(SDL_Keycode key)
    {
        switch (key)
        {
            case SDLK_ESCAPE:
                closePanel();
                break;
            case SDLK_i:
                toggleInventoryPanel();
                break;
            case SDLK_UP:
            case SDLK_w:
                movePanelSelection(-1);
                break;
            case SDLK_DOWN:
            case SDLK_s:
                movePanelSelection(1);
                break;
            case SDLK_a:
                if (mPanelMode == PanelMode::Container)
                    takeAllContainerEntries();
                break;
            case SDLK_r:
                if (mPanelMode == PanelMode::Inventory)
                    readSelectedInventoryEntry();
                else if (mPanelMode == PanelMode::Container)
                    readSelectedContainerEntry();
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_e:
                switch (mPanelMode)
                {
                    case PanelMode::Access:
                        executeSelectedAccessAction();
                        break;
                    case PanelMode::Container:
                        inspectSelectedContainerEntry();
                        break;
                    case PanelMode::Inventory:
                        inspectSelectedInventoryEntry();
                        break;
                    case PanelMode::Info:
                    case PanelMode::Terminal:
                    case PanelMode::Read:
                        closePanel();
                        break;
                    case PanelMode::None:
                        break;
                }
                break;
            default:
                break;
        }
    }

    void handleKey(SDL_Keycode key, bool pressed)
    {
        if (mPanelOpen)
        {
            if (pressed)
                handlePanelKey(key);
            return;
        }

        switch (key)
        {
            case SDLK_w:
                mMoveForward = pressed;
                break;
            case SDLK_s:
                mMoveBackward = pressed;
                break;
            case SDLK_a:
                mMoveLeft = pressed;
                break;
            case SDLK_d:
                mMoveRight = pressed;
                break;
            case SDLK_SPACE:
                mMoveUp = pressed;
                break;
            case SDLK_LCTRL:
            case SDLK_RCTRL:
                mMoveDown = pressed;
                break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT:
                mSprint = pressed;
                break;
            case SDLK_e:
                if (pressed)
                    mPendingInteract = true;
                break;
            case SDLK_i:
                if (pressed)
                    toggleInventoryPanel();
                break;
            case SDLK_c:
                if (pressed)
                {
                    mCollisionEnabled = !mCollisionEnabled;
                    if (mCollisionEnabled)
                    {
                        resetGroundSupportHistory();
                        groundPlayer();
                    }
                    else
                        resetGroundSupportHistory();
                }
                break;
            case SDLK_ESCAPE:
                if (pressed)
                    mQuit = true;
                break;
            default:
                break;
        }
    }

    void updateSimulation(float dt)
    {
        mSimulationTime += dt;
        updateMovement(dt);
        if (mSceneMode == SceneMode::Exterior)
            updateActiveExteriorGrid(getNewObjectGridCenterForCamera(mCameraPosition), false);
        updateFocus();
        if (mPendingInteract)
        {
            mPendingInteract = false;
            activateFocused();
        }
        if (mSharedUniformStateUpdater != nullptr)
            mSharedUniformStateUpdater->setPlayerPos(mCameraPosition);
        updateStatusText();
        updateCameraMatrix();
    }

    osg::Vec3f forwardVector() const
    {
        return osg::Vec3f(std::sin(mYaw) * std::cos(mPitch), std::cos(mYaw) * std::cos(mPitch), std::sin(mPitch));
    }

    osg::Vec3f rightVector() const
    {
        return osg::Vec3f(std::cos(mYaw), -std::sin(mYaw), 0.f);
    }

    struct GroundProbeResult
    {
        osg::Vec3f mPoint = osg::Vec3f(0.f, 0.f, 0.f);
        osg::Vec3f mNormal = osg::Vec3f(0.f, 0.f, 1.f);
        SceneObjectMetadata* mMetadata = nullptr;
        bool mFromTerrain = false;
    };

    bool matchesLastGroundSupport(const GroundProbeResult& result) const
    {
        if (!mHasLastGroundSupport)
            return false;

        if (result.mFromTerrain != mLastGroundSupportFromTerrain)
            return false;

        if (result.mFromTerrain)
            return true;

        return result.mMetadata != nullptr && result.mMetadata->mReferenceId == mLastGroundSupportReferenceId;
    }

    void updateGroundSupportHistory(const std::optional<GroundProbeResult>& ground)
    {
        if (!ground.has_value())
            return;

        mHasLastGroundSupport = true;
        mLastGroundSupportFromTerrain = ground->mFromTerrain;
        mLastGroundSupportReferenceId = ground->mMetadata != nullptr ? ground->mMetadata->mReferenceId : ESM::FormId{};
        mLastGroundSupportZ = ground->mPoint.z();
    }

    void resetGroundSupportHistory()
    {
        mHasLastGroundSupport = false;
        mLastGroundSupportFromTerrain = false;
        mLastGroundSupportReferenceId = ESM::FormId{};
        mLastGroundSupportZ = 0.f;
    }

    std::optional<GroundProbeResult> probeGroundAt(const osg::Vec3f& eyePosition, float referenceFootZ) const
    {
        std::optional<GroundProbeResult> bestMeshResult;
        std::optional<GroundProbeResult> bestTerrainResult;
        std::optional<GroundProbeResult> previousSupportResult;
        std::optional<GroundProbeResult> centerMeshResult;
        std::optional<GroundProbeResult> centerTerrainResult;
        const osg::Vec3f footPosition(eyePosition.x(), eyePosition.y(), referenceFootZ);
        const float probeDistance = std::max(sGroundProbeDistance, sMaxStepUp + sMaxStepDown + sPlayerRadius);
        const osg::Vec3f probeStartOffset(0.f, 0.f, sMaxStepUp + sPlayerRadius);
        const float footOffset = sGroundProbeFootOffsetScale * sPlayerRadius;
        osg::Vec3f horizontalForward(std::sin(mYaw), std::cos(mYaw), 0.f);
        osg::Vec3f horizontalRight(std::cos(mYaw), -std::sin(mYaw), 0.f);
        if (horizontalForward.length2() <= 1e-6f)
            horizontalForward = osg::Vec3f(0.f, 1.f, 0.f);
        else
            horizontalForward.normalize();
        if (horizontalRight.length2() <= 1e-6f)
            horizontalRight = osg::Vec3f(1.f, 0.f, 0.f);
        else
            horizontalRight.normalize();

        const std::array<osg::Vec3f, 5> probeOffsets = {
            osg::Vec3f(0.f, 0.f, 0.f),
            horizontalForward * footOffset,
            horizontalForward * -footOffset,
            horizontalRight * footOffset,
            horizontalRight * -footOffset,
        };

        auto considerResult = [&](const GroundProbeResult& candidate, bool preferMesh, bool centerProbe) {
            const float deltaZ = candidate.mPoint.z() - referenceFootZ;
            if (deltaZ > sMaxStepUp || deltaZ < -sMaxStepDown)
                return;
            if (candidate.mNormal.z() < sWalkableNormalMinZ)
                return;

            std::optional<GroundProbeResult>& bestResult = preferMesh ? bestMeshResult : bestTerrainResult;
            if (!bestResult.has_value() || candidate.mPoint.z() > bestResult->mPoint.z())
                bestResult = candidate;

            if (centerProbe)
            {
                std::optional<GroundProbeResult>& centerResult = preferMesh ? centerMeshResult : centerTerrainResult;
                if (!centerResult.has_value() || candidate.mPoint.z() > centerResult->mPoint.z())
                    centerResult = candidate;
            }

            if (matchesLastGroundSupport(candidate))
            {
                if (!previousSupportResult.has_value() || candidate.mPoint.z() > previousSupportResult->mPoint.z())
                    previousSupportResult = candidate;
            }
        };

        for (std::size_t probeIndex = 0; probeIndex < probeOffsets.size(); ++probeIndex)
        {
            const osg::Vec3f& probeOffset = probeOffsets[probeIndex];
            const bool centerProbe = probeIndex == 0;
            const osg::Vec3f samplePosition = footPosition + probeOffset;
            if (mCollisionScene != nullptr)
            {
                if (const std::optional<CollisionScene::RayHit> hit
                    = mCollisionScene->probeGround(samplePosition + probeStartOffset, probeDistance);
                    hit.has_value())
                {
                    considerResult(GroundProbeResult{
                        .mPoint = hit->mPoint,
                        .mNormal = hit->mNormal,
                        .mMetadata = hit->mMetadata,
                        .mFromTerrain = false,
                    },
                        true, centerProbe);
                }
            }

            if (mSceneMode == SceneMode::Exterior)
            {
                const float terrainZ = terrainHeightAt(samplePosition.x(), samplePosition.y());
                if (std::isfinite(terrainZ))
                {
                    considerResult(GroundProbeResult{
                        .mPoint = osg::Vec3f(samplePosition.x(), samplePosition.y(), terrainZ),
                        .mNormal = terrainNormalAt(samplePosition.x(), samplePosition.y()),
                        .mMetadata = nullptr,
                        .mFromTerrain = true,
                    },
                        false, centerProbe);
                }
            }
        }

        std::optional<GroundProbeResult> chosen = bestMeshResult.has_value() ? bestMeshResult : bestTerrainResult;
        const std::optional<GroundProbeResult> centerSupport
            = centerMeshResult.has_value() ? centerMeshResult : centerTerrainResult;
        if (!centerSupport.has_value())
            return std::nullopt;

        if (chosen.has_value() && chosen->mFromTerrain != centerSupport->mFromTerrain
            && std::fabs(chosen->mPoint.z() - centerSupport->mPoint.z()) > sMaxStepUp)
        {
            chosen = centerSupport;
        }

        if (previousSupportResult.has_value() && chosen.has_value()
            && std::fabs(previousSupportResult->mPoint.z() - chosen->mPoint.z()) <= sSupportContinuityTolerance)
        {
            chosen = previousSupportResult;
        }

        return chosen;
    }

    void applyTeleportOrientation(const ESM::Position& position)
    {
        if (std::isfinite(position.rot[2]))
            mYaw = position.rot[2];
    }

    void applyTeleportPlacement(const ESM::Position& placement)
    {
        mCameraPosition = placement.asVec3();
        mCameraPosition.z() += sEyeOffset;
        applyTeleportOrientation(placement);
        if (mCollisionEnabled)
        {
            resetGroundSupportHistory();
            groundPlayer();
        }
        updateCameraMatrix();
        updateStatusText();
    }

    void enterExteriorCell(ESM::RefId worldspace, ESM::RefId cellId, const ESM::Position& placement)
    {
        const ESM4::Cell* cell = mContent->findCell(cellId);
        if (cell == nullptr || !cell->isExterior())
            throw std::runtime_error("OpenFO3 could not enter unresolved exterior cell " + cellId.serializeText());

        if (mSceneMode == SceneMode::Exterior && mWorldspace == worldspace && mActiveCell == cellId)
        {
            mFocusedObject = nullptr;
            mFocusSuppressedUntil = mSimulationTime + sPostTravelFocusSuppressSeconds;
            applyTeleportPlacement(placement);
            return;
        }

        clearActiveScene();
        mWorldspace = worldspace;
        createExteriorSceneSystems();
        mSceneMode = SceneMode::Exterior;
        mActiveCell = cellId;
        updateActiveExteriorGrid(osg::Vec2i(cell->mX, cell->mY), false);
        mFocusedObject = nullptr;
        mFocusSuppressedUntil = mSimulationTime + sPostTravelFocusSuppressSeconds;
        applyTeleportPlacement(placement);
    }

    void enterInteriorCell(ESM::RefId cellId, const ESM::Position& placement)
    {
        const ESM4::Cell* cell = mContent->findCell(cellId);
        if (cell == nullptr || cell->isExterior())
            throw std::runtime_error("OpenFO3 could not enter unresolved interior cell " + cellId.serializeText());

        if (mSceneMode == SceneMode::Interior && mActiveCell == cellId)
        {
            mFocusedObject = nullptr;
            mFocusSuppressedUntil = mSimulationTime + sPostTravelFocusSuppressSeconds;
            applyTeleportPlacement(placement);
            return;
        }

        clearActiveScene();
        mSceneMode = SceneMode::Interior;
        mActiveCell = cellId;
        mLoadedInteriorScene.mLoaded = true;
        mLoadedInteriorScene.mCellId = cellId;
        populateInteriorCell(cellId);
        mFocusedObject = nullptr;
        mFocusSuppressedUntil = mSimulationTime + sPostTravelFocusSuppressSeconds;
        applyTeleportPlacement(placement);
    }

    void updateMovement(float dt)
    {
        if (mPanelOpen)
        {
            if (mCollisionEnabled)
                groundPlayer();
            return;
        }

        osg::Vec3f move(0.f, 0.f, 0.f);
        const osg::Vec3f forward = forwardVector();
        const osg::Vec3f right = rightVector();
        const float speed = sWalkSpeed * (mSprint ? sSprintMultiplier : 1.f);

        if (mMoveForward)
            move += mCollisionEnabled ? osg::Vec3f(forward.x(), forward.y(), 0.f) : forward;
        if (mMoveBackward)
            move -= mCollisionEnabled ? osg::Vec3f(forward.x(), forward.y(), 0.f) : forward;
        if (mMoveRight)
            move += right;
        if (mMoveLeft)
            move -= right;
        if (!mCollisionEnabled)
        {
            if (mMoveUp)
                move.z() += 1.f;
            if (mMoveDown)
                move.z() -= 1.f;
        }

        if (move.length2() == 0.f)
        {
            if (mCollisionEnabled)
                groundPlayer();
            return;
        }

        move.normalize();
        osg::Vec3f desired = mCameraPosition + move * speed * dt;
        osg::Vec3f next = desired;
        if (mCollisionEnabled && mCollisionScene != nullptr)
        {
            const float previousFootZ = mCameraPosition.z() - sEyeOffset;
            auto resolveCandidate = [&](float targetX, float targetY)
                -> std::optional<std::pair<osg::Vec3f, GroundProbeResult>> {
                osg::Vec3f candidate = mCollisionScene->sweep(
                    mCameraPosition, osg::Vec3f(targetX, targetY, mCameraPosition.z()), sPlayerRadius);
                if (const std::optional<GroundProbeResult> ground = probeGroundAt(candidate, previousFootZ); ground.has_value())
                {
                    candidate.z() = ground->mPoint.z() + sEyeOffset;
                    return std::make_pair(candidate, *ground);
                }
                return std::nullopt;
            };

            if (const auto resolved = resolveCandidate(desired.x(), desired.y()); resolved.has_value())
            {
                next = resolved->first;
                updateGroundSupportHistory(resolved->second);
            }
            else
            {
                std::optional<std::pair<osg::Vec3f, GroundProbeResult>> bestPartial;
                const auto considerPartial = [&](std::optional<std::pair<osg::Vec3f, GroundProbeResult>>&& partial) {
                    if (!partial.has_value())
                        return;

                    if (!bestPartial.has_value()
                        || (partial->first - mCameraPosition).length2() > (bestPartial->first - mCameraPosition).length2())
                    {
                        bestPartial = std::move(partial);
                    }
                };

                if (std::fabs(desired.x() - mCameraPosition.x()) > 1e-3f)
                    considerPartial(resolveCandidate(desired.x(), mCameraPosition.y()));
                if (std::fabs(desired.y() - mCameraPosition.y()) > 1e-3f)
                    considerPartial(resolveCandidate(mCameraPosition.x(), desired.y()));

                if (bestPartial.has_value())
                {
                    next = bestPartial->first;
                    updateGroundSupportHistory(bestPartial->second);
                }
                else
                {
                    next = mCameraPosition;
                }
            }
        }

        mCameraPosition = next;
    }

    void groundPlayer()
    {
        if (!mCollisionEnabled)
            return;

        const float footZ = mCameraPosition.z() - sEyeOffset;
        if (const std::optional<GroundProbeResult> ground = probeGroundAt(mCameraPosition, footZ); ground.has_value())
        {
            mCameraPosition.z() = ground->mPoint.z() + sEyeOffset;
            updateGroundSupportHistory(ground);
        }
    }

    float terrainHeightAt(float x, float y) const
    {
        if (mTerrain == nullptr)
            return 0.f;
        return mTerrain->getHeightAt(osg::Vec3f(x, y, 0.f));
    }

    osg::Vec3f terrainNormalAt(float x, float y) const
    {
        constexpr float sampleOffset = std::max(16.f, sPlayerRadius * 0.5f);
        const float left = terrainHeightAt(x - sampleOffset, y);
        const float right = terrainHeightAt(x + sampleOffset, y);
        const float down = terrainHeightAt(x, y - sampleOffset);
        const float up = terrainHeightAt(x, y + sampleOffset);

        if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(down) || !std::isfinite(up))
            return osg::Vec3f(0.f, 0.f, 1.f);

        osg::Vec3f normal(left - right, down - up, sampleOffset * 2.f);
        if (normal.length2() <= 1e-6f)
            return osg::Vec3f(0.f, 0.f, 1.f);
        normal.normalize();
        return normal;
    }

    void updateCameraMatrix()
    {
        const osg::Vec3f forward = forwardVector();
        mViewer->getCamera()->setViewMatrix(
            osg::Matrix::lookAt(mCameraPosition, mCameraPosition + forward, osg::Vec3f(0.f, 0.f, 1.f)));
    }

    std::optional<FocusHit> raycastFocusProxy(const osg::Vec3f& from, const osg::Vec3f& to) const
    {
        std::optional<FocusHit> bestHit;
        for (SceneObjectMetadata* metadata : mFocusableObjects)
        {
            if (metadata == nullptr || metadata->mPickedUp || !metadata->mHasFocusProxy)
                continue;

            const std::optional<float> hitDistance
                = raySphereHitDistance(from, to, metadata->mFocusProxyCenter, metadata->mFocusProxyRadius);
            if (!hitDistance.has_value())
                continue;

            const osg::Vec3f delta = to - from;
            const float rayLength = std::max(delta.length(), 1e-6f);
            const osg::Vec3f direction = delta / rayLength;
            const osg::Vec3f hitPoint = from + direction * *hitDistance;

            if (!bestHit.has_value() || *hitDistance < bestHit->mDistance)
                bestHit = FocusHit{ metadata, hitPoint, *hitDistance };
        }

        return bestHit;
    }

    std::optional<FocusHit> raycastFocusedObject(const osg::Vec3f& from, const osg::Vec3f& to) const
    {
        std::optional<FocusHit> bestHit;
        if (mCollisionScene != nullptr)
        {
            if (const std::optional<CollisionScene::RayHit> hit = mCollisionScene->raycast(from, to); hit.has_value())
                bestHit = FocusHit{ hit->mMetadata, hit->mPoint, hit->mDistance };
        }

        if (const std::optional<FocusHit> proxyHit = raycastFocusProxy(from, to);
            proxyHit.has_value() && (!bestHit.has_value() || proxyHit->mDistance < bestHit->mDistance))
        {
            bestHit = proxyHit;
        }

        return bestHit;
    }

    void updateFocus()
    {
        mFocusedObject = nullptr;
        mFocusText->setText("");

        if (mPanelOpen)
            return;
        if (mSimulationTime < mFocusSuppressedUntil)
            return;

        if (const std::optional<FocusHit> hit = raycastFocusedObject(mCameraPosition,
                mCameraPosition + forwardVector() * sLookDistance);
            hit.has_value() && hit->mMetadata != nullptr && !hit->mMetadata->mPickedUp)
        {
            mFocusedObject = hit->mMetadata;
            std::ostringstream prompt;
            prompt << "[E] " << actionLabel(*mFocusedObject);
            mFocusText->setText(prompt.str());
        }
    }

    std::string actionLabel(const SceneObjectMetadata& metadata) const
    {
        if (isLockedForSession(metadata))
        {
            switch (metadata.mKind)
            {
                case ObjectKind::Door:
                case ObjectKind::Container:
                    return "Unlock " + metadata.mName;
                case ObjectKind::Terminal:
                    return "Access " + metadata.mName;
                default:
                    break;
            }
        }

        switch (metadata.mKind)
        {
            case ObjectKind::Door:
                return "Use " + metadata.mName;
            case ObjectKind::Container:
                return "Open " + metadata.mName;
            case ObjectKind::Terminal:
                return "Read " + metadata.mName;
            case ObjectKind::Book:
            case ObjectKind::Note:
                return metadata.mNoTake ? "Read " + metadata.mName : "Take/Read " + metadata.mName;
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Misc:
                return "Take " + metadata.mName;
            case ObjectKind::Weapon:
                return "Take/Inspect " + metadata.mName;
            default:
                return "Inspect " + metadata.mName;
        }
    }

    void activateSceneObject(SceneObjectMetadata& metadata)
    {
        switch (metadata.mKind)
        {
            case ObjectKind::Door:
                activateDoor(metadata);
                break;
            case ObjectKind::Container:
                openContainerPanel(metadata);
                break;
            case ObjectKind::Terminal:
                openTextPanel(PanelMode::Terminal, metadata.mName, buildTerminalText(metadata));
                break;
            case ObjectKind::Book:
            case ObjectKind::Note:
                takeItem(metadata, true);
                break;
            case ObjectKind::Aid:
            case ObjectKind::Ammo:
            case ObjectKind::Armor:
            case ObjectKind::Key:
            case ObjectKind::Light:
            case ObjectKind::Weapon:
            case ObjectKind::Misc:
                takeItem(metadata, false);
                break;
            default:
                openTextPanel(PanelMode::Info, "Object", metadata.mName);
                break;
        }
    }

    void activateFocused()
    {
        mPendingInteract = false;
        if (mFocusedObject == nullptr)
            return;

        if (isLockedForSession(*mFocusedObject))
        {
            const std::vector<AccessPromptAction> actions = buildAccessPromptActions(*mFocusedObject);
            if (actions.size() > 1)
                openAccessPrompt(*mFocusedObject);
            else
                openAccessDeniedPanel(*mFocusedObject);
            return;
        }

        activateSceneObject(*mFocusedObject);
    }

    std::optional<ResolvedDoorDestination> resolveSupportedDoorDestination(
        const SceneObjectMetadata& metadata, std::string& failure) const
    {
        const auto resolveExteriorDestination = [&](ESM::RefId worldspace, const ESM::Position& placement,
                                                    std::optional<ESM::RefId> preferredCellId)
            -> std::optional<ResolvedDoorDestination> {
            ESM::RefId cellId;
            if (preferredCellId.has_value())
            {
                cellId = *preferredCellId;
            }
            else
            {
                const ESM::ExteriorCellLocation targetCell
                    = ESM::positionToExteriorCellLocation(placement.pos[0], placement.pos[1], worldspace);
                const std::optional<ESM::RefId> resolvedCell = mContent->findExteriorCellId(worldspace, targetCell.mX, targetCell.mY);
                if (!resolvedCell.has_value())
                {
                    failure = "This door's exterior destination cell could not be resolved.";
                    return std::nullopt;
                }
                cellId = *resolvedCell;
            }

            return ResolvedDoorDestination{
                .mSceneMode = SceneMode::Exterior,
                .mCellId = cellId,
                .mWorldspace = worldspace,
                .mPlacement = placement,
            };
        };

        if (!metadata.mDoor.destDoor.isZeroOrUnset())
        {
            const ESM4::Reference* target = mContent->findReference(metadata.mDoor.destDoor);
            if (target == nullptr)
            {
                failure = "This door's linked destination could not be resolved.";
                return std::nullopt;
            }

            const ESM4::Cell* targetCell = mContent->findCell(target->mParent);
            if (targetCell == nullptr)
            {
                failure = "This door's destination cell could not be resolved.";
                return std::nullopt;
            }

            if (targetCell->isExterior())
            {
                return resolveExteriorDestination(targetCell->mParent, target->mPos, target->mParent);
            }

            return ResolvedDoorDestination{
                .mSceneMode = SceneMode::Interior,
                .mCellId = target->mParent,
                .mWorldspace = mWorldspace,
                .mPlacement = target->mPos,
            };
        }

        if (!metadata.mDoor.transitionInterior.isZeroOrUnset())
        {
            if (metadata.mDoor.destPos == ESM::Position{})
            {
                failure = "This door's interior transition has no destination position.";
                return std::nullopt;
            }

            const ESM::RefId interiorCellId = ESM::RefId::formIdRefId(metadata.mDoor.transitionInterior);
            const ESM4::Cell* targetCell = mContent->findCell(interiorCellId);
            if (targetCell == nullptr || targetCell->isExterior())
            {
                failure = "This door's interior transition cell could not be resolved.";
                return std::nullopt;
            }

            return ResolvedDoorDestination{
                .mSceneMode = SceneMode::Interior,
                .mCellId = interiorCellId,
                .mWorldspace = mWorldspace,
                .mPlacement = metadata.mDoor.destPos,
            };
        }

        if (metadata.mDoor.destPos == ESM::Position{})
        {
            failure = "This door has no resolved teleport target in the current bootstrap slice.";
            return std::nullopt;
        }

        return resolveExteriorDestination(mWorldspace, metadata.mDoor.destPos, std::nullopt);
    }

    void activateDoor(SceneObjectMetadata& metadata)
    {
        std::string failure;
        if (const std::optional<ResolvedDoorDestination> destination = resolveSupportedDoorDestination(metadata, failure);
            destination.has_value())
        {
            try
            {
                if (destination->mSceneMode == SceneMode::Exterior)
                    enterExteriorCell(destination->mWorldspace, destination->mCellId, destination->mPlacement);
                else
                    enterInteriorCell(destination->mCellId, destination->mPlacement);
            }
            catch (const std::exception& e)
            {
                std::ostringstream stream;
                stream << metadata.mName << "\n\n" << e.what();
                openPanel("Door", stream.str());
            }
            return;
        }

        std::ostringstream stream;
        stream << metadata.mName << "\n\n" << failure;
        openPanel("Door", stream.str());
    }

    std::string buildContainerText(const SceneObjectMetadata& metadata) const
    {
        std::ostringstream stream;
        stream << metadata.mName << "\n\nContained items: " << metadata.mItemCount;
        return stream.str();
    }

    std::string buildTerminalText(const SceneObjectMetadata& metadata) const
    {
        std::ostringstream stream;
        stream << metadata.mName << "\n\n";
        if (!metadata.mText.empty())
            stream << metadata.mText;
        else
            stream << "No terminal text was parsed for this record.";

        if (!metadata.mResultText.empty())
            stream << "\n\nResult:\n" << metadata.mResultText;
        return stream.str();
    }

    void takeItem(SceneObjectMetadata& metadata, bool readable)
    {
        if (metadata.mPickedUp)
            return;
        if (metadata.mNoTake)
        {
            const InventoryStack stack = buildInventoryStackForWorldObject(metadata, 1);
            if (readable && stack.mReadable)
                openReadPanel(stack);
            else
                openTextPanel(PanelMode::Info, metadata.mName, metadata.mText.empty() ? metadata.mName : metadata.mText);
            return;
        }

        if (mFocusedObject == &metadata)
            mFocusedObject = nullptr;
        setObjectFocusable(metadata, false);
        metadata.mPickedUp = true;
        mPickedUpReferenceIds.insert(metadata.mReferenceId);
        if (metadata.mNode != nullptr && metadata.mNode->getNumParents() > 0)
            metadata.mNode->getParent(0)->removeChild(metadata.mNode);
        metadata.mNode = nullptr;
        metadata.mHasFocusProxy = false;
        if (mCollisionScene != nullptr)
            mCollisionScene->remove(metadata);

        const InventoryStack stack = buildInventoryStackForWorldObject(metadata, 1);
        addToInventory(stack);

        if (readable && stack.mReadable)
        {
            openReadPanel(stack);
            return;
        }

        std::ostringstream stream;
        stream << metadata.mName << "\n\nPicked up into the local OpenFO3 prototype inventory."
               << "\nInventory items: " << inventoryItemCount();
        openTextPanel(PanelMode::Info, "Inventory", stream.str());
    }

    void openPanel(std::string_view title, const std::string& body)
    {
        openTextPanel(PanelMode::Info, title, body);
    }

    void closePanel()
    {
        clearMovementState();
        mPanelMode = PanelMode::None;
        mPanelTitle.clear();
        mPanelTextLines.clear();
        mPanelTargetReferenceId = {};
        mPanelSelectionIndex = 0;
        mPanelScrollOffset = 0;
        if (mPanelTitleText != nullptr)
            mPanelTitleText->setText("");
        if (mPanelBodyText != nullptr)
            mPanelBodyText->setText("");
        if (mPanelFooterText != nullptr)
            mPanelFooterText->setText("");
        if (mPanelRoot != nullptr)
            mPanelRoot->setNodeMask(0u);
        mPanelOpen = false;
        if (mGrabMouse)
            applyMouseMode(true);
    }

    void updateStatusText()
    {
        std::ostringstream stream;
        stream << "OpenFO3 prototype\n"
               << "Cell: " << mActiveCell.serializeText() << "\n"
               << "Collision: " << (mCollisionEnabled ? "on" : "noclip") << "\n"
               << "Move: WASD  Look: mouse";
        if (!mGrabMouse)
            stream << " / hold RMB";
        stream << "\nUp/Down: Space/Ctrl  Toggle collision: C  Interact: E  Inventory: I\n"
               << "Inventory: " << inventoryItemCount();
        mStatusText->setText(stream.str());
    }

    void updateOverlayLayout(int width, int height)
    {
        mOverlayCamera->setProjectionMatrix(osg::Matrix::ortho2D(0.0, width, 0.0, height));
        mOverlayCamera->setViewMatrix(osg::Matrix::identity());

        mStatusText->setPosition(osg::Vec3(20.f, height - 110.f, 0.f));
        mFocusText->setAlignment(osgText::TextBase::CENTER_CENTER);
        mFocusText->setPosition(osg::Vec3(width * 0.5f, 60.f, 0.f));

        const float panelWidth = std::min(960.f, std::max(420.f, static_cast<float>(width) - 180.f));
        const float panelHeight = std::min(380.f, std::max(220.f, static_cast<float>(height) - 200.f));
        const float panelLeft = (static_cast<float>(width) - panelWidth) * 0.5f;
        const float panelBottom = (static_cast<float>(height) - panelHeight) * 0.5f;
        const float panelRight = panelLeft + panelWidth;
        const float panelTop = panelBottom + panelHeight;

        (*mPanelFrameBackgroundVertices)[0].set(panelLeft, panelBottom, 0.f);
        (*mPanelFrameBackgroundVertices)[1].set(panelRight, panelBottom, 0.f);
        (*mPanelFrameBackgroundVertices)[2].set(panelRight, panelTop, 0.f);
        (*mPanelFrameBackgroundVertices)[3].set(panelLeft, panelTop, 0.f);
        mPanelFrameBackgroundVertices->dirty();

        (*mPanelFrameOutlineVertices)[0].set(panelLeft, panelBottom, 0.f);
        (*mPanelFrameOutlineVertices)[1].set(panelRight, panelBottom, 0.f);
        (*mPanelFrameOutlineVertices)[2].set(panelRight, panelTop, 0.f);
        (*mPanelFrameOutlineVertices)[3].set(panelLeft, panelTop, 0.f);
        (*mPanelFrameOutlineVertices)[4].set(panelLeft, panelBottom, 0.f);
        mPanelFrameOutlineVertices->dirty();

        const float textLeft = panelLeft + 28.f;
        const float textTop = panelTop - 34.f;
        mPanelTitleText->setAlignment(osgText::TextBase::LEFT_TOP);
        mPanelTitleText->setPosition(osg::Vec3(textLeft, textTop, 0.f));

        mPanelBodyText->setAlignment(osgText::TextBase::LEFT_TOP);
        mPanelBodyText->setMaximumWidth(panelWidth - 56.f);
        mPanelBodyText->setPosition(osg::Vec3(textLeft, textTop - 48.f, 0.f));

        mPanelFooterText->setAlignment(osgText::TextBase::LEFT_BOTTOM);
        mPanelFooterText->setPosition(osg::Vec3(textLeft, panelBottom + 22.f, 0.f));
    }

    Files::ConfigurationManager& mCfgMgr;

    bool mSdlInitialized = false;
    SDL_Window* mWindow = nullptr;
    int mWindowWidth = 1600;
    int mWindowHeight = 900;

    Files::PathContainer mDataDirs;
    Files::Collections mFileCollections;
    std::vector<std::string> mArchives;
    std::filesystem::path mResDir;
    std::vector<std::string> mContentFiles;
    ToUTF8::FromType mEncoding = ToUTF8::WINDOWS_1252;
    std::unique_ptr<ToUTF8::Utf8Encoder> mEncoder;
    bool mGrabMouse = true;
    bool mMouseLookActive = true;

    osg::ref_ptr<osgViewer::Viewer> mViewer;
    osg::ref_ptr<SceneUtil::LightManager> mLightRoot;
    osg::ref_ptr<osg::Group> mSceneRoot;
    osg::ref_ptr<osg::Group> mWorldRoot;
    osg::ref_ptr<osg::Camera> mOverlayCamera;
    osg::ref_ptr<osgText::Text> mStatusText;
    osg::ref_ptr<osgText::Text> mFocusText;
    osg::ref_ptr<osg::Group> mPanelRoot;
    osg::ref_ptr<osg::Geode> mPanelFrameGeode;
    osg::ref_ptr<osg::Geometry> mPanelFrameBackground;
    osg::ref_ptr<osg::Vec3Array> mPanelFrameBackgroundVertices;
    osg::ref_ptr<osg::Geometry> mPanelFrameOutline;
    osg::ref_ptr<osg::Vec3Array> mPanelFrameOutlineVertices;
    osg::ref_ptr<osg::Geode> mPanelTextGeode;
    osg::ref_ptr<osgText::Text> mPanelTitleText;
    osg::ref_ptr<osgText::Text> mPanelBodyText;
    osg::ref_ptr<osgText::Text> mPanelFooterText;

    std::unique_ptr<VFS::Manager> mVFS;
    std::unique_ptr<Resource::ResourceSystem> mResourceSystem;
    std::unique_ptr<Resource::BulletShapeManager> mBulletShapeManager;
    std::unique_ptr<LoadedContent> mContent;
    std::unique_ptr<Fo3TerrainStorage> mTerrainStorage;
    std::unique_ptr<Terrain::World> mTerrain;
    std::unique_ptr<CollisionScene> mCollisionScene;
    osg::ref_ptr<SceneUtil::StateUpdater> mStateUpdater;
    osg::ref_ptr<SceneUtil::SharedUniformStateUpdater> mSharedUniformStateUpdater;
    osg::ref_ptr<SceneUtil::PerViewUniformStateUpdater> mPerViewUniformStateUpdater;

    std::map<ExteriorBucketKey, CellSceneStats> mBucketSceneStats;
    std::map<ExteriorBucketKey, LoadedObjectBucket> mLoadedObjectBuckets;
    std::set<std::pair<int, int>> mLoadedTerrainCells;
    LoadedInteriorScene mLoadedInteriorScene;
    std::vector<std::unique_ptr<SceneObjectMetadata>> mObjects;
    std::unordered_map<ESM::FormId, SceneObjectMetadata*> mObjectByReferenceId;
    std::vector<SceneObjectMetadata*> mFocusableObjects;
    std::unordered_map<std::string, NodeRenderStats> mMeshRenderStatsCache;
    std::unordered_map<ESM::FormId, std::vector<InventoryStack>> mContainerStates;
    std::unordered_set<ESM::FormId> mPickedUpReferenceIds;
    std::unordered_set<ESM::FormId> mUnlockedReferenceIds;
    std::unordered_map<ESM::FormId, InventoryStack> mInventory;
    PlayerCapabilities mPlayerCapabilities;

    ESM::RefId mWorldspace;
    ESM::RefId mActiveCell;
    osg::Vec2i mCurrentObjectGridCenter = osg::Vec2i(0, 0);
    osg::Vec2i mCurrentTerrainGridCenter = osg::Vec2i(0, 0);
    osg::Vec3f mCameraPosition = osg::Vec3f(0.f, 0.f, 0.f);
    float mViewDistance = 0.f;
    float mYaw = 0.f;
    float mPitch = 0.f;
    int mTerrainHalfGridSize = Constants::ESM4CellGridRadius;
    int mLandmarkHalfGridSize = Constants::ESM4CellGridRadius;
    int mFarVisualHalfGridSize = Constants::ESM4CellGridRadius;
    int mNearFullHalfGridSize = 1;
    int mInteractionHalfGridSize = 1;
    SceneMode mSceneMode = SceneMode::Exterior;
    SceneObjectMetadata* mFocusedObject = nullptr;
    PanelMode mPanelMode = PanelMode::None;
    std::string mPanelTitle;
    std::vector<std::string> mPanelTextLines;
    ESM::FormId mPanelTargetReferenceId;
    int mPanelSelectionIndex = 0;
    int mPanelScrollOffset = 0;
    float mSimulationTime = 0.f;
    float mFocusSuppressedUntil = 0.f;

    bool mMoveForward = false;
    bool mMoveBackward = false;
    bool mMoveLeft = false;
    bool mMoveRight = false;
    bool mMoveUp = false;
    bool mMoveDown = false;
    bool mSprint = false;
    bool mPendingInteract = false;
    bool mCollisionEnabled = false;
    bool mPanelOpen = false;
    bool mObjectGridCenterInitialized = false;
    bool mTerrainGridCenterInitialized = false;
    bool mUseDistantTerrain = false;
    bool mHasLastGroundSupport = false;
    bool mLastGroundSupportFromTerrain = false;
    bool mQuit = false;
    ESM::FormId mLastGroundSupportReferenceId;
    float mLastGroundSupportZ = 0.f;
};

OpenFO3::Engine::Engine(Files::ConfigurationManager& configurationManager)
    : mImpl(std::make_unique<Impl>(configurationManager))
{
}

OpenFO3::Engine::~Engine() = default;

void OpenFO3::Engine::setDataDirs(const Files::PathContainer& dataDirs)
{
    mImpl->setDataDirs(dataDirs);
}

void OpenFO3::Engine::addArchive(const std::string& archive)
{
    mImpl->addArchive(archive);
}

void OpenFO3::Engine::setResourceDir(const std::filesystem::path& parResDir)
{
    mImpl->setResourceDir(parResDir);
}

void OpenFO3::Engine::addContentFile(const std::string& file)
{
    mImpl->addContentFile(file);
}

void OpenFO3::Engine::setEncoding(const ToUTF8::FromType& encoding)
{
    mImpl->setEncoding(encoding);
}

void OpenFO3::Engine::setGrabMouse(bool grab)
{
    mImpl->setGrabMouse(grab);
}

void OpenFO3::Engine::go()
{
    mImpl->go();
}
