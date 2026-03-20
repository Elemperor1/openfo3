#include "options.hpp"

#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>

#include <string>
#include <vector>

namespace
{
    namespace bpo = boost::program_options;
    using StringsVector = std::vector<std::string>;
}

namespace OpenFO3
{
    bpo::options_description makeOptionsDescription()
    {
        bpo::options_description desc("Syntax: openfo3 <options>\nAllowed options");
        Files::ConfigurationManager::addCommonOptions(desc);

        auto addOption = desc.add_options();
        addOption("help", "print help message");
        addOption("version", "print version information and quit");

        addOption("data",
            bpo::value<Files::MaybeQuotedPathContainer>()
                ->default_value(Files::MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");

        addOption("fallback-archive",
            bpo::value<StringsVector>()->default_value(StringsVector(), "fallback-archive")->multitoken()->composing(),
            "set fallback BSA archives (later archives have higher priority)");

        addOption("content", bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "content file(s): esm/esp");

        addOption("encoding", bpo::value<std::string>()->default_value("win1252"),
            "Character encoding used in Fallout 3 string content");

        addOption("fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");

        addOption("no-grab", bpo::value<bool>()->implicit_value(true)->default_value(false), "Don't grab mouse cursor");

        return desc;
    }
}
