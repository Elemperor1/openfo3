#include "contentstore.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/util.hpp>
#include <components/esm4/common.hpp>
#include <components/esm4/reader.hpp>
#include <components/esm4/readerutils.hpp>
#include <components/files/constrainedfilestream.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string normalizeReadableText(std::string text)
    {
        Misc::StringUtils::replaceAll(text, "\r\n", "\n");
        Misc::StringUtils::replaceAll(text, "\r", "\n");
        Misc::StringUtils::replaceAll(text, "\t", "    ");
        Misc::StringUtils::trim(text);
        return text;
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

    int bootstrapScoreForKind(OpenFO3::ObjectKind kind)
    {
        using OpenFO3::ObjectKind;
        switch (kind)
        {
            case ObjectKind::Door:
                return 60;
            case ObjectKind::Container:
                return 40;
            case ObjectKind::Terminal:
                return 32;
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
                return 6;
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

    int settlementBootstrapBonus(const OpenFO3::BaseRecordData& base, const ESM4::Reference& ref)
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

    std::string normalizedEditorId(std::string_view editorId)
    {
        return Misc::StringUtils::lowerCase(editorId);
    }
}

namespace OpenFO3
{
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

    bool isLockableKind(ObjectKind kind)
    {
        return kind == ObjectKind::Door || kind == ObjectKind::Container || kind == ObjectKind::Terminal;
    }

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

    void LoadedContent::load(const Files::Collections& fileCollections, const std::vector<std::string>& contentFiles,
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
            ESM4::Reader reader(Files::openConstrainedFileStream(path), path, vfs, encoder, true);
            reader.setModIndex(static_cast<std::uint32_t>(i));
            reader.updateModIndices(fileToIndex);

            ESM4::ReaderUtils::readAll(
                reader, [&](ESM4::Reader& activeReader) { return handleRecord(activeReader); }, [](ESM4::Reader&) {});
        }

        rebuildDerivedIndices();
    }

    std::optional<BootstrapCell> LoadedContent::chooseBootstrapCell() const
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
                    settlementRefBonus += ::settlementBootstrapBonus(*base, ref);
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

                const int settlementBonus = ::settlementBootstrapBonus(cell);
                const int totalScore = settlementBonus + settlementRefBonus + interactiveScore * 8 + renderableRefs * 3
                    + doorCount * 160 + containerCount * 48 + terminalCount * 64;

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

    const BaseRecordData* LoadedContent::findBase(ESM::FormId id) const
    {
        const auto it = mBaseRecords.find(id);
        return it == mBaseRecords.end() ? nullptr : &it->second;
    }

    const ESM4::LevelledItem* LoadedContent::findLevelledItem(ESM::FormId id) const
    {
        const auto it = mLevelledItems.find(id);
        return it == mLevelledItems.end() ? nullptr : &it->second;
    }

    const ESM4::FormIdList* LoadedContent::findFormList(ESM::FormId id) const
    {
        const auto it = mFormLists.find(id);
        return it == mFormLists.end() ? nullptr : &it->second;
    }

    const ESM4::Land* LoadedContent::findLand(ESM::RefId worldspace, int x, int y) const
    {
        const auto it = mLandByCell.find(std::make_tuple(worldspace, x, y));
        if (it == mLandByCell.end())
            return nullptr;

        const auto landIt = mLands.find(it->second);
        return landIt == mLands.end() ? nullptr : &landIt->second;
    }

    bool LoadedContent::hasLand(ESM::RefId worldspace, int x, int y) const
    {
        return mLandByCell.find(std::make_tuple(worldspace, x, y)) != mLandByCell.end();
    }

    const std::map<ESM::FormId, ESM4::Reference>* LoadedContent::getReferences(ESM::RefId cellId) const
    {
        const auto it = mReferencesByCell.find(cellId);
        return it == mReferencesByCell.end() ? nullptr : &it->second;
    }

    const ESM4::Reference* LoadedContent::findReference(ESM::FormId id) const
    {
        const auto it = mReferenceById.find(id);
        return it == mReferenceById.end() ? nullptr : it->second;
    }

    const ESM4::LandTexture* LoadedContent::findLandTexture(ESM::FormId id) const
    {
        const auto it = mLandTextures.find(id);
        return it == mLandTextures.end() ? nullptr : &it->second;
    }

    const ESM4::TextureSet* LoadedContent::findTextureSet(ESM::FormId id) const
    {
        const auto it = mTextureSets.find(id);
        return it == mTextureSets.end() ? nullptr : &it->second;
    }

    const ESM4::Cell* LoadedContent::findCell(ESM::RefId id) const
    {
        const auto it = mCells.find(id);
        return it == mCells.end() ? nullptr : &it->second;
    }

    std::optional<ESM::RefId> LoadedContent::findExteriorCellId(ESM::RefId worldspace, int x, int y) const
    {
        const auto it = mExteriorCellByCoord.find(std::make_tuple(worldspace, x, y));
        if (it == mExteriorCellByCoord.end())
            return std::nullopt;
        return it->second;
    }

    const std::vector<const ESM4::Reference*>* LoadedContent::getExteriorSpatialReferences(
        const ExteriorBucketKey& key) const
    {
        const auto it = mExteriorRefsBySpatialCell.find(key);
        return it == mExteriorRefsBySpatialCell.end() ? nullptr : &it->second;
    }

    const WorldBounds* LoadedContent::getWorldBounds(ESM::RefId worldspace) const
    {
        const auto it = mWorldBounds.find(worldspace);
        return it == mWorldBounds.end() ? nullptr : &it->second;
    }

    const ESM4::GameSetting* LoadedContent::findGameSetting(std::string_view editorId) const
    {
        const auto indexIt = mGameSettingsByEditorId.find(normalizedEditorId(editorId));
        if (indexIt == mGameSettingsByEditorId.end())
            return nullptr;
        const auto it = mGameSettings.find(indexIt->second);
        return it == mGameSettings.end() ? nullptr : &it->second;
    }

    std::optional<int> LoadedContent::getGameSettingInt(std::string_view editorId) const
    {
        const ESM4::GameSetting* setting = findGameSetting(editorId);
        if (setting == nullptr)
            return std::nullopt;
        if (const auto* value = std::get_if<std::int32_t>(&setting->mData))
            return static_cast<int>(*value);
        if (const auto* value = std::get_if<std::uint32_t>(&setting->mData))
            return static_cast<int>(*value);
        if (const auto* value = std::get_if<bool>(&setting->mData))
            return *value ? 1 : 0;
        return std::nullopt;
    }

    std::optional<float> LoadedContent::getGameSettingFloat(std::string_view editorId) const
    {
        const ESM4::GameSetting* setting = findGameSetting(editorId);
        if (setting == nullptr)
            return std::nullopt;
        if (const auto* value = std::get_if<float>(&setting->mData))
            return *value;
        if (const auto* value = std::get_if<std::int32_t>(&setting->mData))
            return static_cast<float>(*value);
        if (const auto* value = std::get_if<std::uint32_t>(&setting->mData))
            return static_cast<float>(*value);
        if (const auto* value = std::get_if<bool>(&setting->mData))
            return *value ? 1.f : 0.f;
        return std::nullopt;
    }

    std::optional<bool> LoadedContent::getGameSettingBool(std::string_view editorId) const
    {
        const ESM4::GameSetting* setting = findGameSetting(editorId);
        if (setting == nullptr)
            return std::nullopt;
        if (const auto* value = std::get_if<bool>(&setting->mData))
            return *value;
        if (const auto* value = std::get_if<std::int32_t>(&setting->mData))
            return *value != 0;
        if (const auto* value = std::get_if<std::uint32_t>(&setting->mData))
            return *value != 0;
        return std::nullopt;
    }

    const ESM4::GlobalVariable* LoadedContent::findGlobal(std::string_view editorId) const
    {
        const auto indexIt = mGlobalsByEditorId.find(normalizedEditorId(editorId));
        if (indexIt == mGlobalsByEditorId.end())
            return nullptr;
        const auto it = mGlobals.find(indexIt->second);
        return it == mGlobals.end() ? nullptr : &it->second;
    }

    std::optional<float> LoadedContent::getGlobalFloat(std::string_view editorId) const
    {
        const ESM4::GlobalVariable* global = findGlobal(editorId);
        if (global == nullptr)
            return std::nullopt;
        return global->mValue;
    }

    const ESM4::DefaultObj* LoadedContent::getDefaultObjects() const
    {
        if (!mDefaultObjectId.has_value())
            return nullptr;
        const auto it = mDefaultObjects.find(*mDefaultObjectId);
        return it == mDefaultObjects.end() ? nullptr : &it->second;
    }

    const ESM4::Race* LoadedContent::findRace(ESM::FormId id) const
    {
        const auto it = mRaces.find(id);
        return it == mRaces.end() ? nullptr : &it->second;
    }

    const ESM4::Class* LoadedContent::findClass(ESM::FormId id) const
    {
        const auto it = mClasses.find(id);
        return it == mClasses.end() ? nullptr : &it->second;
    }

    const ESM4::Npc* LoadedContent::findNpc(ESM::FormId id) const
    {
        const auto it = mNpcs.find(id);
        return it == mNpcs.end() ? nullptr : &it->second;
    }

    const ESM4::Npc* LoadedContent::findPrototypePlayerNpc() const
    {
        if (!mPrototypePlayerNpcId.has_value())
            return nullptr;
        return findNpc(*mPrototypePlayerNpcId);
    }

    const ESM4::ActorValueInfo* LoadedContent::findActorValue(ESM::FormId id) const
    {
        const auto it = mActorValues.find(id);
        return it == mActorValues.end() ? nullptr : &it->second;
    }

    const ESM4::ActorValueInfo* LoadedContent::findActorValue(std::string_view editorId) const
    {
        const auto indexIt = mActorValuesByEditorId.find(normalizedEditorId(editorId));
        if (indexIt == mActorValuesByEditorId.end())
            return nullptr;
        return findActorValue(indexIt->second);
    }

    const ESM4::Perk* LoadedContent::findPerk(ESM::FormId id) const
    {
        const auto it = mPerks.find(id);
        return it == mPerks.end() ? nullptr : &it->second;
    }

    const ESM4::Perk* LoadedContent::findPerk(std::string_view editorId) const
    {
        const auto indexIt = mPerksByEditorId.find(normalizedEditorId(editorId));
        if (indexIt == mPerksByEditorId.end())
            return nullptr;
        return findPerk(indexIt->second);
    }

    std::vector<const ESM4::Perk*> LoadedContent::getPlayablePerks() const
    {
        std::vector<const ESM4::Perk*> perks;
        perks.reserve(mPerks.size());
        for (const auto& [id, perk] : mPerks)
        {
            (void)id;
            if (!perk.mData.playable || perk.mData.hidden)
                continue;
            perks.push_back(&perk);
        }
        return perks;
    }

    bool LoadedContent::handleRecord(ESM4::Reader& reader)
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
            case ESM4::REC_GMST:
            {
                ESM4::GameSetting record;
                record.load(reader);
                mGameSettings[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_GLOB:
            {
                ESM4::GlobalVariable record;
                record.load(reader);
                mGlobals[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_DOBJ:
            {
                ESM4::DefaultObj record;
                record.load(reader);
                mDefaultObjects[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_RACE:
            {
                ESM4::Race record;
                record.load(reader);
                mRaces[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_CLAS:
            {
                ESM4::Class record;
                record.load(reader);
                mClasses[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_NPC_:
            {
                ESM4::Npc record;
                record.load(reader);
                mNpcs[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_AVIF:
            {
                ESM4::ActorValueInfo record;
                record.load(reader);
                mActorValues[record.mId] = std::move(record);
                return true;
            }
            case ESM4::REC_PERK:
            {
                ESM4::Perk record;
                record.load(reader);
                mPerks[record.mId] = std::move(record);
                return true;
            }
            default:
                return false;
        }
    }

    void LoadedContent::rebuildDerivedIndices()
    {
        mLandByCell.clear();
        mReferenceById.clear();
        mWorldBounds.clear();
        mExteriorCellByCoord.clear();
        mExteriorRefsBySpatialCell.clear();
        mGameSettingsByEditorId.clear();
        mGlobalsByEditorId.clear();
        mActorValuesByEditorId.clear();
        mPerksByEditorId.clear();
        mDefaultObjectId.reset();
        mPrototypePlayerNpcId.reset();

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
                (void)cellId;
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
            const float cellSize = static_cast<float>(ESM::getCellSize(cell.mParent));
            mWorldBounds[cell.mParent].include(static_cast<float>(cell.mX) * cellSize, static_cast<float>(cell.mY) * cellSize);
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

        for (const auto& [id, setting] : mGameSettings)
        {
            (void)id;
            if (!setting.mEditorId.empty())
                mGameSettingsByEditorId[normalizedEditorId(setting.mEditorId)] = setting.mId;
        }

        for (const auto& [id, global] : mGlobals)
        {
            (void)id;
            if (!global.mEditorId.empty())
                mGlobalsByEditorId[normalizedEditorId(global.mEditorId)] = global.mId;
        }

        for (const auto& [id, valueInfo] : mActorValues)
        {
            (void)id;
            if (!valueInfo.mEditorId.empty())
                mActorValuesByEditorId[normalizedEditorId(valueInfo.mEditorId)] = valueInfo.mId;
        }

        for (const auto& [id, perk] : mPerks)
        {
            (void)id;
            if (!perk.mEditorId.empty())
                mPerksByEditorId[normalizedEditorId(perk.mEditorId)] = perk.mId;
        }

        if (!mDefaultObjects.empty())
            mDefaultObjectId = mDefaultObjects.rbegin()->first;

        for (const auto& [id, npc] : mNpcs)
        {
            (void)id;
            if (Misc::StringUtils::ciEqual(npc.mEditorId, "Player")
                || Misc::StringUtils::ciEqual(npc.mEditorId, "PlayerCharacter"))
            {
                mPrototypePlayerNpcId = npc.mId;
                break;
            }
        }
    }
}
