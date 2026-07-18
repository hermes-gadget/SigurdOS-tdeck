#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>

namespace sigurdos::mesh::telemetry_policy {

constexpr uint8_t MODE_DENY = 0;
constexpr uint8_t MODE_ALLOW_FLAGS = 1;
constexpr uint8_t MODE_ALLOW_ALL = 2;

constexpr uint8_t PERM_BASE = 0x01;
constexpr uint8_t PERM_LOCATION = 0x02;
constexpr uint8_t PERM_ENVIRONMENT = 0x04;

constexpr uint8_t LPP_VOLTAGE_TYPE = 116;
constexpr uint8_t LPP_GPS_TYPE = 136;
constexpr uint8_t LPP_SELF_CHANNEL = 1;

struct GpsSample {
    bool valid;
    float latitude;
    float longitude;
    float altitude_m;
};

inline uint8_t permissionForMode(uint8_t mode, uint8_t contact_permissions,
                                 uint8_t permission) {
    if (mode == MODE_ALLOW_ALL) return permission;
    if (mode == MODE_ALLOW_FLAGS) return contact_permissions & permission;
    return 0;
}

// C-14/E-04: decode the three packed 2-bit modes and apply the request's
// inverse mask. Contact telemetry permissions live above the favourite bit.
inline uint8_t resolvePermissions(uint8_t packed_modes, uint8_t contact_flags,
                                  uint8_t inverse_request_mask) {
    const uint8_t contact_permissions = contact_flags >> 1;
    uint8_t permissions = permissionForMode(
        packed_modes & 0x03, contact_permissions, PERM_BASE);
    permissions |= permissionForMode(
        (packed_modes >> 2) & 0x03, contact_permissions, PERM_LOCATION);
    permissions |= permissionForMode(
        (packed_modes >> 4) & 0x03, contact_permissions, PERM_ENVIRONMENT);
    return permissions & static_cast<uint8_t>(~inverse_request_mask);
}

inline void writeU24(uint8_t* out, int32_t value) {
    out[0] = static_cast<uint8_t>(value >> 16);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value);
}

inline size_t appendPayload(uint8_t* out, size_t capacity, uint8_t permissions,
                            uint16_t battery_mv, const GpsSample& gps) {
    if (!out || !(permissions & PERM_BASE) || capacity < 4) return 0;

    size_t pos = 0;
    out[pos++] = LPP_SELF_CHANNEL;
    out[pos++] = LPP_VOLTAGE_TYPE;
    const uint16_t centivolts = battery_mv / 10;
    out[pos++] = static_cast<uint8_t>(centivolts >> 8);
    out[pos++] = static_cast<uint8_t>(centivolts);

    if ((permissions & PERM_LOCATION) && gps.valid && capacity - pos >= 11) {
        out[pos++] = LPP_SELF_CHANNEL;
        out[pos++] = LPP_GPS_TYPE;
        writeU24(&out[pos], static_cast<int32_t>(gps.latitude * 10000.0f));
        pos += 3;
        writeU24(&out[pos], static_cast<int32_t>(gps.longitude * 10000.0f));
        pos += 3;
        writeU24(&out[pos], static_cast<int32_t>(gps.altitude_m * 100.0f));
        pos += 3;
    }

    // No environmental sensor is fitted to the T-Deck. Future environment
    // data must be appended only when PERM_ENVIRONMENT is present.
    return pos;
}

// C-14/A-12: RF responses reflect the request timestamp before the LPP body.
inline size_t buildResponse(uint8_t* out, size_t capacity, uint32_t sender_timestamp,
                            uint8_t permissions, uint16_t battery_mv,
                            const GpsSample& gps) {
    if (!out || !(permissions & PERM_BASE) || capacity < 8) return 0;
    out[0] = static_cast<uint8_t>(sender_timestamp);
    out[1] = static_cast<uint8_t>(sender_timestamp >> 8);
    out[2] = static_cast<uint8_t>(sender_timestamp >> 16);
    out[3] = static_cast<uint8_t>(sender_timestamp >> 24);
    const size_t payload_len = appendPayload(
        out + 4, capacity - 4, permissions, battery_mv, gps);
    return payload_len == 0 ? 0 : 4 + payload_len;
}

}  // namespace sigurdos::mesh::telemetry_policy
