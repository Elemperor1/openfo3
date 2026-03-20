#include "engine.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/exteriorcelllocation.hpp>
#include <components/esm/refid.hpp>
#include <components/esm/util.hpp>
#include <components/esm4/loadacti.hpp>
#include <components/esm4/loadbook.hpp>
#include <components/esm4/loadcell.hpp>
#include <components/esm4/loadcont.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadland.hpp>
#include <components/esm4/loadltex.hpp>
#include <components/esm4/loadmisc.hpp>
#include <components/esm4/loadmstt.hpp>
#include <components/esm4/loadnote.hpp>
#include <components/esm4/loadrefr.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/loadterm.hpp>
#include <components/esm4/loadtxst.hpp>
#include <components/esm4/loadwrld.hpp>
#include <components/esm4/readerutils.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/constrainedfilestream.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/osguservalues.hpp>
#include <components/misc/resourcehelpers.hpp>
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
    constexpr float sBootstrapHoverHeight = 24.f;
    constexpr float sBootstrapTargetLift = 36.f;
    constexpr float sCellLoadingThreshold = 1024.f;
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
            case ObjectKind::Misc:
                return true;
            case ObjectKind::Static:
            case ObjectKind::MovableStatic:
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
        int mItemCount = 0;
    };

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

    struct BootstrapCell
    {
        ESM::RefId mId;
        const ESM4::Cell* mCell = nullptr;
    };

    struct SceneObjectMetadata
    {
        ObjectKind mKind = ObjectKind::Static;
        ESM::FormId mBaseId;
        ESM::FormId mReferenceId;
        ESM::RefId mCellId;
        std::string mName;
        std::string mEditorId;
        std::string mModelPath;
        std::string mText;
        std::string mResultText;
        int mItemCount = 0;
        ESM4::TeleportDest mDoor;
        ESM::Position mTransform;
        float mScale = 1.f;
        osg::Vec3f mBoundCenter = osg::Vec3f(0.f, 0.f, 0.f);
        float mBoundRadius = 0.f;
        bool mRenderable = false;
        bool mEffectOnly = false;
        osg::Vec3f mFocusProxyCenter = osg::Vec3f(0.f, 0.f, 0.f);
        float mFocusProxyRadius = 0.f;
        bool mHasFocusProxy = false;
        SceneUtil::PositionAttitudeTransform* mNode = nullptr;
        btCollisionObject* mCollisionObject = nullptr;
        bool mPickedUp = false;
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
            int bestInteractive = -1;
            int bestRenderable = -1;

            for (const auto& [cellId, cell] : mCells)
            {
                if (!cell.isExterior() || !hasLand(cell.mParent, cell.mX, cell.mY))
                    continue;

                if (const auto* refs = getReferences(cellId); refs != nullptr)
                {
                    int interactiveScore = 0;
                    int renderableRefs = 0;
                    for (const auto& [refId, ref] : *refs)
                    {
                        (void)refId;
                        const BaseRecordData* base = findBase(ref.mBaseObj);
                        if (base == nullptr)
                            continue;

                        interactiveScore += bootstrapScoreForKind(base->mKind);
                        if (!base->mModel.empty())
                            ++renderableRefs;
                    }

                    if (interactiveScore == 0 && renderableRefs == 0)
                        continue;

                    if (!bestCell.has_value() || interactiveScore > bestInteractive
                        || (interactiveScore == bestInteractive && renderableRefs > bestRenderable))
                    {
                        bestCell = BootstrapCell{ cellId, &cell };
                        bestInteractive = interactiveScore;
                        bestRenderable = renderableRefs;
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
                        .mModel = record.mModel };
                    return true;
                }
                case ESM4::REC_MSTT:
                {
                    ESM4::MovableStatic record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::MovableStatic,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel };
                    return true;
                }
                case ESM4::REC_DOOR:
                {
                    ESM4::Door record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Door,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel };
                    return true;
                }
                case ESM4::REC_CONT:
                {
                    ESM4::Container record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Container,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mItemCount = static_cast<int>(record.mInventory.size()) };
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
                        .mText = record.mText,
                        .mResultText = record.mResultText };
                    return true;
                }
                case ESM4::REC_BOOK:
                {
                    ESM4::Book record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Book,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel,
                        .mText = record.mText };
                    return true;
                }
                case ESM4::REC_NOTE:
                {
                    ESM4::Note record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Note,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel };
                    return true;
                }
                case ESM4::REC_MISC:
                {
                    ESM4::MiscItem record;
                    record.load(reader);
                    mBaseRecords[record.mId] = BaseRecordData{ .mKind = ObjectKind::Misc,
                        .mEditorId = record.mEditorId,
                        .mFullName = record.mFullName,
                        .mModel = record.mModel };
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
            mWorld.addCollisionObject(object.get());

            mShapes.push_back(std::move(shape));
            mObjects.push_back(std::move(object));
        }

        void remove(SceneObjectMetadata& metadata)
        {
            if (metadata.mCollisionObject == nullptr)
                return;

            mWorld.removeCollisionObject(metadata.mCollisionObject);
            metadata.mCollisionObject = nullptr;
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
            return RayHit{ metadata, point, (point - from).length() };
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
        btDefaultCollisionConfiguration mConfiguration;
        btCollisionDispatcher mDispatcher;
        btDbvtBroadphase mBroadphase;
        btCollisionWorld mWorld;
        std::vector<osg::ref_ptr<Resource::BulletShapeInstance>> mShapes;
        std::vector<std::unique_ptr<btCollisionObject>> mObjects;
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
        mSceneRoot->addChild(mPanelRoot);
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

        const float cellSize = static_cast<float>(ESM::getCellSize(mWorldspace));
        mViewDistance = std::max(Settings::camera().mViewingDistance.get(), cellSize);
        mHalfGridSize = std::max(Constants::ESM4CellGridRadius,
            static_cast<int>(std::ceil(mViewDistance / cellSize)));
        mUseDistantTerrain = Settings::terrain().mDistantTerrain;
        Log(Debug::Info) << "OpenFO3 streaming config: viewingDistance=" << mViewDistance << " halfGridSize="
                         << mHalfGridSize << " distantTerrain=" << (mUseDistantTerrain ? 1 : 0);

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
        mCellSceneStats.clear();
        mLoadedTerrainCells.clear();
        mLoadedSceneCells.clear();
        mCollisionScene = std::make_unique<CollisionScene>();

        const int centerX = bootstrap->mCell->mX;
        const int centerY = bootstrap->mCell->mY;
        updateActiveExteriorGrid(osg::Vec2i(centerX, centerY), true);
        initializeBootstrapCamera(centerX, centerY, cellSize);
        updateCameraMatrix();
        updateStatusText();
    }

    osg::Vec4i gridCenterToBounds(const osg::Vec2i& centerCell) const
    {
        return osg::Vec4i(centerCell.x() - mHalfGridSize, centerCell.y() - mHalfGridSize,
            centerCell.x() + mHalfGridSize + 1, centerCell.y() + mHalfGridSize + 1);
    }

    osg::Vec2i getNewGridCenterForCamera(const osg::Vec3f& position) const
    {
        if (mGridCenterInitialized)
        {
            const osg::Vec2f center = ESM::indexToPosition(
                ESM::ExteriorCellLocation(mCurrentGridCenter.x(), mCurrentGridCenter.y(), mWorldspace), true);
            const float distance = std::max(std::abs(center.x() - position.x()), std::abs(center.y() - position.y()));
            const float maxDistance = static_cast<float>(ESM::getCellSize(mWorldspace)) * 0.5f + sCellLoadingThreshold;
            if (distance <= maxDistance)
                return mCurrentGridCenter;
        }

        const ESM::ExteriorCellLocation cellPos
            = ESM::positionToExteriorCellLocation(position.x(), position.y(), mWorldspace);
        return osg::Vec2i(cellPos.mX, cellPos.mY);
    }

    void unloadCellScene(ESM::RefId cellId)
    {
        auto sceneCellIt = std::find_if(mLoadedSceneCells.begin(), mLoadedSceneCells.end(),
            [&](const auto& entry) { return entry.second.mCellId == cellId; });
        if (sceneCellIt != mLoadedSceneCells.end())
            mLoadedSceneCells.erase(sceneCellIt);

        if (const auto statsIt = mCellSceneStats.find(cellId); statsIt != mCellSceneStats.end())
        {
            if (statsIt->second.mRoot != nullptr && statsIt->second.mRoot->getNumParents() > 0)
                statsIt->second.mRoot->getParent(0)->removeChild(statsIt->second.mRoot);
            mCellSceneStats.erase(statsIt);
        }

        mObjects.erase(std::remove_if(mObjects.begin(), mObjects.end(),
                           [&](const std::unique_ptr<SceneObjectMetadata>& metadata) {
                               if (metadata == nullptr || metadata->mCellId != cellId)
                                   return false;

                               if (mFocusedObject == metadata.get())
                                   mFocusedObject = nullptr;

                               if (mCollisionScene != nullptr)
                                   mCollisionScene->remove(*metadata);
                               if (metadata->mNode != nullptr && metadata->mNode->getNumParents() > 0)
                                   metadata->mNode->getParent(0)->removeChild(metadata->mNode);
                               return true;
                           }),
            mObjects.end());
    }

    void updateActiveExteriorGrid(const osg::Vec2i& centerCell, bool bootstrap)
    {
        if (mGridCenterInitialized && centerCell == mCurrentGridCenter)
            return;

        const auto withinGrid = [&](const std::pair<int, int>& cell) {
            return std::abs(cell.first - centerCell.x()) <= mHalfGridSize
                && std::abs(cell.second - centerCell.y()) <= mHalfGridSize;
        };

        if (mTerrain != nullptr)
            mTerrain->setActiveGrid(gridCenterToBounds(centerCell));

        int unloadedTerrainCells = 0;
        for (auto it = mLoadedTerrainCells.begin(); it != mLoadedTerrainCells.end();)
        {
            if (withinGrid(*it))
            {
                ++it;
                continue;
            }

            if (mTerrain != nullptr)
                mTerrain->unloadCell(it->first, it->second);
            it = mLoadedTerrainCells.erase(it);
            ++unloadedTerrainCells;
        }

        int unloadedSceneCells = 0;
        for (auto it = mLoadedSceneCells.begin(); it != mLoadedSceneCells.end();)
        {
            if (withinGrid(it->first))
            {
                ++it;
                continue;
            }

            const ESM::RefId cellId = it->second.mCellId;
            ++it;
            unloadCellScene(cellId);
            ++unloadedSceneCells;
        }

        int loadedTerrainCells = 0;
        int loadedSceneCells = 0;
        for (int dy = -mHalfGridSize; dy <= mHalfGridSize; ++dy)
        {
            for (int dx = -mHalfGridSize; dx <= mHalfGridSize; ++dx)
            {
                const std::pair<int, int> coord(centerCell.x() + dx, centerCell.y() + dy);
                if (mContent->hasLand(mWorldspace, coord.first, coord.second)
                    && !mLoadedTerrainCells.contains(coord))
                {
                    mTerrain->loadCell(coord.first, coord.second);
                    mLoadedTerrainCells.insert(coord);
                    ++loadedTerrainCells;
                }

                const std::optional<ESM::RefId> cellId
                    = mContent->findExteriorCellId(mWorldspace, coord.first, coord.second);
                if (!cellId.has_value())
                    continue;

                const bool shouldHaveNonInteractiveCollision = (dx == 0 && dy == 0);
                const auto loadedSceneIt = mLoadedSceneCells.find(coord);
                const bool needsReloadForCollision = loadedSceneIt != mLoadedSceneCells.end()
                    && shouldHaveNonInteractiveCollision && !loadedSceneIt->second.mHasNonInteractiveCollision;
                if (needsReloadForCollision)
                    unloadCellScene(loadedSceneIt->second.mCellId);

                if (!mLoadedSceneCells.contains(coord))
                {
                    populateCellScene(*cellId, shouldHaveNonInteractiveCollision);
                    mLoadedSceneCells[coord] = LoadedExteriorCell{ .mCellId = *cellId,
                        .mHasNonInteractiveCollision = shouldHaveNonInteractiveCollision };
                    ++loadedSceneCells;
                }
            }
        }

        mCurrentGridCenter = centerCell;
        mGridCenterInitialized = true;
        if (const std::optional<ESM::RefId> activeCell = mContent->findExteriorCellId(mWorldspace, centerCell.x(), centerCell.y());
            activeCell.has_value())
        {
            mActiveCell = *activeCell;
        }

        if (bootstrap)
        {
            Log(Debug::Info) << "OpenFO3 loaded " << mLoadedTerrainCells.size() << " terrain cells around bootstrap cell";
            Log(Debug::Info) << "OpenFO3 populated render refs for " << mLoadedSceneCells.size()
                             << " exterior cells around bootstrap cell";
        }
        else
        {
            Log(Debug::Info) << "OpenFO3 updated active grid center=(" << centerCell.x() << ", " << centerCell.y()
                             << ") radius=" << mHalfGridSize << " loadedTerrain=" << loadedTerrainCells
                             << " unloadedTerrain=" << unloadedTerrainCells << " loadedSceneCells="
                             << loadedSceneCells << " unloadedSceneCells=" << unloadedSceneCells;
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

        for (const auto& [cellId, stats] : mCellSceneStats)
        {
            (void)cellId;
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
            const auto cellStatsIt = mCellSceneStats.find(metadata->mCellId);
            if (cellStatsIt != mCellSceneStats.end() && cellStatsIt->second.mSuspiciousBounds)
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
            const auto cellStatsIt = mCellSceneStats.find(metadata->mCellId);
            if (cellStatsIt != mCellSceneStats.end() && cellStatsIt->second.mSuspiciousBounds)
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

    osg::Group* ensureCellRoot(ESM::RefId cellId)
    {
        CellSceneStats& stats = mCellSceneStats[cellId];
        if (stats.mRoot == nullptr)
        {
            stats.mRoot = new osg::Group;
            stats.mRoot->setName("Exterior Cell Root " + cellId.serializeText());
            stats.mRoot->setNodeMask(sObjectNodeMask);
            mWorldRoot->addChild(stats.mRoot);
        }
        return stats.mRoot.get();
    }

    void updateCellSceneStats(ESM::RefId cellId, std::size_t refCount, std::size_t instantiated,
        std::size_t collisionShapes, std::size_t renderableCount, std::size_t effectOnlyCount,
        std::size_t skippedMissingBase, std::size_t skippedMissingModel,
        const std::optional<osg::BoundingSphere>& populatedBounds)
    {
        auto it = mCellSceneStats.find(cellId);
        if (it == mCellSceneStats.end() || it->second.mRoot == nullptr)
            return;

        CellSceneStats& stats = it->second;
        stats.mInstantiated = instantiated;
        stats.mCollisionCount = collisionShapes;
        stats.mRenderableCount = renderableCount;
        stats.mEffectOnlyCount = effectOnlyCount;

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
        stream << "OpenFO3 populated cell " << cellId.serializeText() << " refs=" << refCount
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

        const ESM4::Cell* cell = mContent->findCell(cellId);
        if (cell == nullptr || !cell->isExterior() || !stats.mHasBounds)
            return;

        const float cellSize = static_cast<float>(ESM::getCellSize(cell->mParent));
        const osg::Vec3f expectedCenter((cell->mX + 0.5f) * cellSize, (cell->mY + 0.5f) * cellSize, center.z());
        const float horizontalOffset = (osg::Vec2f(center.x(), center.y()) - osg::Vec2f(expectedCenter.x(), expectedCenter.y())).length();
        const float suspiciousDistance = cellSize * 1.6f;
        if (horizontalOffset > suspiciousDistance)
        {
            stats.mSuspiciousBounds = true;
            Log(Debug::Warning) << "OpenFO3 suspicious bounds for cell " << cellId.serializeText()
                                << " expectedCenter=(" << expectedCenter.x() << ", " << expectedCenter.y()
                                << ") boundsCenter=(" << center.x() << ", " << center.y()
                                << ") horizontalOffset=" << horizontalOffset;
        }
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

    void populateCellScene(ESM::RefId cellId, bool allowNonInteractiveCollision)
    {
        const auto* refs = mContent->getReferences(cellId);
        if (refs == nullptr)
        {
            Log(Debug::Warning) << "OpenFO3 found no references for cell " << cellId.serializeText();
            return;
        }

        osg::Group* const cellRoot = ensureCellRoot(cellId);

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
            const NodeRenderStats renderStats = analyzeSceneNode(*instance, false, true);
            if (instance->getNodeMask() == 0u && renderStats.hasRenderableGeometry())
                Log(Debug::Warning) << "OpenFO3 invisible instance root remained hidden for " << meshPath << " ref="
                                    << ESM::RefId(ref.mId).serializeText();

            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> transform = new SceneUtil::PositionAttitudeTransform;
            transform->setPosition(ref.mPos.asVec3());
            transform->setAttitude(Misc::Convert::makeOsgQuat(ref.mPos));
            transform->setScale(osg::Vec3f(ref.mScale, ref.mScale, ref.mScale));
            transform->setNodeMask(sObjectNodeMask);
            transform->addChild(instance);
            cellRoot->addChild(transform);

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
            metadata->mItemCount = base->mItemCount;
            metadata->mDoor = ref.mDoor;
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
                metadata->mFocusProxyCenter = metadata->mRenderable ? metadata->mBoundCenter : ref.mPos.asVec3();
                const float focusRadius = metadata->mBoundRadius > 0.f ? metadata->mBoundRadius : 1.f;
                metadata->mFocusProxyRadius = std::max(
                    std::max(focusRadius * std::max(ref.mScale, 1.f), 1.f), sFocusProxyRadiusMin);
            }

            if (mCollisionScene != nullptr && (isInteractiveKind(base->mKind) || allowNonInteractiveCollision))
            {
                mCollisionScene->addStatic(*mBulletShapeManager, meshPath, ref.mPos, ref.mScale, *metadata);
                if (metadata->mCollisionObject != nullptr)
                    ++collisionShapes;
            }
            mObjects.push_back(std::move(metadata));
            ++instantiated;
        }

        std::optional<osg::BoundingSphere> populatedBounds;
        if (hasPopulatedBounds)
        {
            const osg::Vec3f center = (populatedMin + populatedMax) * 0.5f;
            const float radius = std::max((populatedMax - center).length(), 1.f);
            populatedBounds = osg::BoundingSphere(center, radius);
        }

        updateCellSceneStats(cellId, refs->size(), instantiated, collisionShapes, renderableCount, effectOnlyCount,
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

    void handleKey(SDL_Keycode key, bool pressed)
    {
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
            case SDLK_c:
                if (pressed)
                    mCollisionEnabled = !mCollisionEnabled;
                break;
            case SDLK_ESCAPE:
                if (pressed)
                {
                    if (mPanelOpen)
                        closePanel();
                    else
                        mQuit = true;
                }
                break;
            default:
                break;
        }
    }

    void updateSimulation(float dt)
    {
        updateMovement(dt);
        updateActiveExteriorGrid(getNewGridCenterForCamera(mCameraPosition), false);
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

    void updateMovement(float dt)
    {
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
        osg::Vec3f next = mCameraPosition + move * speed * dt;
        if (mCollisionEnabled && mCollisionScene != nullptr)
        {
            next = mCollisionScene->sweep(mCameraPosition, osg::Vec3f(next.x(), next.y(), mCameraPosition.z()), sPlayerRadius);
            const float ground = terrainHeightAt(next.x(), next.y());
            next.z() = ground + sEyeOffset;
        }

        mCameraPosition = next;
    }

    void groundPlayer()
    {
        if (!mCollisionEnabled)
            return;

        mCameraPosition.z() = terrainHeightAt(mCameraPosition.x(), mCameraPosition.y()) + sEyeOffset;
    }

    float terrainHeightAt(float x, float y) const
    {
        if (mTerrain == nullptr)
            return 0.f;
        return mTerrain->getHeightAt(osg::Vec3f(x, y, 0.f));
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
        for (const auto& metadata : mObjects)
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
                bestHit = FocusHit{ metadata.get(), hitPoint, *hitDistance };
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
                return "Take/Read " + metadata.mName;
            case ObjectKind::Misc:
                return "Take " + metadata.mName;
            default:
                return "Inspect " + metadata.mName;
        }
    }

    void activateFocused()
    {
        if (mPanelOpen)
        {
            closePanel();
            return;
        }

        if (mFocusedObject == nullptr)
            return;

        switch (mFocusedObject->mKind)
        {
            case ObjectKind::Door:
                activateDoor(*mFocusedObject);
                break;
            case ObjectKind::Container:
                openPanel("Container", buildContainerText(*mFocusedObject));
                break;
            case ObjectKind::Terminal:
                openPanel("Terminal", buildTerminalText(*mFocusedObject));
                break;
            case ObjectKind::Book:
            case ObjectKind::Note:
                takeItem(*mFocusedObject, true);
                break;
            case ObjectKind::Misc:
                takeItem(*mFocusedObject, false);
                break;
            default:
                openPanel("Object", mFocusedObject->mName);
                break;
        }
    }

    void activateDoor(SceneObjectMetadata& metadata)
    {
        if (metadata.mDoor.destPos != ESM::Position{})
        {
            mCameraPosition = metadata.mDoor.destPos.asVec3();
            mCameraPosition.z() += sEyeOffset;
            return;
        }

        if (!metadata.mDoor.destDoor.isZeroOrUnset())
        {
            if (const ESM4::Reference* target = mContent->findReference(metadata.mDoor.destDoor); target != nullptr)
            {
                mCameraPosition = target->mPos.asVec3();
                mCameraPosition.z() += sEyeOffset;
                return;
            }
        }

        std::ostringstream stream;
        stream << metadata.mName << "\n\nThis door has no resolved teleport target in the current bootstrap slice.";
        openPanel("Door", stream.str());
    }

    std::string buildContainerText(const SceneObjectMetadata& metadata) const
    {
        std::ostringstream stream;
        stream << metadata.mName << "\n\nContained item entries: " << metadata.mItemCount
               << "\n\nThis is a read-only proof-of-activation slice.";
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

        metadata.mPickedUp = true;
        if (metadata.mNode != nullptr && metadata.mNode->getNumParents() > 0)
            metadata.mNode->getParent(0)->removeChild(metadata.mNode);
        if (mCollisionScene != nullptr)
            mCollisionScene->remove(metadata);

        mInventory.push_back(metadata.mName);

        if (readable && !metadata.mText.empty())
        {
            openPanel(metadata.mName, metadata.mText);
            return;
        }

        std::ostringstream stream;
        stream << metadata.mName << "\n\nPicked up into the local OpenFO3 prototype inventory."
               << "\nInventory size: " << mInventory.size();
        openPanel("Inventory", stream.str());
    }

    void openPanel(std::string_view title, const std::string& body)
    {
        mPanelTitleText->setText(std::string(title));
        mPanelBodyText->setText(body);
        mPanelFooterText->setText("[Esc] close   [E] toggle panel");
        if (mPanelRoot != nullptr)
            mPanelRoot->setNodeMask(~0u);
        mPanelOpen = true;
    }

    void closePanel()
    {
        mPanelTitleText->setText("");
        mPanelBodyText->setText("");
        mPanelFooterText->setText("");
        if (mPanelRoot != nullptr)
            mPanelRoot->setNodeMask(0u);
        mPanelOpen = false;
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
        stream << "\nUp/Down: Space/Ctrl  Toggle collision: C  Interact: E\n"
               << "Inventory: " << mInventory.size();
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

    struct LoadedExteriorCell
    {
        ESM::RefId mCellId;
        bool mHasNonInteractiveCollision = false;
    };

    std::map<ESM::RefId, CellSceneStats> mCellSceneStats;
    std::map<std::pair<int, int>, LoadedExteriorCell> mLoadedSceneCells;
    std::set<std::pair<int, int>> mLoadedTerrainCells;
    std::vector<std::unique_ptr<SceneObjectMetadata>> mObjects;
    std::vector<std::string> mInventory;

    ESM::RefId mWorldspace;
    ESM::RefId mActiveCell;
    osg::Vec2i mCurrentGridCenter = osg::Vec2i(0, 0);
    osg::Vec3f mCameraPosition = osg::Vec3f(0.f, 0.f, 0.f);
    float mViewDistance = 0.f;
    float mYaw = 0.f;
    float mPitch = 0.f;
    int mHalfGridSize = Constants::ESM4CellGridRadius;
    SceneObjectMetadata* mFocusedObject = nullptr;

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
    bool mGridCenterInitialized = false;
    bool mUseDistantTerrain = false;
    bool mQuit = false;
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
