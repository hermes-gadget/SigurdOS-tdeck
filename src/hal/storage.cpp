// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "storage.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_partition.h>
#include <atomic>
#include <memory>
#include <new>

#if defined(ESP32_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace sigurdos {

static std::atomic<bool> s_storage_available{false};
static std::atomic<bool> s_storage_init_called{false};
static std::atomic<bool> s_storage_mounted{false};
static std::atomic<bool> s_storage_warm_cancel{false};
static std::atomic<StorageWarmState> s_storage_warm_state{StorageWarmState::Idle};

#if defined(ESP32_PLATFORM)
static std::atomic<TaskHandle_t> s_storage_warm_task{nullptr};
static std::atomic<bool> s_storage_warm_task_starting{false};
static constexpr uint32_t STORAGE_WARM_JOIN_TIMEOUT_MS = 2000;
static constexpr uint32_t STORAGE_WARM_DELAY_MS = 100;
static constexpr uint32_t STORAGE_WARM_DELAY_TOTAL_MS = 12000;
#endif

// First-write SPIFFS GC absorption: the first write after mount can trigger
// a GC pass that blocks the writer for 10-90s on this encrypted partition
// (measured 9.5-90s for the first channel-store commit after boot). Running
// that write in a background task after the boot sequence keeps loopTask
// (and its 10s runtime watchdog) free of the stall, so runtime commits —
// channel store, contacts — stay fast.
#if defined(ESP32_PLATFORM)
static bool storage_warm_cancelled()
{
    return s_storage_warm_cancel.load(std::memory_order_acquire) ||
           !s_storage_mounted.load(std::memory_order_acquire) ||
           s_storage_warm_state.load(std::memory_order_acquire) ==
               StorageWarmState::Stopping;
}

static void storage_warm_finish(bool ready)
{
    s_storage_warm_state.store(ready ? StorageWarmState::Ready
                                     : StorageWarmState::Idle,
                               std::memory_order_release);
    s_storage_warm_task.store(nullptr, std::memory_order_release);
    vTaskDelete(nullptr);
}

static void storage_warm_task(void*) {
    // xTaskCreate may schedule this task before its caller has received and
    // published the handle. Wait for that publication so cancellation cannot
    // make the creator store a stale handle after this task has exited.
    while (s_storage_warm_task_starting.load(std::memory_order_acquire)) {
        vTaskDelay(1);
    }

    // Boot's own SPIFFS readers (settings/chats/mesh) finish around t+5-8s;
    // wait for them so the warm write cannot stall boot-time reads.
    for (uint32_t waited = 0; waited < STORAGE_WARM_DELAY_TOTAL_MS;
         waited += STORAGE_WARM_DELAY_MS) {
        if (storage_warm_cancelled()) {
            storage_warm_finish(false);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(STORAGE_WARM_DELAY_MS));
    }

    if (storage_warm_cancelled()) {
        storage_warm_finish(false);
        return;
    }

    bool warmed = true;
    for (int i = 0; i < 3; ++i) {
        if (storage_warm_cancelled()) {
            storage_warm_finish(false);
            return;
        }
        File f = SPIFFS.open("/.warm", FILE_WRITE);
        if (!f) {
            Serial.println("[storage] warm open failed");
            warmed = false;
            break;
        }
        const uint8_t byte = static_cast<uint8_t>('w' + i);
        const bool wrote = f.write(&byte, 1) == 1;
        f.close();
        if (!wrote) {
            Serial.println("[storage] warm write failed");
            warmed = false;
            break;
        }
    }
    if (warmed && !storage_warm_cancelled() && !SPIFFS.remove("/.warm")) {
        Serial.println("[storage] warm cleanup failed");
        warmed = false;
    }
    storage_warm_finish(warmed && !storage_warm_cancelled());
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
    if (s_storage_init_called.load(std::memory_order_acquire)) {
        return s_storage_available.load(std::memory_order_acquire);
    }
    s_storage_init_called.store(true, std::memory_order_release);

    // Attempt a safe mount first — don't format, respect existing data.
    if (SPIFFS.begin(false)) {
        s_storage_available.store(true, std::memory_order_release);
        s_storage_mounted.store(true, std::memory_order_release);
        s_storage_warm_cancel.store(false, std::memory_order_release);
        s_storage_warm_state.store(StorageWarmState::Idle,
                                   std::memory_order_release);
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
        s_storage_available.store(false, std::memory_order_release);
        return false;
    }

    const PartitionEraseState erase_state = classify_partition_erasure(part);

    if (erase_state == PartitionEraseState::Erased) {
        Serial.println("[storage] SPIFFS partition is fully erased — formatting once");
        if (!SPIFFS.format()) {
            Serial.println("[storage] SPIFFS format failed — storage unavailable");
            s_storage_available.store(false, std::memory_order_release);
            return false;
        }
        if (!SPIFFS.begin(false)) {
            Serial.println("[storage] SPIFFS mount after format failed — storage unavailable");
            s_storage_available.store(false, std::memory_order_release);
            return false;
        }
        Serial.println("[storage] SPIFFS formatted and mounted");
        s_storage_available.store(true, std::memory_order_release);
        s_storage_mounted.store(true, std::memory_order_release);
        s_storage_warm_cancel.store(false, std::memory_order_release);
        s_storage_warm_state.store(StorageWarmState::Idle,
                                   std::memory_order_release);
        return true;
    }

    if (erase_state == PartitionEraseState::Unknown) {
        Serial.println("[storage] SPIFFS erased-state check failed — preserving partition");
        s_storage_available.store(false, std::memory_order_release);
        return false;
    }

    // Partition has data but SPIFFS can't mount it — likely corruption.
    Serial.println("[storage] SPIFFS mount failed (partition contains non-erased data but is not a valid SPIFFS filesystem)");
    Serial.println("[storage] Storage unavailable — identity/contacts won't persist. Use factory reset to reformat.");
    s_storage_available.store(false, std::memory_order_release);
    return false;
}

