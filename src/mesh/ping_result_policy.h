// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstring>

namespace sigurdos::mesh {

inline bool pingWindowActive(uint32_t sent_at, uint32_t now, uint32_t window_ms) {
    return sent_at != 0 && now - sent_at < window_ms;
}

template <typename Result>
bool recordUnauthenticatedPingHint(Result* results, int& count, int capacity,
                                   const char* name, int local_rssi) {
    if (!results || !name || !name[0] || count < 0 || capacity <= 0 || count > capacity) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(results[i].name, name) == 0) {
            results[i].rssi = local_rssi;
            return true;
        }
    }
    if (count == capacity) return false;
    const size_t name_len = std::strlen(name);
    if (name_len >= sizeof(results[count].name)) return false;
    std::memcpy(results[count].name, name, name_len + 1);
    results[count].rssi = local_rssi;
    ++count;
    return true;
}

}  // namespace sigurdos::mesh
