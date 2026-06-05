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
    char conversation[SIGURDOS_MSG_CONVERSATION_LEN];
    char sender[SIGURDOS_MSG_SENDER_LEN];
    char text[SIGURDOS_MSG_TEXT_LEN];
    uint32_t timestamp;
    uint8_t sender_prefix[SIGURDOS_MSG_PREFIX_LEN];
    int16_t rssi;
    int8_t snr_quarters;
    bool is_self;
    bool is_channel;
    bool acked;
};

namespace detail {
static constexpr uint32_t MESSAGE_STORE_MAGIC = 0x534d5347; // "SMSG"
static constexpr uint8_t MESSAGE_STORE_VERSION = 1;
static constexpr size_t MESSAGE_STORE_RECORD_SIZE =
    SIGURDOS_MSG_CONVERSATION_LEN +
    SIGURDOS_MSG_SENDER_LEN +
    SIGURDOS_MSG_TEXT_LEN +
    4 +
    SIGURDOS_MSG_PREFIX_LEN +
    2 +
    1 +
    1;

bool storedMessageSameIdentity(const StoredMessage& a, const StoredMessage& b);
void storedMessageNormalize(StoredMessage& msg);
} // namespace detail

bool messageStoreBegin();
bool messageStoreClear();
bool messageStoreAppend(const StoredMessage& msg);
int  messageStoreLoadRecent(const char* conversation, StoredMessage* out, int max);
int  messageStoreLoadAll(StoredMessage* out, int max);
bool messageStoreMarkAcked(const char* conversation, uint32_t timestamp);
int  messageStoreCount();

#if !defined(ESP32_PLATFORM)
void messageStoreSetNativePath(const char* path);
#endif

} // namespace mesh
} // namespace sigurdos
