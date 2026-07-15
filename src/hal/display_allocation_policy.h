// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace sigurdos {
namespace hal {

struct DisplayAllocationOps {
    void* context;
    void* (*create_display)(void*, int32_t, int32_t);
    void* (*create_input)(void*);
    void* (*create_group)(void*);
    void (*delete_input)(void*, void*);
};

struct DisplayInputObjects {
    void* touch;
    void* keyboard;
    void* trackball;
    void* group;
    bool group_allocation_failed;
};

inline bool allocate_display_object(const DisplayAllocationOps& ops,
                                    int32_t width, int32_t height,
                                    void** display)
{
    if (!display) return false;
    *display = nullptr;
    if (!ops.create_display) return false;
    *display = ops.create_display(ops.context, width, height);
    return *display != nullptr;
}

inline DisplayInputObjects allocate_display_inputs(const DisplayAllocationOps& ops)
{
    DisplayInputObjects result{nullptr, nullptr, nullptr, nullptr, false};
    if (!ops.create_input) return result;

    // Each class is independent: failure of one must not suppress the others.
    result.touch = ops.create_input(ops.context);
    result.keyboard = ops.create_input(ops.context);
    result.trackball = ops.create_input(ops.context);

    // Keyboard and encoder devices require a group. If it cannot be created,
    // discard only those unusable devices and retain touch input.
    if (result.keyboard || result.trackball) {
        if (ops.create_group) result.group = ops.create_group(ops.context);
        if (!result.group) {
            result.group_allocation_failed = true;
            if (ops.delete_input) {
                if (result.keyboard) ops.delete_input(ops.context, result.keyboard);
                if (result.trackball) ops.delete_input(ops.context, result.trackball);
            }
            result.keyboard = nullptr;
            result.trackball = nullptr;
        }
    }

    return result;
}

}  // namespace hal
}  // namespace sigurdos
