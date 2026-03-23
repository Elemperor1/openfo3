#include <gtest/gtest.h>

#include "../../openfo3/playerstate.hpp"

namespace
{
    TEST(OpenFO3PlayerState, SeededPlayerShellMatchesPrototypeAccessFloor)
    {
        OpenFO3::LoadedContent content;
        OpenFO3::PlayerState state = OpenFO3::seedPrototypePlayerState(content);

        EXPECT_EQ(state.progression.level, 1);
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Lockpick], 50);
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Science], 50);
        EXPECT_EQ(state.progression.unspentSkillPoints, 0);
        EXPECT_EQ(state.progression.unspentPerks, 0);
    }

    TEST(OpenFO3PlayerState, GrantExperienceAwardsLevelSkillPointsAndPerkPick)
    {
        OpenFO3::LoadedContent content;
        OpenFO3::PlayerState state = OpenFO3::seedPrototypePlayerState(content);

        const int awarded = OpenFO3::grantExperience(state, content, 250);

        EXPECT_EQ(awarded, 250);
        EXPECT_EQ(state.progression.level, 2);
        EXPECT_EQ(state.progression.unspentSkillPoints, state.derived.skillPointsPerLevel);
        EXPECT_EQ(state.progression.unspentPerks, 1);
    }

    TEST(OpenFO3PlayerState, ReadableSkillBookAppliesOncePerSourceKey)
    {
        OpenFO3::LoadedContent content;
        OpenFO3::PlayerState state = OpenFO3::seedPrototypePlayerState(content);

        const auto first = OpenFO3::applyReadableBonus(
            state, "BookSkillScience", "Big Book of Science", "ref:science-book");
        OpenFO3::recomputePlayerState(state, content);

        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(*first, "Science +1");
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Science], 51);

        const auto second = OpenFO3::applyReadableBonus(
            state, "BookSkillScience", "Big Book of Science", "ref:science-book");
        OpenFO3::recomputePlayerState(state, content);

        EXPECT_FALSE(second.has_value());
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Science], 51);
    }

    TEST(OpenFO3PlayerState, SpecialBookRaisesLowestSpecialStat)
    {
        OpenFO3::LoadedContent content;
        OpenFO3::PlayerState state = OpenFO3::seedPrototypePlayerState(content);

        const auto result = OpenFO3::applyReadableBonus(state, "BookSkill01", "You're SPECIAL", "ref:ys");
        OpenFO3::recomputePlayerState(state, content);

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, "Luck +1");
        EXPECT_EQ(state.effectiveSpecial.luck, 5);
    }

    TEST(OpenFO3PlayerState, SelectingSimplePerkAppliesImmediateSkillBonuses)
    {
        OpenFO3::LoadedContent content;
        OpenFO3::PlayerState state = OpenFO3::seedPrototypePlayerState(content);
        OpenFO3::grantExperience(state, content, 250);

        ESM4::Perk thief;
        thief.mId = ESM::FormId::fromUint32(0x01020304);
        thief.mEditorId = "Thief";
        thief.mFullName = "Thief";
        thief.mData.playable = 1;
        thief.mData.ranks = 1;
        thief.mData.minLevel = 2;

        ASSERT_TRUE(OpenFO3::selectPerk(state, content, thief));
        EXPECT_EQ(state.progression.unspentPerks, 0);
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Lockpick], 55);
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Science], 55);
        EXPECT_EQ(state.currentSkills[OpenFO3::Fo3Skill::Sneak], 26);
    }

    TEST(OpenFO3PlayerState, UnsupportedPerkConditionFunctionBlocksSelection)
    {
        OpenFO3::LoadedContent content;
        OpenFO3::PlayerState state = OpenFO3::seedPrototypePlayerState(content);
        OpenFO3::grantExperience(state, content, 250);

        ESM4::Perk perk;
        perk.mId = ESM::FormId::fromUint32(0x01020305);
        perk.mEditorId = "RequiresUnsupportedCondition";
        perk.mFullName = "Requires Unsupported Condition";
        perk.mData.playable = 1;
        perk.mData.ranks = 1;
        perk.mData.minLevel = 2;

        ESM4::TargetCondition condition{};
        condition.functionIndex = 0xFFFF; // Unsupported by OpenFO3 shell.
        condition.comparison = 0.f;
        perk.mConditions.push_back(condition);

        EXPECT_FALSE(OpenFO3::canSelectPerk(state, perk));
        EXPECT_FALSE(OpenFO3::selectPerk(state, content, perk));
        EXPECT_EQ(state.progression.unspentPerks, 1);
    }
}
