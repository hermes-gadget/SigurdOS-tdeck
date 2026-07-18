#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace mesh {

// Keeps the persisted/default scope separate from the companion session's
// persistent override. A one-shot unscoped send is retained for local callers.
class FloodScopeState {
public:
    enum class OverrideMode : uint8_t {
        Default,
        Scoped,
        Unscoped,
    };

    void setDefault(const uint8_t* key16) {
        copyOrClear(_default_key, key16);
    }

    void setOverride(const uint8_t* key16, bool unscoped) {
        if (unscoped) {
            _override_mode = OverrideMode::Unscoped;
            std::memset(_override_key, 0, sizeof(_override_key));
        } else if (key16) {
            _override_mode = OverrideMode::Scoped;
            std::memcpy(_override_key, key16, sizeof(_override_key));
        } else {
            clearOverride();
        }
    }

    void clearOverride() {
        _override_mode = OverrideMode::Default;
        std::memset(_override_key, 0, sizeof(_override_key));
    }

    OverrideMode overrideMode() const { return _override_mode; }

    bool copyDefault(uint8_t* key16) const {
        return copyNonNull(_default_key, key16);
    }

    bool copyOverride(uint8_t* key16) const {
        return _override_mode == OverrideMode::Scoped &&
            copyNonNull(_override_key, key16);
    }

    void setUnscopedOnce(bool enabled) { _unscoped_once = enabled; }

    // Resolve the effective scope for an ordinary flood. Returns true and
    // copies a key for a scoped send; false means wildcard/unscoped.
    bool takeEffective(uint8_t* key16) {
        if (_unscoped_once) {
            _unscoped_once = false;
            return false;
        }
        if (_override_mode == OverrideMode::Unscoped) return false;
        if (_override_mode == OverrideMode::Scoped) {
            return copyNonNull(_override_key, key16);
        }
        return copyNonNull(_default_key, key16);
    }

private:
    static bool isNull(const uint8_t* key16) {
        if (!key16) return true;
        uint8_t combined = 0;
        for (size_t i = 0; i < 16; ++i) combined |= key16[i];
        return combined == 0;
    }

    static void copyOrClear(uint8_t* destination, const uint8_t* source) {
        if (source) std::memcpy(destination, source, 16);
        else std::memset(destination, 0, 16);
    }

    static bool copyNonNull(const uint8_t* source, uint8_t* destination) {
        if (!destination || isNull(source)) return false;
        std::memcpy(destination, source, 16);
        return true;
    }

    uint8_t _default_key[16]{};
    uint8_t _override_key[16]{};
    OverrideMode _override_mode = OverrideMode::Default;
    bool _unscoped_once = false;
};

} // namespace mesh
} // namespace sigurdos
