// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "message_store.h"

#include "hal/atomic_file.h"

#include <cstdlib>
#include <cstring>

#if defined(ESP32_PLATFORM)
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include "hal/storage.h"
#else
#include <cstdio>
#endif

namespace sigurdos {
namespace mesh {

namespace {

#if defined(ESP32_PLATFORM)
static constexpr const char* STORE_PATH = "/companion_msgs";
#endif

static constexpr uint32_t MESSAGE_STORE_RECOVERY_MAX_RECORDS =
    MESSAGE_STORE_MAX_RECORDS + 1;

#if !defined(ESP32_PLATFORM)
static char g_native_path[160] = "/tmp/sigurdos_companion_msgs.bin";

static void copyZ(char* dest, size_t dest_sz, const char* src)
{
    if (!dest || dest_sz == 0) return;
    if (!src) src = "";
    std::strncpy(dest, src, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
}
#endif

static uint8_t flagsFor(const StoredMessage& msg)
{
    uint8_t flags = 0;
    if (msg.is_self) flags |= 0x01;
    if (msg.is_channel) flags |= 0x02;
    if (msg.acked) flags |= 0x04;
    if (msg.companion_sent) flags |= 0x08;
    if (msg.confirmation_lost) flags |= 0x10;
    if (msg.route_flood) flags |= 0x10;
    if (msg.route_known) flags |= 0x20;
    if (msg.attempt_known) flags |= 0x40;