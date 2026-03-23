#include "loadavif.hpp"

#include "reader.hpp"

void ESM4::ActorValueInfo::load(ESM4::Reader& reader)
{
    mId = reader.getFormIdFromHeader();
    mFlags = reader.hdr().record.flags;

    while (reader.getSubRecordHeader())
    {
        const ESM4::SubRecordHeader& subHdr = reader.subRecordHeader();
        switch (subHdr.typeId)
        {
            case ESM::fourCC("EDID"):
                reader.getZString(mEditorId);
                break;
            case ESM::fourCC("FULL"):
                reader.getLocalizedString(mFullName);
                break;
            case ESM::fourCC("DESC"):
                reader.getLocalizedString(mDescription);
                break;
            case ESM::fourCC("ICON"):
                reader.getZString(mIcon);
                break;
            case ESM::fourCC("MICO"):
                reader.getZString(mSmallIcon);
                break;
            case ESM::fourCC("ANAM"):
                reader.getZString(mShortName);
                break;
            default:
                reader.skipSubRecordData();
                break;
        }
    }
}
