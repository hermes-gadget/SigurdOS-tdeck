// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "storage.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_partition.h>
#include <memory>
#include <new>

namespace sigurdos {

static bool s_storage_available = false;
static bool s_storage_init_called = false;
static bool s_storage_mounted = false;
static bool s_storage_warm_started = false;

// First-write SPIFFS GC absorption: the first write after mount can trigger
// a GC pass that blocks the writer for 10-90s on this encrypted partition
// (measured 9.5-90s for the first channel-store commit after boot). Running
// that write in a background task after the boot sequence keeps loopTask
// (and its 10s runtime watchdog) free of the stall, so runtime commits —
// channel store, contacts — stay fast.
#if defined(ESP32_PLATFORM)
static void storage_warm_task(void*) {
    // Boot's own SPIFFS readers (settings/chats/mesh) finish around t+5-8s;
    // wait for them so the warm write cannot stall boot-time reads.
    vTaskDelay(pdMS_TO_TICKS(12000));
    if (!s_storage_mounted) {
        vTaskDelete(nullptr);
        return;
    }
    for (int i = 0; i < 3; ++i) {
        File f = SPIFFS.open("/.warm", FILE_WRITE);
        if (!f) break;
        const uint8_t byte = static_cast<uint8_t>('w' + i);
        f.write(&byte, 1);
        f.close();
    }
    SPIFFS.remove("/.warm");
    vTaskDelete(nullptr);
}
#endif

enum class PartitionEraseState {
    Erased,
    ContainsData,
    Unknown,
};

static constexpr size_t PARTITION_SCAN_CHUNK_BYTES = 4096;

static PartitionEraseState classify_partition_erasure(
    const esp_partition_t* part)
{
    if (!part || part->size == 0) return PartitionEraseState::Unknown;

    // A short erased prefix is not proof that a partition is unused. Scan the
    // complete partition in flash-sector-sized chunks and fail closed if the
    // buffer cannot be allocated or any read fails.
    std::unique_ptr<uint8_t[]> buf(
        new (std::nothrow) uint8_t[PARTITION_SCAN_CHUNK_BYTES]);
    if (!buf) return PartitionEraseState::Unknown;

    size_t offset = 0;
    while (offset < part->size) {
        const size_t remaining = static_cast<size_t>(part->size) - offset;
        const size_t read_size = remaining < PARTITION_SCAN_CHUNK_BYTES
            ? remaining
            : PARTITION_SCAN_CHUNK_BYTES;
        if (esp_partition_read(part, offset, buf.get(), read_size) != ESP_OK) {
            return PartitionEraseState::Unknown;
        }
        for (size_t i = 0; i < read_size; ++i) {
            if (buf[i] != 0xFF) return PartitionEraseState::ContainsData;
        }
        offset += read_size;
        // Keep setup cooperative while a large blank partition is inspected.
        delay(0);
    }

    return PartitionEraseState::Erased;
}

bool storage_init()
{
    if (s_storage_init_called) return s_storage_available;
    s_storage_init_called = true;

    // Attempt a safe mount first — don't format, respect existing data.
    if (SPIFFS.begin(false)) {
        s_storage_available = true;
        s_storage_mounted = true;
        return true;
    }

    // Mount failed. Determine whether the partition is merely erased
    // (clean flash / factory reset — safe to format) or corrupt.
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr);
    if (!part) {
        Serial.println("[storage] SPIFFS partition not found — storage unavailable");
        s_storage_available = false;
        return false;
    }

    const PartitionEraseState erase_state = classify_partition_erasure(part);

    if (erase_state == PartitionEraseState::Erased) {
        Serial.println("[storage] SPIFFS partition is fully erased — formatting once");
        if (!SPIFFS.format()) {
            Serial.println("[storage] SPIFFS format failed — storage unavailable");
            s_storage_available = false;
            return false;
        }
        if (!SPIFFS.begin(false)) {
            Serial.println("[storage] SPIFFS mount after format failed — storage unavailable");
            s_storage_available = false;
            return false;
        }
        Serial.println("[storage] SPIFFS formatted and mounted");
        s_storage_available = true;
        s_storage_mounted = true;
        return true;
    }

    if (erase_state == PartitionEraseState::Unknown) {
        Serial.println("[storage] SPIFFS erased-state check failed — preserving partition");
        s_storage_available = false;
        return false;
    }

    // Partition has data but SPIFFS can't mount it — likely corruption.
    Serial.println("[storage] SPIFFS mount failed (partition contains non-erased data but is not a valid SPIFFS filesystem)");
    Serial.println("[storage] Storage unavailable — identity/contacts won't persist. Use factory reset to reformat.");
    s_storage_available = false;
    return false;
}

bool storage_available()
{
    return s_storage_available;
}

bool storage_ensure_mounted()
{
    if (!s_storage_available) return false;
    if (!s_storage_mounted) {
        if (!SPIFFS.begin(false)) return false;
        s_storage_mounted = true;
    }
    return true;
}

void storage_warm_after_mount()
{
    if (!s_storage_mounted || s_storage_warm_started) return;
    s_storage_warm_started = true;
#if defined(ESP32_PLATFORM)
    // 4096-byte stack; the warm does only a tiny bounded file op.
    xTaskCreate(storage_warm_task, "storage-warm", 4096, nullptr, 1, nullptr);
#endif
}

void storage_reset()
{
    s_storage_init_called = false;
    s_storage_available = false;
    s_storage_mounted = false;
    s_storage_warm_started = false;
}

} // namespace sigurdos
