// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "storage.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_partition.h>

namespace sigurdos {

static bool s_storage_available = false;
static bool s_storage_init_called = false;

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

    // Read the first 64 bytes to check if the partition is all 0xFF (erased).
    uint8_t buf[64];
    esp_err_t err = esp_partition_read(part, 0, buf, sizeof(buf));
    bool erased = (err == ESP_OK);
    if (erased) {
        for (size_t i = 0; i < sizeof(buf); i++) {
            if (buf[i] != 0xFF) {
                erased = false;
                break;
            }
        }
    }

    if (erased) {
        Serial.println("[storage] SPIFFS partition appears erased — formatting once");
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
