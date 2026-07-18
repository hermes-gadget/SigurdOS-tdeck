// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "mesh/flood_scope_state.h"

#include <cstring>

namespace {

using sigurdos::mesh::FloodScopeState;

void fillKey(uint8_t* key, uint8_t first) {
    for (size_t i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>(first + i);
}

TEST(FloodScopeStateTest, OverrideResetRestoresDefault) {
    FloodScopeState state;
    uint8_t default_key[16];
    uint8_t override_key[16];
    uint8_t resolved[16]{};
    fillKey(default_key, 0x10);
    fillKey(override_key, 0x40);

    state.setDefault(default_key);
    state.setOverride(override_key, false);
    ASSERT_TRUE(state.takeEffective(resolved));
    EXPECT_EQ(std::memcmp(resolved, override_key, sizeof(resolved)), 0);

    state.setOverride(nullptr, false);
    ASSERT_TRUE(state.takeEffective(resolved));
    EXPECT_EQ(std::memcmp(resolved, default_key, sizeof(resolved)), 0);
}

TEST(FloodScopeStateTest, UnscopedOverridePersistsUntilScopeCommand) {
    FloodScopeState state;
    uint8_t default_key[16];
    uint8_t resolved[16]{};
    fillKey(default_key, 0x20);
    state.setDefault(default_key);

    state.setOverride(nullptr, true);
    EXPECT_FALSE(state.takeEffective(resolved));
    EXPECT_FALSE(state.takeEffective(resolved));

    state.setOverride(nullptr, false);
    ASSERT_TRUE(state.takeEffective(resolved));
    EXPECT_EQ(std::memcmp(resolved, default_key, sizeof(resolved)), 0);
}

TEST(FloodScopeStateTest, ChangingDefaultDoesNotDiscardOverride) {
    FloodScopeState state;
    uint8_t first_default[16];
    uint8_t second_default[16];
    uint8_t override_key[16];
    uint8_t resolved[16]{};
    fillKey(first_default, 0x10);
    fillKey(second_default, 0x20);
    fillKey(override_key, 0x30);

    state.setDefault(first_default);
    state.setOverride(override_key, false);
    state.setDefault(second_default);
    ASSERT_TRUE(state.takeEffective(resolved));
    EXPECT_EQ(std::memcmp(resolved, override_key, sizeof(resolved)), 0);

    state.clearOverride();
    ASSERT_TRUE(state.takeEffective(resolved));
    EXPECT_EQ(std::memcmp(resolved, second_default, sizeof(resolved)), 0);
}

TEST(FloodScopeStateTest, LocalUnscopedRequestRemainsOneShot) {
    FloodScopeState state;
    uint8_t default_key[16];
    uint8_t resolved[16]{};
    fillKey(default_key, 0x50);
    state.setDefault(default_key);

    state.setUnscopedOnce(true);
    EXPECT_FALSE(state.takeEffective(resolved));
    ASSERT_TRUE(state.takeEffective(resolved));
    EXPECT_EQ(std::memcmp(resolved, default_key, sizeof(resolved)), 0);
}

} // namespace
