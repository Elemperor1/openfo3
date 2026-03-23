#include <components/esm/refid.hpp>
#include <components/esmterrain/storage.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>

#include <osg/Image>

namespace
{
    class TestStorage : public ESMTerrain::Storage
    {
    public:
        TestStorage(osg::ref_ptr<const ESMTerrain::LandObject> land, ESM::RefId worldspace, const VFS::Manager* vfs)
            : ESMTerrain::Storage(vfs)
            , mLand(std::move(land))
            , mWorldspace(worldspace)
        {
        }

        osg::ref_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellLocation) override
        {
            if (cellLocation.mWorldspace == mWorldspace && cellLocation.mX == 0 && cellLocation.mY == 0)
                return mLand;
            return nullptr;
        }

        const std::string* getLandTexture(std::uint16_t, int) override { return nullptr; }

        void getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId) override
        {
            minX = 0.f;
            maxX = 1.f;
            minY = 0.f;
            maxY = 1.f;
        }

    private:
        osg::ref_ptr<const ESMTerrain::LandObject> mLand;
        ESM::RefId mWorldspace;
    };

    TEST(ESMTerrainStorage, missingEsm4LandTextureFallsBackToDefaultLayerInfo)
    {
        constexpr VFS::Path::NormalizedView defaultDiffuse("textures/landscape/dirtwasteland01.dds");
        constexpr VFS::Path::NormalizedView defaultNormal("textures/landscape/dirtwasteland01_n.dds");
        const ESM::FormId missingTexture = ESM::FormId::fromUint32(0xa8b);
        const ESM::RefId worldspace = ESM::RefId::formIdRefId(ESM::FormId::fromUint32(0xa74));

        ESM4::Land land{};
        land.mId = ESM::FormId::fromUint32(0x2dbb);
        land.mDefaultDiffuseMap = defaultDiffuse;
        land.mDefaultNormalMap = defaultNormal;
        for (auto& texture : land.mTextures)
            texture.base.formId = missingTexture.toUint32();

        osg::ref_ptr<const ESMTerrain::LandObject> landObject = new ESMTerrain::LandObject(land, 0);
        const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({});
        TestStorage storage(landObject, worldspace, vfs.get());

        Terrain::Storage::ImageVector blendmaps;
        std::vector<Terrain::LayerInfo> layerList;
        storage.getBlendmaps(1.f, osg::Vec2f(0.5f, 0.5f), blendmaps, layerList, worldspace);

        ASSERT_FALSE(layerList.empty());
        EXPECT_EQ(layerList.front().mDiffuseMap, defaultDiffuse);
        EXPECT_EQ(layerList.front().mNormalMap, defaultNormal);
    }
}
