#ifndef APPS_OPENFO3_PLAYERSTATE_H
#define APPS_OPENFO3_PLAYERSTATE_H

#include "contentstore.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace OpenFO3
{
    enum class Fo3Skill
    {
        Barter,
        BigGuns,
        EnergyWeapons,
        Explosives,
        Lockpick,
        Medicine,
        MeleeWeapons,
        Repair,
        Science,
        SmallGuns,
        Sneak,
        Speech,
        Unarmed,
        Count,
    };

    constexpr std::size_t sFo3SkillCount = static_cast<std::size_t>(Fo3Skill::Count);

    struct SpecialStats
    {
        int strength = 0;
        int perception = 0;
        int endurance = 0;
        int charisma = 0;
        int intelligence = 0;
        int agility = 0;
        int luck = 0;
    };

    struct SkillSet
    {
        std::array<int, sFo3SkillCount> mValues{};

        int& operator[](Fo3Skill skill);
        int operator[](Fo3Skill skill) const;
    };

    struct DerivedStats
    {
        int hitPoints = 0;
        int actionPoints = 0;
        int carryWeight = 0;
        int radiationResistance = 0;
        int criticalChance = 0;
        int skillPointsPerLevel = 10;
        int skillBookBonus = 1;
        int skillCap = 100;
        float experienceMultiplier = 1.f;
    };

    struct ProgressionState
    {
        int level = 1;
        int experience = 0;
        int experienceForNextLevel = 0;
        int levelCap = 20;
        int unspentSkillPoints = 0;
        int unspentPerks = 0;
    };

    struct PermanentBonuses
    {
        SpecialStats special;
        SkillSet skills;
        std::unordered_set<std::string> appliedSourceKeys;
    };

    struct ChosenPerk
    {
        ESM::FormId perkId;
        std::string editorId;
        std::string name;
        int rank = 1;
    };

    struct PlayerState
    {
        SpecialStats baseSpecial;
        SpecialStats effectiveSpecial;
        SkillSet trainedSkills;
        SkillSet currentSkills;
        DerivedStats derived;
        ProgressionState progression;
        PermanentBonuses permanentBonuses;
        std::vector<ChosenPerk> chosenPerks;
        int radiation = 0;
    };

    constexpr std::array<Fo3Skill, sFo3SkillCount> sAllFo3Skills = {
        Fo3Skill::Barter,
        Fo3Skill::BigGuns,
        Fo3Skill::EnergyWeapons,
        Fo3Skill::Explosives,
        Fo3Skill::Lockpick,
        Fo3Skill::Medicine,
        Fo3Skill::MeleeWeapons,
        Fo3Skill::Repair,
        Fo3Skill::Science,
        Fo3Skill::SmallGuns,
        Fo3Skill::Sneak,
        Fo3Skill::Speech,
        Fo3Skill::Unarmed,
    };

    std::string_view toString(Fo3Skill skill);

    PlayerState seedPrototypePlayerState(const LoadedContent& content);
    void recomputePlayerState(PlayerState& state, const LoadedContent& content);
    int grantExperience(PlayerState& state, const LoadedContent& content, int amount);
    bool spendSkillPoint(PlayerState& state, const LoadedContent& content, Fo3Skill skill);
    bool canAccessWithSkill(const PlayerState& state, ObjectKind kind, int requiredSkill);

    std::optional<std::string> applyReadableBonus(
        PlayerState& state, std::string_view editorId, std::string_view displayName, std::string_view sourceKey);

    bool canSelectPerk(const PlayerState& state, const ESM4::Perk& perk);
    bool selectPerk(PlayerState& state, const LoadedContent& content, const ESM4::Perk& perk);
    std::vector<const ESM4::Perk*> getEligiblePerks(const PlayerState& state, const LoadedContent& content);

    std::optional<Fo3Skill> skillForBookEditorId(std::string_view editorId);
}

#endif
