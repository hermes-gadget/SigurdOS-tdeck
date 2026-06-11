// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Mock ESP partition API for native testing.

#include "esp_partition.h"
#include <cstddef>

// ── Mock state ───────────────────────────────────────
static bool s_has_test_partition = false;
static bool s_has_otadata_partition = true;
static esp_partition_t s_test_partition = {
    ESP_PARTITION_TYPE_APP,
    ESP_PARTITION_SUBTYPE_APP_TEST,
    0x10000,
    0x180000,
    "app0",
};
static esp_partition_t s_otadata_partition = {
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_OTA,
    0xE000,
    0x2000,
    "otadata",
};

namespace sigurdos {
namespace test {
void mock_launcher_partition(bool present) {
    s_has_test_partition = present;
}

void mock_otadata_partition(bool present, uint32_t address) {
    s_has_otadata_partition = present;
    s_otadata_partition.address = address;
}
} // namespace test
} // namespace sigurdos

// ── ESP-IDF API stubs ─────────────────────────────────
esp_partition_iterator_t esp_partition_find(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char* label)
{
    if (type == ESP_PARTITION_TYPE_APP &&
        subtype == ESP_PARTITION_SUBTYPE_APP_TEST &&
        label == NULL) {
        return s_has_test_partition
            ? reinterpret_cast<esp_partition_iterator_t>(&s_test_partition)
            : NULL;
    }

    if (type == ESP_PARTITION_TYPE_DATA &&
        subtype == static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_DATA_OTA) &&
        label == NULL) {
        return s_has_otadata_partition
            ? reinterpret_cast<esp_partition_iterator_t>(&s_otadata_partition)
            : NULL;
    }
    return NULL;
}

void esp_partition_iterator_release(esp_partition_iterator_t iterator)
{
    // No-op in mock.
    (void)iterator;
}

const esp_partition_t* esp_partition_get(esp_partition_iterator_t iterator)
{
    return reinterpret_cast<const esp_partition_t*>(iterator);
}
