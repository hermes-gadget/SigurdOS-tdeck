// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

// Size of the RepeaterStats payload after the four-byte response tag.
#define NODE_STATUS_RESPONSE_SIZE 56

namespace sigurdos::mesh {

static constexpr size_t NODE_STATUS_TAG_SIZE = sizeof(uint32_t);
static constexpr size_t NODE_STATUS_WIRE_SIZE =
    NODE_STATUS_TAG_SIZE + NODE_STATUS_RESPONSE_SIZE;

struct NodeStatus {
    uint16_t batt_milli_volts;       // battery voltage in mV
    uint16_t curr_tx_queue_len;      // current TX queue length
    int16_t noise_floor;             // noise floor (dBm)
    int16_t last_rssi;               // last received RSSI (dBm)
    uint32_t n_packets_recv;         // total packets received
    uint32_t n_packets_sent;         // total packets sent
    uint32_t total_air_time_secs;    // total TX air time (seconds)
    uint32_t total_up_time_secs;     // node uptime (seconds)
    uint32_t n_sent_flood;           // flood messages sent
    uint32_t n_sent_direct;          // direct messages sent
    uint32_t n_recv_flood;           // flood messages received
    uint32_t n_recv_direct;          // direct messages received
    uint16_t err_events;             // error event count
    int16_t last_snr;                // last SNR (value/4 = dB)
    uint16_t n_direct_dups;          // duplicate direct packets
    uint16_t n_flood_dups;           // duplicate flood packets
    uint32_t total_rx_air_time_secs; // total RX air time (seconds)
    uint32_t n_recv_errors;          // receive errors
};

namespace detail {

inline uint16_t readStatusU16(const uint8_t* data, size_t offset)
{
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

inline int16_t readStatusI16(const uint8_t* data, size_t offset)
{
    return static_cast<int16_t>(readStatusU16(data, offset));
}

inline uint32_t readStatusU32(const uint8_t* data, size_t offset)
{
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

// Parses a complete contact response: four-byte tag followed by the exact
// 56-byte RepeaterStats payload. Malformed lengths are rejected so a partial
// response can never be exposed as valid status data.
inline bool parseNodeStatusResponse(const uint8_t* data, size_t len,
                                    NodeStatus* out)
{
    if (!data || !out || len != NODE_STATUS_WIRE_SIZE) return false;

    const uint8_t* blob = data + NODE_STATUS_TAG_SIZE;
    NodeStatus parsed{};
    parsed.batt_milli_volts = readStatusU16(blob, 0);
    parsed.curr_tx_queue_len = readStatusU16(blob, 2);
    parsed.noise_floor = readStatusI16(blob, 4);
    parsed.last_rssi = readStatusI16(blob, 6);
    parsed.n_packets_recv = readStatusU32(blob, 8);
    parsed.n_packets_sent = readStatusU32(blob, 12);
    parsed.total_air_time_secs = readStatusU32(blob, 16);
    parsed.total_up_time_secs = readStatusU32(blob, 20);
    parsed.n_sent_flood = readStatusU32(blob, 24);
    parsed.n_sent_direct = readStatusU32(blob, 28);
    parsed.n_recv_flood = readStatusU32(blob, 32);
    parsed.n_recv_direct = readStatusU32(blob, 36);
    parsed.err_events = readStatusU16(blob, 40);
    parsed.last_snr = readStatusI16(blob, 42);
    parsed.n_direct_dups = readStatusU16(blob, 44);
    parsed.n_flood_dups = readStatusU16(blob, 46);
    parsed.total_rx_air_time_secs = readStatusU32(blob, 48);
    parsed.n_recv_errors = readStatusU32(blob, 52);

    *out = parsed;
    return true;
}

} // namespace detail
} // namespace sigurdos::mesh
