#ifndef ESM4_AVIF_H
#define ESM4_AVIF_H

#include <cstdint>
#include <string>

#include <components/esm/defs.hpp>
#include <components/esm/formid.hpp>

namespace ESM4
{
    class Reader;

    struct ActorValueInfo
    {
        ESM::FormId mId;
        std::uint32_t mFlags = 0;

        std::string mEditorId;
        std::string mFullName;
        std::string mDescription;
        std::string mIcon;
        std::string mSmallIcon;
        std::string mShortName;

        void load(Reader& reader);
        static constexpr ESM::RecNameInts sRecordId = ESM::RecNameInts::REC_AVIF4;
    };
}

#endif
