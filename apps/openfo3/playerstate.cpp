#include "playerstate.hpp"

#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    using namespace OpenFO3;

    constexpr int sFallbackXpBase = 200;
    constexpr int sFallbackXpBump = 150;
    constexpr int sFallbackLevelCap = 20;
    constexpr int sFallbackSkillCap = 100;

    struct ShellPerkBonuses
    {
        SpecialStats special;
        SkillSet skills;
        int skillPointsPerLevel = 0;
        int skillBookBonus = 0;
        float experienceMultiplier = 1.f;
    };

    int toIndex(Fo3Skill skill)
    {
        return static_cast<int>(skill);
    }

    int ceilingHalf(int value)
    {
        return (value + 1) / 2;
    }

    int clampSpecial(int value)
    {
        return std::clamp(value, 1, 10);
    }

    int clampSkillValue(int value, int skillCap)
    {
        return std::clamp(value, 0, std::max(skillCap, 0));
    }

    std::string normalizeToken(std::string_view value)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (char c : Misc::StringUtils::lowerCase(value))
        {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                normalized.push_back(c);
        }
        return normalized;
    }

    std::string normalizedPerkKey(const ChosenPerk& perk)
    {
        if (!perk.editorId.empty())
            return normalizeToken(perk.editorId);
        return normalizeToken(perk.name);
    }

    bool perkKeyMatches(std::string_view normalized, std::string_view needle)
    {
        return normalized.find(needle) != std::string::npos;
    }

    int getIntSetting(const LoadedContent& content, std::initializer_list<std::string_view> names, int fallback)
    {
        for (std::string_view name : names)
        {
            if (const std::optional<int> value = content.getGameSettingInt(name); value.has_value())
                return *value;
        }
        return fallback;
    }

    int experienceRequiredForLevel(const LoadedContent& content, int level)
    {
        if (level <= 1)
            return 0;

        const int base = getIntSetting(content, { "iXPBase" }, sFallbackXpBase);
        const int bump = getIntSetting(content, { "iXPBumpBase" }, sFallbackXpBump);

        int total = 0;
        for (int currentLevel = 2; currentLevel <= level; ++currentLevel)
            total += base + bump * std::max(currentLevel - 2, 0);
        return total;
    }

    bool isSupportedPerkConditionFunction(std::uint32_t functionIndex)
    {
        switch (functionIndex)
        {
            case ESM4::FUN_GetBaseActorValue:
            case ESM4::FUN_GetActorValue:
            case ESM4::FUN_GetPermanentActorValue:
            case ESM4::FUN_HasPerk:
                return true;
            default:
                return false;
        }
    }

    int actorValueFromState(const PlayerState& state, std::uint32_t actorValue)
    {
        switch (actorValue)
        {
            case 5:
                return state.effectiveSpecial.strength;
            case 6:
                return state.effectiveSpecial.perception;
            case 7:
                return state.effectiveSpecial.endurance;
            case 8:
                return state.effectiveSpecial.charisma;
            case 9:
                return state.effectiveSpecial.intelligence;
            case 10:
                return state.effectiveSpecial.agility;
            case 11:
                return state.effectiveSpecial.luck;
            case 12:
                return state.derived.actionPoints;
            case 13:
                return state.derived.carryWeight;
            case 14:
                return state.derived.criticalChance;
            case 16:
                return state.derived.hitPoints;
            case 20:
                return state.derived.radiationResistance;
            case 32:
                return state.currentSkills[Fo3Skill::Barter];
            case 33:
                return state.currentSkills[Fo3Skill::BigGuns];
            case 34:
                return state.currentSkills[Fo3Skill::EnergyWeapons];
            case 35:
                return state.currentSkills[Fo3Skill::Explosives];
            case 36:
                return state.currentSkills[Fo3Skill::Lockpick];
            case 37:
                return state.currentSkills[Fo3Skill::Medicine];
            case 38:
                return state.currentSkills[Fo3Skill::MeleeWeapons];
            case 39:
                return state.currentSkills[Fo3Skill::Repair];
            case 40:
                return state.currentSkills[Fo3Skill::Science];
            case 41:
                return state.currentSkills[Fo3Skill::SmallGuns];
            case 42:
                return state.currentSkills[Fo3Skill::Sneak];
            case 43:
                return state.currentSkills[Fo3Skill::Speech];
            case 45:
                return state.currentSkills[Fo3Skill::Unarmed];
            default:
                return 0;
        }
    }

    bool evaluateCondition(const PlayerState& state, const ESM4::TargetCondition& condition)
    {
        if ((condition.condition & ESM4::CTF_UseGlobal) != 0)
            return false;

        const auto compareValue = [&](float lhs) {
            constexpr float epsilon = 0.0001f;
            const std::uint32_t op = condition.condition & 0xE0u;
            switch (op)
            {
                case ESM4::CTF_EqualTo:
                    return std::fabs(lhs - condition.comparison) <= epsilon;
                case ESM4::CTF_NotEqualTo:
                    return std::fabs(lhs - condition.comparison) > epsilon;
                case ESM4::CTF_GreaterThan:
                    return lhs > condition.comparison;
                case ESM4::CTF_GrThOrEqTo:
                    return lhs >= condition.comparison;
                case ESM4::CTF_LessThan:
                    return lhs < condition.comparison;
                case ESM4::CTF_LeThOrEqTo:
                    return lhs <= condition.comparison;
                default:
                    return false;
            }
        };

        switch (condition.functionIndex)
        {
            case ESM4::FUN_GetBaseActorValue:
            case ESM4::FUN_GetActorValue:
            case ESM4::FUN_GetPermanentActorValue:
                return compareValue(static_cast<float>(actorValueFromState(state, condition.param1)));
            case ESM4::FUN_HasPerk:
            {
                const bool hasPerk = std::any_of(state.chosenPerks.begin(), state.chosenPerks.end(), [&](const ChosenPerk& perk) {
                    return perk.perkId == ESM::FormId::fromUint32(condition.param1);
                });
                return compareValue(hasPerk ? 1.f : 0.f);
            }
            default:
                return true;
        }
    }

    int selectedPerkRank(const PlayerState& state, ESM::FormId perkId)
    {
        for (const ChosenPerk& perk : state.chosenPerks)
        {
            if (perk.perkId == perkId)
                return perk.rank;
        }
        return 0;
    }

    void applyShellPerkBonuses(const std::vector<ChosenPerk>& chosenPerks, ShellPerkBonuses& bonuses)
    {
        for (const ChosenPerk& perk : chosenPerks)
        {
            const std::string key = normalizedPerkKey(perk);
            if (perkKeyMatches(key, "swiftlearner"))
                bonuses.experienceMultiplier += 0.10f * static_cast<float>(perk.rank);
            if (perkKeyMatches(key, "educated"))
                bonuses.skillPointsPerLevel += 3 * perk.rank;
            if (perkKeyMatches(key, "comprehension"))
                bonuses.skillBookBonus += perk.rank;
            if (perkKeyMatches(key, "thief"))
            {
                bonuses.skills[Fo3Skill::Lockpick] += 5 * perk.rank;
                bonuses.skills[Fo3Skill::Science] += 5 * perk.rank;
                bonuses.skills[Fo3Skill::Sneak] += 5 * perk.rank;
            }
            if (perkKeyMatches(key, "gunnut"))
            {
                bonuses.skills[Fo3Skill::Repair] += 5 * perk.rank;
                bonuses.skills[Fo3Skill::SmallGuns] += 5 * perk.rank;
            }
            if (perkKeyMatches(key, "littleleaguer"))
            {
                bonuses.skills[Fo3Skill::MeleeWeapons] += 5 * perk.rank;
                bonuses.skills[Fo3Skill::Unarmed] += 5 * perk.rank;
            }
            if (perkKeyMatches(key, "daddysboy") || perkKeyMatches(key, "daddysgirl"))
            {
                bonuses.skills[Fo3Skill::Medicine] += 5 * perk.rank;
                bonuses.skills[Fo3Skill::Science] += 5 * perk.rank;
            }
        }
    }

    int baseSkillFromSpecial(Fo3Skill skill, const SpecialStats& special)
    {
        const int luckHalf = ceilingHalf(special.luck);
        switch (skill)
        {
            case Fo3Skill::Barter:
                return 2 + 2 * special.charisma + luckHalf;
            case Fo3Skill::BigGuns:
                return 2 + 2 * special.endurance + luckHalf;
            case Fo3Skill::EnergyWeapons:
                return 2 + 2 * special.perception + luckHalf;
            case Fo3Skill::Explosives:
                return 2 + 2 * special.perception + luckHalf;
            case Fo3Skill::Lockpick:
                return 2 + 2 * special.perception + luckHalf;
            case Fo3Skill::Medicine:
                return 2 + 2 * special.intelligence + luckHalf;
            case Fo3Skill::MeleeWeapons:
                return 2 + 2 * special.strength + luckHalf;
            case Fo3Skill::Repair:
                return 2 + 2 * special.intelligence + luckHalf;
            case Fo3Skill::Science:
                return 2 + 2 * special.intelligence + luckHalf;
            case Fo3Skill::SmallGuns:
                return 2 + 2 * special.agility + luckHalf;
            case Fo3Skill::Sneak:
                return 2 + 2 * special.agility + luckHalf;
            case Fo3Skill::Speech:
                return 2 + 2 * special.charisma + luckHalf;
            case Fo3Skill::Unarmed:
                return 2 + 2 * special.endurance + luckHalf;
            case Fo3Skill::Count:
                break;
        }
        return 0;
    }

    int& specialValue(SpecialStats& special, std::size_t index)
    {
        switch (index)
        {
            case 0:
                return special.strength;
            case 1:
                return special.perception;
            case 2:
                return special.endurance;
            case 3:
                return special.charisma;
            case 4:
                return special.intelligence;
            case 5:
                return special.agility;
            default:
                return special.luck;
        }
    }

    std::string_view specialName(std::size_t index)
    {
        static constexpr std::array<std::string_view, 7> sNames = {
            "Strength", "Perception", "Endurance", "Charisma", "Intelligence", "Agility", "Luck",
        };
        return sNames.at(index);
    }

    std::optional<Fo3Skill> skillFromNormalizedText(std::string_view normalized)
    {
        static constexpr std::pair<std::string_view, Fo3Skill> sSkillTokens[] = {
            { "barter", Fo3Skill::Barter },
            { "bigguns", Fo3Skill::BigGuns },
            { "energyweapons", Fo3Skill::EnergyWeapons },
            { "explosives", Fo3Skill::Explosives },
            { "lockpick", Fo3Skill::Lockpick },
            { "lockpicking", Fo3Skill::Lockpick },
            { "medicine", Fo3Skill::Medicine },
            { "melee", Fo3Skill::MeleeWeapons },
            { "meleeweapons", Fo3Skill::MeleeWeapons },
            { "repair", Fo3Skill::Repair },
            { "science", Fo3Skill::Science },
            { "smallguns", Fo3Skill::SmallGuns },
            { "sneak", Fo3Skill::Sneak },
            { "speech", Fo3Skill::Speech },
            { "unarmed", Fo3Skill::Unarmed },
        };

        for (const auto& [token, skill] : sSkillTokens)
        {
            if (normalized.find(token) != std::string::npos)
                return skill;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> specialIndexFromNormalizedText(std::string_view normalized)
    {
        static constexpr std::pair<std::string_view, std::size_t> sSpecialTokens[] = {
            { "strength", 0 },
            { "perception", 1 },
            { "endurance", 2 },
            { "charisma", 3 },
            { "intelligence", 4 },
            { "agility", 5 },
            { "luck", 6 },
            { "yourspecial", 0 },
        };

        for (const auto& [token, index] : sSpecialTokens)
        {
            if (normalized.find(token) != std::string::npos)
                return index;
        }
        return std::nullopt;
    }
}

namespace OpenFO3
{
    int& SkillSet::operator[](Fo3Skill skill)
    {
        return mValues.at(static_cast<std::size_t>(toIndex(skill)));
    }

    int SkillSet::operator[](Fo3Skill skill) const
    {
        return mValues.at(static_cast<std::size_t>(toIndex(skill)));
    }

    std::string_view toString(Fo3Skill skill)
    {
        switch (skill)
        {
            case Fo3Skill::Barter:
                return "Barter";
            case Fo3Skill::BigGuns:
                return "Big Guns";
            case Fo3Skill::EnergyWeapons:
                return "Energy Weapons";
            case Fo3Skill::Explosives:
                return "Explosives";
            case Fo3Skill::Lockpick:
                return "Lockpick";
            case Fo3Skill::Medicine:
                return "Medicine";
            case Fo3Skill::MeleeWeapons:
                return "Melee Weapons";
            case Fo3Skill::Repair:
                return "Repair";
            case Fo3Skill::Science:
                return "Science";
            case Fo3Skill::SmallGuns:
                return "Small Guns";
            case Fo3Skill::Sneak:
                return "Sneak";
            case Fo3Skill::Speech:
                return "Speech";
            case Fo3Skill::Unarmed:
                return "Unarmed";
            case Fo3Skill::Count:
                break;
        }
        return "Unknown";
    }

    std::optional<Fo3Skill> skillForBookEditorId(std::string_view editorId)
    {
        const std::string normalized = normalizeToken(editorId);
        static constexpr std::pair<std::string_view, Fo3Skill> sBookSkills[] = {
            { "bookskillbarter", Fo3Skill::Barter },
            { "bookskillbigguns", Fo3Skill::BigGuns },
            { "bookskillenergyweapons", Fo3Skill::EnergyWeapons },
            { "bookskillexplosives", Fo3Skill::Explosives },
            { "bookskilllockpicking", Fo3Skill::Lockpick },
            { "bookskillmedicine", Fo3Skill::Medicine },
            { "bookskillmelee", Fo3Skill::MeleeWeapons },
            { "bookskillrepair", Fo3Skill::Repair },
            { "bookskillscience", Fo3Skill::Science },
            { "bookskillsmallguns", Fo3Skill::SmallGuns },
            { "bookskillsneak", Fo3Skill::Sneak },
            { "bookskillspeech", Fo3Skill::Speech },
            { "bookskillunarmed", Fo3Skill::Unarmed },
        };

        for (const auto& [token, skill] : sBookSkills)
        {
            if (normalized == token)
                return skill;
        }
        return std::nullopt;
    }

    PlayerState seedPrototypePlayerState(const LoadedContent& content)
    {
        PlayerState state;
        state.baseSpecial = SpecialStats{ .strength = 6,
            .perception = 8,
            .endurance = 6,
            .charisma = 5,
            .intelligence = 10,
            .agility = 6,
            .luck = 4 };

        state.trainedSkills[Fo3Skill::Barter] = 5;
        state.trainedSkills[Fo3Skill::BigGuns] = 8;
        state.trainedSkills[Fo3Skill::Explosives] = 6;
        state.trainedSkills[Fo3Skill::Lockpick] = 30;
        state.trainedSkills[Fo3Skill::Medicine] = 8;
        state.trainedSkills[Fo3Skill::Repair] = 10;
        state.trainedSkills[Fo3Skill::Science] = 26;
        state.trainedSkills[Fo3Skill::SmallGuns] = 6;
        state.trainedSkills[Fo3Skill::Sneak] = 5;
        state.trainedSkills[Fo3Skill::Speech] = 5;

        state.progression.level = 1;
        state.progression.experience = 0;
        state.progression.levelCap = getIntSetting(content, { "iMaxCharacterLevel" }, sFallbackLevelCap);

        recomputePlayerState(state, content);
        return state;
    }

    void recomputePlayerState(PlayerState& state, const LoadedContent& content)
    {
        state.progression.levelCap = getIntSetting(content, { "iMaxCharacterLevel" }, sFallbackLevelCap);

        ShellPerkBonuses perkBonuses;
        applyShellPerkBonuses(state.chosenPerks, perkBonuses);

        state.effectiveSpecial.strength
            = clampSpecial(state.baseSpecial.strength + state.permanentBonuses.special.strength + perkBonuses.special.strength);
        state.effectiveSpecial.perception = clampSpecial(
            state.baseSpecial.perception + state.permanentBonuses.special.perception + perkBonuses.special.perception);
        state.effectiveSpecial.endurance = clampSpecial(
            state.baseSpecial.endurance + state.permanentBonuses.special.endurance + perkBonuses.special.endurance);
        state.effectiveSpecial.charisma = clampSpecial(
            state.baseSpecial.charisma + state.permanentBonuses.special.charisma + perkBonuses.special.charisma);
        state.effectiveSpecial.intelligence = clampSpecial(state.baseSpecial.intelligence
            + state.permanentBonuses.special.intelligence + perkBonuses.special.intelligence);
        state.effectiveSpecial.agility
            = clampSpecial(state.baseSpecial.agility + state.permanentBonuses.special.agility + perkBonuses.special.agility);
        state.effectiveSpecial.luck
            = clampSpecial(state.baseSpecial.luck + state.permanentBonuses.special.luck + perkBonuses.special.luck);

        state.derived.hitPoints = 90 + state.effectiveSpecial.endurance * 20 + (state.progression.level - 1) * 10;
        state.derived.actionPoints = 65 + state.effectiveSpecial.agility * 2;
        state.derived.carryWeight = 150 + state.effectiveSpecial.strength * 10;
        state.derived.radiationResistance = state.effectiveSpecial.endurance * 2;
        state.derived.criticalChance = state.effectiveSpecial.luck;
        state.derived.skillPointsPerLevel = 10 + state.effectiveSpecial.intelligence + perkBonuses.skillPointsPerLevel;
        state.derived.skillBookBonus = std::max(1, 1 + perkBonuses.skillBookBonus);
        state.derived.skillCap = getIntSetting(content, { "iAVDSkillMax", "iSkillCap" }, sFallbackSkillCap);
        state.derived.experienceMultiplier = std::max(1.f, perkBonuses.experienceMultiplier);

        for (Fo3Skill skill : sAllFo3Skills)
        {
            const int value = baseSkillFromSpecial(skill, state.effectiveSpecial) + state.trainedSkills[skill]
                + state.permanentBonuses.skills[skill] + perkBonuses.skills[skill];
            state.currentSkills[skill] = clampSkillValue(value, state.derived.skillCap);
        }

        if (state.progression.level >= state.progression.levelCap)
            state.progression.experienceForNextLevel = experienceRequiredForLevel(content, state.progression.level);
        else
            state.progression.experienceForNextLevel = experienceRequiredForLevel(content, state.progression.level + 1);
    }

    int grantExperience(PlayerState& state, const LoadedContent& content, int amount)
    {
        if (amount <= 0)
            return 0;

        recomputePlayerState(state, content);

        const int awarded = std::max(1, static_cast<int>(std::lround(static_cast<float>(amount) * state.derived.experienceMultiplier)));
        state.progression.experience += awarded;

        while (state.progression.level < state.progression.levelCap)
        {
            const int threshold = experienceRequiredForLevel(content, state.progression.level + 1);
            if (state.progression.experience < threshold)
                break;

            ++state.progression.level;
            state.progression.unspentSkillPoints += state.derived.skillPointsPerLevel;
            state.progression.unspentPerks += 1;
            recomputePlayerState(state, content);
        }

        recomputePlayerState(state, content);
        return awarded;
    }

    bool spendSkillPoint(PlayerState& state, const LoadedContent& content, Fo3Skill skill)
    {
        recomputePlayerState(state, content);
        if (state.progression.unspentSkillPoints <= 0)
            return false;
        if (state.currentSkills[skill] >= state.derived.skillCap)
            return false;

        ++state.trainedSkills[skill];
        --state.progression.unspentSkillPoints;
        recomputePlayerState(state, content);
        return true;
    }

    bool canAccessWithSkill(const PlayerState& state, ObjectKind kind, int requiredSkill)
    {
        const int required = std::clamp(requiredSkill, 0, 100);
        if (kind == ObjectKind::Terminal)
            return state.currentSkills[Fo3Skill::Science] >= required;
        if (kind == ObjectKind::Door || kind == ObjectKind::Container)
            return state.currentSkills[Fo3Skill::Lockpick] >= required;
        return false;
    }

    std::optional<std::string> applyReadableBonus(
        PlayerState& state, std::string_view editorId, std::string_view displayName, std::string_view sourceKey)
    {
        if (sourceKey.empty())
            return std::nullopt;

        const std::string source = std::string(sourceKey);
        if (state.permanentBonuses.appliedSourceKeys.contains(source))
            return std::nullopt;

        const std::string normalizedEditorId = normalizeToken(editorId);
        const std::string normalizedDisplayName = normalizeToken(displayName);

        if (const std::optional<Fo3Skill> skill = skillForBookEditorId(editorId); skill.has_value())
        {
            state.permanentBonuses.skills[*skill] += std::max(1, state.derived.skillBookBonus);
            state.permanentBonuses.appliedSourceKeys.insert(source);
            return std::string(toString(*skill)) + " +" + std::to_string(std::max(1, state.derived.skillBookBonus));
        }

        const std::string combined = normalizedEditorId + normalizedDisplayName;
        if (combined.find("bobblehead") != std::string::npos)
        {
            if (const std::optional<Fo3Skill> skill = skillFromNormalizedText(combined); skill.has_value())
            {
                state.permanentBonuses.skills[*skill] += 10;
                state.permanentBonuses.appliedSourceKeys.insert(source);
                return std::string(toString(*skill)) + " +10";
            }

            if (const std::optional<std::size_t> specialIndex = specialIndexFromNormalizedText(combined); specialIndex.has_value())
            {
                ++specialValue(state.permanentBonuses.special, *specialIndex);
                state.permanentBonuses.appliedSourceKeys.insert(source);
                return std::string(specialName(*specialIndex)) + " +1";
            }
        }

        if (normalizedEditorId == "bookskill01" || normalizedDisplayName == "yourspecial")
        {
            std::size_t bestIndex = 0;
            int bestValue = specialValue(state.effectiveSpecial, 0);
            for (std::size_t i = 1; i < 7; ++i)
            {
                const int value = specialValue(state.effectiveSpecial, i);
                if (value < bestValue)
                {
                    bestIndex = i;
                    bestValue = value;
                }
            }

            ++specialValue(state.permanentBonuses.special, bestIndex);
            state.permanentBonuses.appliedSourceKeys.insert(source);
            return std::string(specialName(bestIndex)) + " +1";
        }

        return std::nullopt;
    }

    bool canSelectPerk(const PlayerState& state, const ESM4::Perk& perk)
    {
        if (!perk.mData.playable || perk.mData.hidden)
            return false;
        if (state.progression.unspentPerks <= 0)
            return false;
        if (state.progression.level < perk.mData.minLevel)
            return false;
        if (selectedPerkRank(state, perk.mId) >= std::max<int>(perk.mData.ranks, 1))
            return false;

        bool hasConditionResult = false;
        bool conditionResult = true;
        for (const ESM4::TargetCondition& condition : perk.mConditions)
        {
            if (!isSupportedPerkConditionFunction(condition.functionIndex))
                return false;

            const bool current = evaluateCondition(state, condition);
            if (!hasConditionResult)
            {
                conditionResult = current;
                hasConditionResult = true;
                continue;
            }

            if ((condition.condition & ESM4::CTF_Combine) != 0)
                conditionResult = conditionResult || current;
            else
                conditionResult = conditionResult && current;
        }

        return conditionResult;
    }

    bool selectPerk(PlayerState& state, const LoadedContent& content, const ESM4::Perk& perk)
    {
        if (!canSelectPerk(state, perk))
            return false;

        for (ChosenPerk& chosen : state.chosenPerks)
        {
            if (chosen.perkId == perk.mId)
            {
                ++chosen.rank;
                --state.progression.unspentPerks;
                recomputePlayerState(state, content);
                return true;
            }
        }

        state.chosenPerks.push_back(ChosenPerk{ .perkId = perk.mId,
            .editorId = perk.mEditorId,
            .name = !perk.mFullName.empty() ? perk.mFullName : perk.mEditorId,
            .rank = 1 });
        --state.progression.unspentPerks;
        recomputePlayerState(state, content);
        return true;
    }

    std::vector<const ESM4::Perk*> getEligiblePerks(const PlayerState& state, const LoadedContent& content)
    {
        std::vector<const ESM4::Perk*> perks;
        for (const ESM4::Perk* perk : content.getPlayablePerks())
        {
            if (perk != nullptr && canSelectPerk(state, *perk))
                perks.push_back(perk);
        }

        std::sort(perks.begin(), perks.end(), [](const ESM4::Perk* left, const ESM4::Perk* right) {
            const std::string_view leftName = !left->mFullName.empty() ? std::string_view(left->mFullName)
                                                                       : std::string_view(left->mEditorId);
            const std::string_view rightName = !right->mFullName.empty() ? std::string_view(right->mFullName)
                                                                         : std::string_view(right->mEditorId);
            return leftName < rightName;
        });
        return perks;
    }
}
