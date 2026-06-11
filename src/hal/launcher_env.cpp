// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Launcher environment detection implementation.
//
// Uses the ESP-IDF partition API to probe for Launcher's resident
// `test`-subtype app partition and its otadata location at 0xD000.

#include "launcher_env.h"
#include <esp_partition.h>

namespace {

constexpr uint32_t LAUNCHER_OTADATA_OFFSET = 0xD000;

bool has_launcher_resident_partition()
{
    esp_partition_iterator_t it = esp_partition_find(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_TEST,
        NULL);
    bool found = (it != NULL);
    if (it) {
        esp_partition_iterator_release(it);
    }
    return found;
}

bool has_launcher_otadata_partition()
{
    esp_partition_iterator_t it = esp_partition_find(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_DATA_OTA),
        NULL);
    if (!it) {
        return false;
    }

    const esp_partition_t* partition = esp_partition_get(it);
    bool found = partition && partition->address == LAUNCHER_OTADATA_OFFSET;
    esp_partition_iterator_release(it);
    return found;
}

} // namespace

bool sigurdos_is_under_launcher()
{
    // The resident test app partition is the primary signal, while
    // Launcher's 0xD000 otadata location confirms this is not another
    // custom table that happens to contain an APP_TEST slot.
    return has_launcher_resident_partition() && has_launcher_otadata_partition();
}

const char* sigurdos_launcher_env_name()
{
    return sigurdos_is_under_launcher()
        ? "bmorcelli/Launcher"
        : "standalone";
}
