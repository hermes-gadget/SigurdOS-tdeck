// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.

#pragma once

#include <stddef.h>

namespace sigurdos {
namespace mesh {

// Apply an indexed channel update while preserving the invariant that every
// populated slot precedes every empty slot. Clearing a slot compacts later
// channels; inserting beyond the first empty slot is rejected.
template <size_t Capacity, typename Slot, typename ReadSlot,
          typename WriteSlot, typename IsEmpty>
bool set_contiguous_channel_slot(size_t index, const Slot& replacement,
                                 ReadSlot read_slot, WriteSlot write_slot,
                                 IsEmpty is_empty) {
    if (index >= Capacity) return false;

    Slot slots[Capacity]{};
    for (size_t i = 0; i < Capacity; ++i) {
        if (!read_slot(i, slots[i])) return false;
    }

    if (!is_empty(replacement) && is_empty(slots[index])) {
        size_t first_empty = 0;
        while (first_empty < Capacity && !is_empty(slots[first_empty])) {
            ++first_empty;
        }
        if (index > first_empty) return false;
    }

    slots[index] = replacement;

    size_t populated = 0;
    for (size_t i = 0; i < Capacity; ++i) {
        if (!is_empty(slots[i])) {
            if (populated != i) slots[populated] = slots[i];
            ++populated;
        }
    }
    for (size_t i = populated; i < Capacity; ++i) slots[i] = Slot{};

    for (size_t i = 0; i < Capacity; ++i) {
        if (!write_slot(i, slots[i])) return false;
    }
    return true;
}

}  // namespace mesh
}  // namespace sigurdos
