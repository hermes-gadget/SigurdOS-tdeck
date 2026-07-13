// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "storage.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_partition.h>

namespace sigurdos {

static bool s_storage_available = false;
static bool s_storage_init_called = false;

enum class PartitionEraseState : uint8_t {
    FullyErased,
    ContainsData,
    ReadError,
};

static PartitionEraseState partition_erase_state(const esp_partition_t* part)
{
    if (!part || part->size == 0) return PartitionEraseState::ReadError;

    // A short prefix is not evidence that the filesystem is blank: an
    // interrupted erase can leave early sectors at 0xFF while later sectors
    // still contain user data. Scan the complete partition in bounded chunks
    // and treat read errors as uncertainty, never as permission to format.
    uint8_t buf[1024];
    for (size_t offset = 0; offset < part->size;) {
        const size_t remaining = part->size - offset;
        const size_t length = remaining < sizeof(buf) ? remaining : sizeof(buf);
        if (esp_partition_read(part, offset, buf, length) != ESP_OK) {
            return PartitionEraseState::ReadError;
        }
        for (size_t i = 0; i < length; i++) {
            if (buf[i] != 0xFF) return PartitionEraseState::ContainsData;
        }
        offset += length;
    }
    return PartitionEraseState::FullyErased;
}

bool storage_init()
{
    if (s_storage_init_called) return s_storage_available;
    s_storage_init_called = true;

    // Attempt a safe mount first — don't format, respect existing data.
    if (SPIFFS.begin(false)) {
        s_storage_available = true;
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

    const PartitionEraseState erase_state = partition_erase_state(part);
    if (erase_state == PartitionEraseState::FullyErased) {
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
        return true;
    }

    if (erase_state == PartitionEraseState::ReadError) {
        Serial.println("[storage] Could not verify the complete SPIFFS partition — refusing to format");
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

void storage_reset()
{
    s_storage_init_called = false;
    s_storage_available = false;
}

} // namespace sigurdos
