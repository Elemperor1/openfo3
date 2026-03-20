#ifndef APPS_OPENFO3_ENGINE_H
#define APPS_OPENFO3_ENGINE_H

#include <filesystem>
#include <memory>
#include <string>

#include <components/files/collections.hpp>
#include <components/toutf8/toutf8.hpp>

namespace Files
{
    struct ConfigurationManager;
}

namespace OpenFO3
{
    class Engine
    {
    public:
        explicit Engine(Files::ConfigurationManager& configurationManager);
        ~Engine();

        void setDataDirs(const Files::PathContainer& dataDirs);
        void addArchive(const std::string& archive);
        void setResourceDir(const std::filesystem::path& parResDir);
        void addContentFile(const std::string& file);
        void setEncoding(const ToUTF8::FromType& encoding);
        void setGrabMouse(bool grab);

        void go();

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
