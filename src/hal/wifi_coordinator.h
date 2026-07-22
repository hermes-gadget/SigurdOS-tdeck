// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

namespace sigurdos::wifi {

enum class Owner : uint8_t {
    None,
    Sta,
    Scan,
    ApOta,
    GitHubOta,
};

enum class RadioMode : uint8_t {
    Off,
    Sta,
    Ap,
    ApSta,
};

struct ReleasePlan {
    bool released = false;
    Owner restored_owner = Owner::None;
    RadioMode restored_mode = RadioMode::Off;
};

// Main-loop-only WiFi ownership state. A persistent STA lease may be
// temporarily suspended by one scan or OTA lease; all other pairings conflict.
class Coordinator {
public:
    bool canAcquire(Owner requested) const {
        if (requested == Owner::None) return false;
        const Owner active = currentOwner();
        return active == Owner::None || active == requested ||
               (active == Owner::Sta && requested != Owner::Sta);
    }

    bool acquire(Owner requested, RadioMode requested_mode,
                 RadioMode observed_mode) {
        if (!canAcquire(requested)) return false;
        if (currentOwner() == requested) {
            return currentMode() == requested_mode;
        }
        if (depth_ >= MAX_DEPTH) return false;

        frames_[depth_++] = {requested, requested_mode, observed_mode};
        return true;
    }

    ReleasePlan release(Owner owner) {
        ReleasePlan plan{};
        if (depth_ == 0 || frames_[depth_ - 1].owner != owner) return plan;

        const Frame released = frames_[--depth_];
        plan.released = true;
        plan.restored_owner = currentOwner();
        plan.restored_mode = depth_ == 0 ? released.observed_mode
                                         : frames_[depth_ - 1].requested_mode;
        idle_mode_ = plan.restored_mode;
        return plan;
    }

    Owner currentOwner() const {
        return depth_ == 0 ? Owner::None : frames_[depth_ - 1].owner;
    }

    RadioMode currentMode() const {
        return depth_ == 0 ? idle_mode_ : frames_[depth_ - 1].requested_mode;
    }

    uint8_t depth() const { return depth_; }

private:
    struct Frame {
        Owner owner;
        RadioMode requested_mode;
        RadioMode observed_mode;
    };

    static constexpr uint8_t MAX_DEPTH = 2;
    Frame frames_[MAX_DEPTH]{};
    uint8_t depth_ = 0;
    RadioMode idle_mode_ = RadioMode::Off;
};

// Production singleton helpers. All callers run on the Arduino/LVGL loop task.
bool acquire(Owner owner, RadioMode requested_mode);
bool release(Owner owner);
bool canAcquire(Owner owner);
Owner currentOwner();
const char* ownerName(Owner owner);

}  // namespace sigurdos::wifi
