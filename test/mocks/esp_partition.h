#pragma once

#include <cstdint>
#include <cstddef>

// Minimal mock of the ESP-IDF partition API for native testing.

#ifdef __cplusplus
extern "C" {
#endif

// -- Types (minimal subset) --

typedef struct {
    uint32_t type;
    uint32_t subtype;
    uint32_t address;
    uint32_t size;
    char label[16];
} esp_partition_t;

typedef enum {
    ESP_PARTITION_TYPE_APP = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_2 = 0x12,
    ESP_PARTITION_SUBTYPE_APP_OTA_3 = 0x13,
    ESP_PARTITION_SUBTYPE_APP_OTA_4 = 0x14,
    ESP_PARTITION_SUBTYPE_APP_OTA_5 = 0x15,
    ESP_PARTITION_SUBTYPE_APP_OTA_6 = 0x16,
    ESP_PARTITION_SUBTYPE_APP_OTA_7 = 0x17,
    ESP_PARTITION_SUBTYPE_APP_TEST = 0x20,
} esp_partition_subtype_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,
    ESP_PARTITION_SUBTYPE_DATA_PHY = 0x01,
    ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82,
    ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81,
} esp_partition_subtype_data_t;

typedef void* esp_partition_iterator_t;

// -- Functions --

// Find partition by type, subtype, label (label can be NULL).
// Returns iterator handle, or NULL if not found.
// Mock: test may set the mock result before calling.
esp_partition_iterator_t esp_partition_find(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char* label);

// Release the iterator.
void esp_partition_iterator_release(esp_partition_iterator_t iterator);

// Get the partition info from the iterator.
const esp_partition_t* esp_partition_get(esp_partition_iterator_t iterator);

#ifdef __cplusplus
}
#endif

// ── Test control interface ───────────────────────────
// These are NOT part of the ESP-IDF API. They exist only
// in the mock to let tests control what esp_partition_find returns.

#ifdef __cplusplus
namespace sigurdos {
namespace test {

// Set whether esp_partition_find should return a non-NULL result
// when called with ESP_PARTITION_TYPE_APP / ESP_PARTITION_SUBTYPE_APP_TEST / NULL.
void mock_launcher_partition(bool present);

// Set whether esp_partition_find should return an otadata partition and which
// address esp_partition_get should expose for it.
void mock_otadata_partition(bool present, uint32_t address);

} // namespace test
} // namespace sigurdos
#endif
