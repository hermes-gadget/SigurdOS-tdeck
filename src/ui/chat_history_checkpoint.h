// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace sigurdos::ui {

class ChatHistoryCheckpoint {
public:
    static constexpr uint32_t DEBOUNCE_MS = 5000;

    void markDirty(uint32_t now) {
        if (!_dirty) _dirty_since = now;
        _dirty = true;
    }

    bool isDue(uint32_t now) const {
        return _dirty && static_cast<uint32_t>(now - _dirty_since) >= DEBOUNCE_MS;
    }

    void saved() { _dirty = false; }
    bool dirty() const { return _dirty; }

private:
    bool _dirty = false;
    uint32_t _dirty_since = 0;
};

}  // namespace sigurdos::ui