bool storage_available()
{
    return s_storage_available.load(std::memory_order_acquire);
}

bool storage_ensure_mounted()
{
    if (!s_storage_available.load(std::memory_order_acquire)) return false;
    if (!s_storage_mounted.load(std::memory_order_acquire)) {
        if (!SPIFFS.begin(false)) return false;
        s_storage_mounted.store(true, std::memory_order_release);
    }
    return true;
}

void storage_warm_after_mount()
{
    if (!s_storage_mounted.load(std::memory_order_acquire)) return;
    StorageWarmState expected = StorageWarmState::Idle;
    if (!s_storage_warm_state.compare_exchange_strong(
            expected, StorageWarmState::Warming,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    s_storage_warm_cancel.store(false, std::memory_order_release);
#if defined(ESP32_PLATFORM)
    // 4096-byte stack; the warm does only a tiny bounded file op.
    s_storage_warm_task_starting.store(true, std::memory_order_release);
    TaskHandle_t task = nullptr;
    if (xTaskCreate(storage_warm_task, "storage-warm", 4096, nullptr, 1,
                    &task) != pdPASS || !task) {
        Serial.println("[storage] could not create warm task");
        s_storage_warm_task_starting.store(false, std::memory_order_release);
        s_storage_warm_state.store(StorageWarmState::Idle,
                                   std::memory_order_release);
        return;
    }
    s_storage_warm_task.store(task, std::memory_order_release);
    s_storage_warm_task_starting.store(false, std::memory_order_release);
#else
    // The host has no FreeRTOS task or SPIFFS implementation. Treat the
    // bounded warm operation as completed so lifecycle tests can exercise the
    // same stop/state contract without sleeping for twelve seconds.
    s_storage_warm_state.store(StorageWarmState::Ready,
                               std::memory_order_release);
#endif
}

bool storage_stop_warm()
{
    s_storage_warm_cancel.store(true, std::memory_order_release);

#if defined(ESP32_PLATFORM)
    s_storage_warm_state.store(StorageWarmState::Stopping,
                               std::memory_order_release);
    const uint32_t started = millis();
    while (s_storage_warm_task_starting.load(std::memory_order_acquire) &&
           static_cast<uint32_t>(millis() - started) <
               STORAGE_WARM_JOIN_TIMEOUT_MS) {
        delay(STORAGE_WARM_DELAY_MS);
    }
    if (s_storage_warm_task_starting.load(std::memory_order_acquire)) {
        Serial.println("[storage] warm task creation did not settle before teardown");
        return false;
    }

    TaskHandle_t task = s_storage_warm_task.load(std::memory_order_acquire);
    while (task != nullptr &&
           static_cast<uint32_t>(millis() - started) <
               STORAGE_WARM_JOIN_TIMEOUT_MS) {
        delay(1);
        task = s_storage_warm_task.load(std::memory_order_acquire);
    }
    if (task != nullptr) {
        Serial.println("[storage] warm task did not stop before teardown");
        return false;
    }
#endif

    s_storage_warm_state.store(StorageWarmState::Idle,
                               std::memory_order_release);
    s_storage_mounted.store(false, std::memory_order_release);
    s_storage_available.store(false, std::memory_order_release);
    return true;
}

StorageWarmState storage_warm_state()
{
    return s_storage_warm_state.load(std::memory_order_acquire);
}

void storage_reset()
{
    if (!storage_stop_warm()) return;
    s_storage_init_called.store(false, std::memory_order_release);
    s_storage_available.store(false, std::memory_order_release);
    s_storage_mounted.store(false, std::memory_order_release);
    s_storage_warm_cancel.store(false, std::memory_order_release);
    s_storage_warm_state.store(StorageWarmState::Idle,
                               std::memory_order_release);
}

} // namespace sigurdos
