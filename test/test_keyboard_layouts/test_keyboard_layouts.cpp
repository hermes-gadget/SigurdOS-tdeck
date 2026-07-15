// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/keyboard_layouts.h"
#include "hal/prefs.h"

#include <cstdint>
#include <cstring>

namespace {

using sigurdos::keyboard_layouts::CycleGesture;
using sigurdos::keyboard_layouts::Id;

class KeyboardLayoutsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::NodePrefs prefs;
        prefs.set_defaults();
        sigurdos::prefs_set(prefs);
        sigurdos::keyboard_layouts::init();
    }
};

TEST_F(KeyboardLayoutsTest, ExposesAllTwelveLayoutsInStableOrder)
{
    const char* expected[] = {
        "EN", "BG", "RU", "UK", "SR", "EL",
        "AR", "FR", "NL", "DE", "ES", "IT",
    };
    static_assert(sizeof(expected) / sizeof(expected[0]) ==
                  sigurdos::keyboard_layouts::COUNT);

    for (size_t i = 0; i < sigurdos::keyboard_layouts::COUNT; ++i) {
        EXPECT_STREQ(expected[i], sigurdos::keyboard_layouts::name(static_cast<Id>(i)));
    }
    EXPECT_STREQ("EN", sigurdos::keyboard_layouts::name(static_cast<Id>(255)));
}

TEST_F(KeyboardLayoutsTest, LoadsAndPersistsActiveLayout)
{
    sigurdos::NodePrefs prefs = sigurdos::prefs_get();
    prefs.kbd_layout = static_cast<uint8_t>(Id::Greek);
    sigurdos::prefs_set(prefs);

    sigurdos::keyboard_layouts::init();
    EXPECT_EQ(Id::Greek, sigurdos::keyboard_layouts::active());

    ASSERT_TRUE(sigurdos::keyboard_layouts::set_active(Id::Arabic));
    EXPECT_EQ(Id::Arabic, sigurdos::keyboard_layouts::active());
    EXPECT_EQ(static_cast<uint8_t>(Id::Arabic), sigurdos::prefs_get().kbd_layout);
}

TEST_F(KeyboardLayoutsTest, RejectsInvalidLayoutWithoutChangingState)
{
    ASSERT_TRUE(sigurdos::keyboard_layouts::set_active(Id::German));
    const uint8_t persisted = sigurdos::prefs_get().kbd_layout;

    EXPECT_FALSE(sigurdos::keyboard_layouts::set_active(static_cast<Id>(255)));
    EXPECT_EQ(Id::German, sigurdos::keyboard_layouts::active());
    EXPECT_EQ(persisted, sigurdos::prefs_get().kbd_layout);
    EXPECT_EQ(nullptr, sigurdos::keyboard_layouts::map_key(static_cast<Id>(255), 'a'));
}

TEST_F(KeyboardLayoutsTest, InvalidPersistedLayoutFallsBackToEnglish)
{
    sigurdos::NodePrefs prefs = sigurdos::prefs_get();
    prefs.kbd_layout = 255;
    sigurdos::prefs_set(prefs);

    sigurdos::keyboard_layouts::init();
    EXPECT_EQ(Id::English, sigurdos::keyboard_layouts::active());
}

TEST_F(KeyboardLayoutsTest, CyclePersistsAndWraps)
{
    ASSERT_TRUE(sigurdos::keyboard_layouts::set_active(Id::Italian, false));
    EXPECT_EQ(Id::English, sigurdos::keyboard_layouts::cycle());
    EXPECT_EQ(0, sigurdos::prefs_get().kbd_layout);

    for (size_t i = 1; i < sigurdos::keyboard_layouts::COUNT; ++i) {
        EXPECT_EQ(static_cast<Id>(i), sigurdos::keyboard_layouts::cycle());
    }
    EXPECT_EQ(static_cast<uint8_t>(Id::Italian), sigurdos::prefs_get().kbd_layout);
}

TEST_F(KeyboardLayoutsTest, MapsCyrillicGreekAndArabicLetters)
{
    EXPECT_STREQ("а", sigurdos::keyboard_layouts::map_key(Id::Bulgarian, 'a'));
    EXPECT_STREQ("Я", sigurdos::keyboard_layouts::map_key(Id::Russian, 'Q'));
    EXPECT_STREQ("ґ", sigurdos::keyboard_layouts::map_key(Id::Ukrainian, 'g'));
    EXPECT_STREQ("Џ", sigurdos::keyboard_layouts::map_key(Id::Serbian, 'X'));
    EXPECT_STREQ("ω", sigurdos::keyboard_layouts::map_key(Id::Greek, 'v'));
    EXPECT_STREQ("ش", sigurdos::keyboard_layouts::map_key(Id::Arabic, 'A'));
}

