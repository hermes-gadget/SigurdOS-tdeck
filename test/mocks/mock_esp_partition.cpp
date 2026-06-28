// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Mock ESP partition API for native testing.

#include "esp_partition.h"
#include <cstddef>
#include <cstring>

// ── Mock state ───────────────────────────────────────
static bool s_has_test_partition = false;
static bool s_has_spiffs_partition = false;
static bool s_spiffs_erased = false;
static bool s_has_otadata_partition = true;
static esp_partition_t s_test_partition = {
    ESP_PARTITION_TYPE_APP,
    ESP_PARTITION_SUBTYPE_APP_TEST,
    0x10000,
    0x180000,
    "app0",
};
static esp_partition_t s_spiffs_partition = {
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
    0x290000,
    0x160000,
    "spiffs",
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

void mock_spiffs_partition(bool present, bool erased) {
    s_has_spiffs_partition = present;
    s_spiffs_erased = erased;
}
} // namespace test
} // namespace sigurdos

// ── ESP-IDF API stubs ─────────────────────────────────

extern "C" {

const esp_partition_t* esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char* label)
{
    (void)label;
    if (type == ESP_PARTITION_TYPE_APP &&
        subtype == ESP_PARTITION_SUBTYPE_APP_TEST) {
        return s_has_test_partition ? &s_test_partition : nullptr;
    }

    if (type == ESP_PARTITION_TYPE_DATA &&
        subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS) {
        return s_has_spiffs_partition ? &s_spiffs_partition : nullptr;
    }

    // otadata lookup (for OTA rollback in native tests)
    if (type == ESP_PARTITION_TYPE_DATA &&
        subtype == ESP_PARTITION_SUBTYPE_DATA_OTA) {
        return s_has_otadata_partition ? &s_otadata_partition : nullptr;
    }
    return nullptr;
}

esp_err_t esp_partition_read(
    const esp_partition_t* partition,
    size_t src_offset,
    void* dst,
    size_t size)
{
    if (!partition || !dst) return ESP_ERR_INVALID_ARG;
    if (src_offset + size > partition->size) return ESP_ERR_INVALID_ARG;

    // Fill with 0xFF if "erased", or 0x00 if "not erased" (simulate data).
    if (partition->type == ESP_PARTITION_TYPE_DATA &&
        partition->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS) {
        if (s_spiffs_erased) {
            memset(dst, 0xFF, size);
        } else {
            memset(dst, 0x00, size);  // non-erased: arbitrary non-0xFF data
        }
        return ESP_OK;
    }

    // Default: fill with 0xFF (generic erased)
    memset(dst, 0xFF, size);
    return ESP_OK;
}

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
        subtype == ESP_PARTITION_SUBTYPE_DATA_OTA &&
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

} // extern "C"
