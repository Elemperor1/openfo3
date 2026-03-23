#include <components/resource/imagemanager.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>

namespace
{
    TEST(ResourceImageManager, emptyPathReturnsWarningImage)
    {
        const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({});
        Resource::ImageManager imageManager(vfs.get(), 0.0);

        osg::ref_ptr<osg::Image> image = imageManager.getImage(VFS::Path::NormalizedView());

        EXPECT_EQ(image.get(), imageManager.getWarningImage());
    }
}
