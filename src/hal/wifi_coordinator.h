// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <atomic>
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
    Owner released_owner = Owner::None;
    Owner restored_owner = Owner::None;
    RadioMode restored_mode = RadioMode::Off;
    uint8_t depth = 0;
};

// A worker task may request cleanup, but coordinator ownership transitions and
// restoration of the prior WiFi mode remain on the Arduino/LVGL loop task.
// Requests are coalesced by owner.
inline uint8_t releaseRequestMask(Owner owner) {
    return owner == Owner::None
        ? 0
        : static_cast<uint8_t>(1U << static_cast<uint8_t>(owner));
}

class ReleaseRequestQueue {
public:
    void request(Owner owner) {
        const uint8_t bit = releaseRequestMask(owner);
        if (bit != 0) pending_.fetch_or(bit, std::memory_order_release);
    }

    uint8_t take() {
        return pending_.exchange(0, std::memory_order_acq_rel);
    }

private:
    std::atomic<uint8_t> pending_{0};
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

    // Prepare is deliberately non-mutating. The production wrapper applies
    // the hardware mode in between this step and commitRelease().
    ReleasePlan prepareRelease(Owner owner) const {
        ReleasePlan plan{};
        if (depth_ == 0 || frames_[depth_ - 1].owner != owner) return plan;

        plan.released = true;
        plan.released_owner = owner;
        plan.depth = depth_;
        plan.restored_owner = depth_ == 1 ? Owner::None
                                          : frames_[depth_ - 2].owner;
        plan.restored_mode = depth_ == 1 ? frames_[depth_ - 1].observed_mode
                                         : frames_[depth_ - 2].requested_mode;
        return plan;
    }

    bool commitRelease(const ReleasePlan& plan) {
        if (!plan.released || depth_ == 0 || plan.depth != depth_ ||
            frames_[depth_ - 1].owner != plan.released_owner) {
            return false;
        }

        --depth_;
        idle_mode_ = plan.restored_mode;
        return true;
    }

    // Compatibility helper for pure state-machine callers. Production code
    // uses releaseWith() so a driver failure cannot discard the lease.
    ReleasePlan release(Owner owner) {
        const ReleasePlan plan = prepareRelease(owner);
        if (plan.released) (void)commitRelease(plan);
        return plan;
    }

    template <typename ApplyMode>
    bool releaseWith(Owner owner, ApplyMode apply_mode) {
        const ReleasePlan plan = prepareRelease(owner);
        if (!plan.released || !apply_mode(plan.restored_mode)) return false;
        return commitRelease(plan);
    }

    Owner currentOwner() const {
        return depth_ == 0 ? Owner::None : frames_[depth_ - 1].owner;
    }

    RadioMode currentMode() const {
        return depth_ == 0 ? idle_mode_ : frames_[depth_ - 1].requested_mode;
    }

    bool hasOwner(Owner owner) const {
        for (uint8_t i = 0; i < depth_; ++i) {
            if (frames_[i].owner == owner) return true;
        }
        return false;
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
// Reserve the coordinator lease without changing hardware mode. The caller
// must run on loopTask; a managed worker may then perform the hardware setup
// and requestRelease() when finished.
bool reserveForWorker(Owner owner, RadioMode requested_mode);
bool release(Owner owner);
bool canAcquire(Owner owner);
Owner currentOwner();
bool hasOwner(Owner owner);
const char* ownerName(Owner owner);

// Worker-safe release request API. Only servicePendingReleases() invokes the
// coordinator and WiFi driver, and it must be called from the main loop task.
void requestRelease(Owner owner);
void servicePendingReleases();

}  // namespace sigurdos::wifi
