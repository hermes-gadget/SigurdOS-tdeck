// SPDX-License-Identifier: GPL-3.0-or-later

// Compile/link probe for target-only branches that native host tests cannot
// exercise. This image is not shipped; CI builds it for the ESP32-S3 target.

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>

#include <type_traits>

#include "comms/ble_frame_queue.h"
#include "comms/ble_task_mutex.h"
#include "hal/tdeck_board.h"

#if !defined(ESP32_PLATFORM)
#error "ESP32 component probe must compile the target branches"
#endif

namespace {

RTC_DATA_ATTR uint32_t retained_probe = 0;
sigurdos::comms::BleFrameQueue<32, 4> frame_queue;
sigurdos::comms::BleTaskMutex task_mutex;

static_assert(std::is_base_of<ESP32Board, sigurdos::TDeckBoard>::value,
              "TDeckBoard target inheritance must compile");
static_assert(sizeof(frame_queue) > 0, "FreeRTOS-backed queue must be concrete");

}  // namespace

void setup() {
  const uint8_t input[] = {1, 2, 3};
  uint8_t output[32] = {};
  frame_queue.tryPush(input, sizeof(input));
  frame_queue.pop(output);
  {
    sigurdos::comms::BleTaskMutex::Guard outer(task_mutex);
    sigurdos::comms::BleTaskMutex::Guard recursive(task_mutex);
    retained_probe += output[0];
  }

  void* target_memory = heap_caps_malloc(32, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  heap_caps_free(target_memory);
  const esp_partition_t* running = esp_ota_get_running_partition();
  (void)running;
  (void)esp_sleep_get_wakeup_cause();
}

void loop() { delay(1000); }
