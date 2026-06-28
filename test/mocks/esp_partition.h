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

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_ARG 0x102

// Partition types and subtypes — using #defines like real ESP-IDF
// to avoid C++ enum type conflicts between subtype categories.
typedef uint32_t esp_partition_type_t;
typedef uint32_t esp_partition_subtype_t;

#define ESP_PARTITION_TYPE_APP           0x00
#define ESP_PARTITION_TYPE_DATA          0x01

// APP subtypes
#define ESP_PARTITION_SUBTYPE_APP_OTA_0  0x10
#define ESP_PARTITION_SUBTYPE_APP_OTA_1  0x11
#define ESP_PARTITION_SUBTYPE_APP_OTA_2  0x12
#define ESP_PARTITION_SUBTYPE_APP_OTA_3  0x13
#define ESP_PARTITION_SUBTYPE_APP_OTA_4  0x14
#define ESP_PARTITION_SUBTYPE_APP_OTA_5  0x15
#define ESP_PARTITION_SUBTYPE_APP_OTA_6  0x16
#define ESP_PARTITION_SUBTYPE_APP_OTA_7  0x17
#define ESP_PARTITION_SUBTYPE_APP_TEST   0x20

// DATA subtypes
#define ESP_PARTITION_SUBTYPE_DATA_OTA    0x00
#define ESP_PARTITION_SUBTYPE_DATA_PHY    0x01
#define ESP_PARTITION_SUBTYPE_DATA_NVS    0x02
#define ESP_PARTITION_SUBTYPE_DATA_COREDUMP 0x03
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 0x82
#define ESP_PARTITION_SUBTYPE_DATA_FAT    0x81

typedef void* esp_partition_iterator_t;

// -- Functions --

// Find first partition by type, subtype, label (label can be NULL).
// Returns pointer to partition, or NULL if not found.
const esp_partition_t* esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char* label);

// Read raw data from a partition.
esp_err_t esp_partition_read(
    const esp_partition_t* partition,
    size_t src_offset,
    void* dst,
    size_t size);

// Find partition by type, subtype, label (label can be NULL).
// Returns iterator handle, or NULL if not found.
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

// Set whether esp_partition_find_first returns a SPIFFS partition
// and whether the partition content appears erased (all 0xFF).
void mock_spiffs_partition(bool present, bool erased);

} // namespace test
} // namespace sigurdos
#endif
