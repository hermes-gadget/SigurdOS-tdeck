// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

static constexpr size_t SIGURDOS_MSG_CONVERSATION_LEN = 32;
static constexpr size_t SIGURDOS_MSG_SENDER_LEN = 32;
static constexpr size_t SIGURDOS_MSG_TEXT_LEN = 160;
static constexpr size_t SIGURDOS_MSG_PREFIX_LEN = 6;

struct StoredMessage {
    // Monotonic store ID assigned at append time — used for per-record
    // companion delivery tracking (a CMD_SYNC_NEXT_MESSAGE that successfully
    // writes the frame marks only this specific record as sent).
    uint32_t store_id;
    char conversation[SIGURDOS_MSG_CONVERSATION_LEN];
    char sender[SIGURDOS_MSG_SENDER_LEN];
    char text[SIGURDOS_MSG_TEXT_LEN];
    uint32_t timestamp;
    uint8_t sender_prefix[SIGURDOS_MSG_PREFIX_LEN];
    int16_t rssi;
    int8_t snr_quarters;
    // Companion path-length byte for the V3 receive frame. Encodes hop count
    // (low 6 bits) and hash size (high 2 bits) exactly as MeshCore expects:
    // pkt->isRouteFlood() ? pkt->path_len : 0xFF. 0xFF means "direct/unknown".
    uint8_t path_len;
    // MeshCore companion text type — COMPANION_TXT_PLAIN (0),
    // COMPANION_TXT_CLI_DATA (1), or COMPANION_TXT_SIGNED_PLAIN (2).
    uint8_t txt_type;
    // Length of the extra bytes (e.g. 4 for signed message sender prefix).
    uint8_t extra_len;
    // Extra data for the companion frame (signed message: 4-byte sender prefix
    // before text; CLI: unused). Zero-padded.
    uint8_t extra[8];
    bool is_self;
    bool is_channel;
    bool acked;
    bool companion_sent;
};

namespace detail {
static constexpr uint32_t MESSAGE_STORE_MAGIC = 0x534d5347; // "SMSG"
// v5 adds a non-zero monotonic next-ID field to the file header. Version-4
// records are migrated by assigning fresh non-zero IDs in chronological order.
static constexpr uint8_t MESSAGE_STORE_VERSION = 5;
static constexpr size_t MESSAGE_STORE_V4_HEADER_SIZE = 9;
static constexpr size_t MESSAGE_STORE_HEADER_SIZE = 13;
static constexpr size_t MESSAGE_STORE_RECORD_SIZE =
    4 +  // store_id
    SIGURDOS_MSG_CONVERSATION_LEN +
    SIGURDOS_MSG_SENDER_LEN +
    SIGURDOS_MSG_TEXT_LEN +
    4 +
    SIGURDOS_MSG_PREFIX_LEN +
    2 +
    1 +   // snr_quarters
    1 +   // path_len
    1 +   // txt_type
    1 +   // extra_len
    8 +   // extra
    1;    // flags

bool storedMessageSameIdentity(const StoredMessage& a, const StoredMessage& b);
void storedMessageNormalize(StoredMessage& msg);
} // namespace detail

bool messageStoreBegin();
bool messageStoreClear();
bool messageStoreAppend(const StoredMessage& msg, uint32_t* store_id_out = nullptr);
int  messageStoreLoadRecent(const char* conversation, StoredMessage* out, int max);
int  messageStoreLoadAll(StoredMessage* out, int max);
bool messageStoreMarkAcked(const char* conversation, uint32_t timestamp);
bool messageStoreMarkCompanionSent(uint32_t store_id);
int  messageStoreLoadUnsent(StoredMessage* out, int max);
int  messageStoreCount();

#if !defined(ESP32_PLATFORM)
void messageStoreSetNativePath(const char* path);
#endif

} // namespace mesh
} // namespace sigurdos
