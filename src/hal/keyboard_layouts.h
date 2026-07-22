#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos::keyboard_layouts {

enum class Id : uint8_t {
    English = 0,
    Bulgarian,
    Russian,
    Ukrainian,
    Serbian,
    Greek,
    Arabic,
    French,
    Dutch,
    German,
    Spanish,
    Italian,
    Count,
};

constexpr size_t COUNT = static_cast<size_t>(Id::Count);
static_assert(COUNT == 12, "issue #752 requires the wadamesh 12-layout set");
constexpr uint32_t DOUBLE_SPACE_WINDOW_MS = 250;

// Small, target-aware state machine for the layout-cycle gesture. Keeping it
// independent of LVGL makes timing, target changes, and millis() wrap testable.
class CycleGesture {
public:
    bool on_space(uint32_t now_ms, uintptr_t target);
    void reset();

private:
    bool armed_ = false;
    uint32_t last_ms_ = 0;
    uintptr_t target_ = 0;
};

// Load the active layout from NodePrefs. Invalid persisted values become English.
void init();

Id active();
const char* name(Id id);

// Set/cycle the active layout. User changes are persisted to NVS.
bool set_active(Id id, bool persist = true);
bool cycle(Id* selected = nullptr);

// Map a pre-decoded C3 ASCII key to UTF-8. nullptr means pass the key through.
// Some mappings contain multiple codepoints (for example Arabic lam-alef or
// Dutch ij); callers must insert the complete string.
const char* map_key(Id id, int key, bool shifted = false);

} // namespace sigurdos::keyboard_layouts
