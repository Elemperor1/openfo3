#include <components/debug/debugging.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/osgpluginchecker.hpp>
#include <components/platform/platform.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include "engine.hpp"
#include "options.hpp"

#include <boost/program_options/variables_map.hpp>
#include <osg/Notify>

#if defined(_WIN32)
#include <components/misc/windows.hpp>
#include <cstdlib>

extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
#endif

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace bpo = boost::program_options;
    using StringsVector = std::vector<std::string>;

    class OSGLogHandler : public osg::NotifyHandler
    {
    public:
        void notify(osg::NotifySeverity severity, const char* msg) override
        {
            std::string copy(msg == nullptr ? "" : msg);
            if (copy.empty())
                return;

            Debug::Level level = Debug::Debug;
            switch (severity)
            {
                case osg::ALWAYS:
                case osg::FATAL:
                    level = Debug::Error;
                    break;
                case osg::WARN:
                case osg::NOTICE:
                    level = Debug::Warning;
                    break;
                case osg::INFO:
                    level = Debug::Info;
                    break;
                case osg::DEBUG_INFO:
                case osg::DEBUG_FP:
                default:
                    break;
            }

            std::string_view text(copy);
            if (!text.empty() && text.back() == '\n')
                text.remove_suffix(1);
            Log(level) << text;
        }
    };

    bool parseOptions(int argc, char** argv, OpenFO3::Engine& engine, Files::ConfigurationManager& cfgMgr)
    {
        bpo::options_description desc = OpenFO3::makeOptionsDescription();
        bpo::variables_map variables;

        Files::parseArgs(argc, argv, variables, desc);
        bpo::notify(variables);

        if (variables.count("help") != 0)
        {
            Debug::getRawStdout() << desc << std::endl;
            return false;
        }

        if (variables.count("version") != 0)
        {
            Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
            return false;
        }

        cfgMgr.processPaths(variables, std::filesystem::current_path());
        cfgMgr.readConfiguration(variables, desc);
        Debug::setupLogging(cfgMgr.getLogPath(), "OpenFO3");
        Log(Debug::Info) << "OpenFO3 prototype";
        Log(Debug::Info) << Version::getOpenmwVersionDescription();

        Settings::Manager::load(cfgMgr);

        engine.setGrabMouse(!variables["no-grab"].as<bool>());

        const std::string encoding = variables["encoding"].as<std::string>();
        Log(Debug::Info) << ToUTF8::encodingUsingMessage(encoding);
        engine.setEncoding(ToUTF8::calculateEncoding(encoding));

        Files::PathContainer dataDirs(asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));
        cfgMgr.filterOutNonExistingPaths(dataDirs);

        engine.setResourceDir(variables["resources"].as<Files::MaybeQuotedPath>().u8string());
        engine.setDataDirs(dataDirs);

        for (const auto& archive : variables["fallback-archive"].as<StringsVector>())
            engine.addArchive(archive);

        StringsVector content = variables["content"].as<StringsVector>();
        if (content.empty())
        {
            Log(Debug::Error) << "No content file given. Pass Fallout 3 content with --content.";
            return false;
        }

        for (const auto& file : content)
            engine.addContentFile(file);

        Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);
        return true;
    }

    int runApplication(int argc, char* argv[])
    {
        Platform::init();
        osg::setNotifyHandler(new OSGLogHandler());

        Files::ConfigurationManager cfgMgr(true);
        OpenFO3::Engine engine(cfgMgr);

        if (!parseOptions(argc, argv, engine, cfgMgr))
            return 0;

        if (!Misc::checkRequiredOSGPluginsArePresent())
            return 1;

        engine.go();
        return 0;
    }
}

#ifdef ANDROID
extern "C" int SDL_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    return Debug::wrapApplication(&runApplication, argc, argv, "OpenFO3");
}

#if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}
#endif