TEST_F(KeyboardLayoutsTest, MapsLatinKeyboardVariants)
{
    EXPECT_STREQ("q", sigurdos::keyboard_layouts::map_key(Id::French, 'a'));
    EXPECT_STREQ("A", sigurdos::keyboard_layouts::map_key(Id::French, 'Q'));
    EXPECT_STREQ("z", sigurdos::keyboard_layouts::map_key(Id::German, 'y'));
    EXPECT_STREQ("Y", sigurdos::keyboard_layouts::map_key(Id::German, 'Z'));
    EXPECT_STREQ("ñ", sigurdos::keyboard_layouts::map_key(Id::Spanish, '1'));
    EXPECT_STREQ("à", sigurdos::keyboard_layouts::map_key(Id::Italian, '1'));
}

TEST_F(KeyboardLayoutsTest, PreservesCompleteMultiCodepointMappings)
{
    const char* arabic = sigurdos::keyboard_layouts::map_key(Id::Arabic, 'b');
    const char* dutch = sigurdos::keyboard_layouts::map_key(Id::Dutch, '1');

    ASSERT_NE(nullptr, arabic);
    EXPECT_STREQ("لا", arabic);
    EXPECT_EQ(4u, std::strlen(arabic));
    ASSERT_NE(nullptr, dutch);
    EXPECT_STREQ("ij", dutch);
    EXPECT_EQ(2u, std::strlen(dutch));
}

TEST_F(KeyboardLayoutsTest, SelectsShiftedDigitTablesForBothC3Encodings)
{
    EXPECT_STREQ("ш", sigurdos::keyboard_layouts::map_key(Id::Bulgarian, '1', false));
    EXPECT_STREQ("Ш", sigurdos::keyboard_layouts::map_key(Id::Bulgarian, '1', true));
    EXPECT_STREQ("Ш", sigurdos::keyboard_layouts::map_key(Id::Bulgarian, '!', true));
    EXPECT_EQ(nullptr, sigurdos::keyboard_layouts::map_key(Id::Bulgarian, '!', false));
}

TEST_F(KeyboardLayoutsTest, PassesEnglishControlsAndUnmappedPunctuationThrough)
{
    EXPECT_EQ(nullptr, sigurdos::keyboard_layouts::map_key(Id::English, 'a'));
    EXPECT_EQ(nullptr, sigurdos::keyboard_layouts::map_key(Id::Russian, ' '));
    EXPECT_EQ(nullptr, sigurdos::keyboard_layouts::map_key(Id::Greek, '#', false));
    EXPECT_EQ(nullptr, sigurdos::keyboard_layouts::map_key(Id::Arabic, 0x0D));
}

TEST(CycleGestureTest, CyclesOnlyOnSameTargetWithinWindow)
{
    CycleGesture gesture;
    EXPECT_FALSE(gesture.on_space(1000, 1));
    EXPECT_FALSE(gesture.on_space(1100, 2));
    EXPECT_TRUE(gesture.on_space(1200, 2));
    EXPECT_FALSE(gesture.on_space(1300, 2));
    EXPECT_TRUE(gesture.on_space(1550, 2));
}

TEST(CycleGestureTest, SlowSecondTapBecomesNewFirstTap)
{
    CycleGesture gesture;
    EXPECT_FALSE(gesture.on_space(100, 1));
    EXPECT_FALSE(gesture.on_space(351, 1));
    EXPECT_TRUE(gesture.on_space(601, 1));
}

TEST(CycleGestureTest, HandlesMillisZeroAndWrap)
{
    CycleGesture gesture;
    EXPECT_FALSE(gesture.on_space(0, 1));
    EXPECT_TRUE(gesture.on_space(250, 1));

    EXPECT_FALSE(gesture.on_space(UINT32_MAX - 100, 1));
    EXPECT_TRUE(gesture.on_space(50, 1));
}

TEST(CycleGestureTest, ResetAndInvalidTargetDisarmGesture)
{
    CycleGesture gesture;
    EXPECT_FALSE(gesture.on_space(100, 1));
    gesture.reset();
    EXPECT_FALSE(gesture.on_space(200, 1));
    EXPECT_FALSE(gesture.on_space(210, 0));
    EXPECT_FALSE(gesture.on_space(220, 1));
}

} // namespace
