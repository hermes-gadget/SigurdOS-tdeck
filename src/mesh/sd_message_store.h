// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::mesh {

// SD retains roughly ten times the SPIFFS history while leaving a compaction
// headroom batch so normal appends remain O(1).
static constexpr uint32_t SD_MESSAGE_STORE_MAX_RECORDS = 5000;
static constexpr uint32_t SD_MESSAGE_STORE_COMPACT_TO_RECORDS = 4480;
static constexpr const char* SD_MESSAGE_STORE_PATH = "/sdcard/msgs";

// Select the durable message backend after storage and SD probing. If SD is
// unavailable or cannot be opened, the existing SPIFFS store remains active
// and the degraded flag is set. A successful selection also migrates any
// records found in the bounded SPIFFS store into SD using the normal dedup
// identity rules.
bool sdMessageStoreSelect(bool spiffs_available);
bool sdMessageStoreUsingSd();
bool sdMessageStoreDegraded();
uint32_t sdMessageStoreCapacity();
uint64_t sdMessageStoreFreeBytes();

#if !defined(ESP32_PLATFORM)
// Native-only controls let the SD backend tests model a mounted/unmounted
// card and use an isolated directory without changing production APIs.
void sdMessageStoreSetNativeRoot(const char* root);
void sdMessageStoreSetNativeMounted(bool mounted);
void sdMessageStoreSetNativeAppendWriteLimit(int bytes);
void sdMessageStoreResetNative();
const char* sdMessageStoreNativePath();
#endif

} // namespace sigurdos::mesh
