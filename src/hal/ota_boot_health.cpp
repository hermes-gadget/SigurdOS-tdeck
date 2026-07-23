// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "ota_boot_health.h"

#include "../diagnostics/log.h"
#include <Arduino.h>
#include <esp_ota_ops.h>

// Arduino otherwise accepts a pending image in initArduino(), before setup()
// can validate SigurdOS's display, storage, settings, UI, and main loop.
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
extern "C" bool verifyRollbackLater() {
    return true;
}
#endif

namespace sigurdos {
namespace ota_boot_health {
namespace {

PolicyState s_policy{};
bool s_validation_attempted = false;

}  // namespace

void begin() {
    s_policy = {};
    s_validation_attempted = false;

#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (running &&
        esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_policy.pending_verification = true;
        s_policy.started_at = millis();
        SIG_LOGW("[ota-health] Pending image; validating after %lu ms of healthy boot",
                 static_cast<unsigned long>(HEALTH_WINDOW_MS));
    }
#endif
}

void markCoreReady() {
    s_policy.core_ready = true;
}

void loop() {
    s_policy.loop_completed = true;
    if (s_validation_attempted || !shouldMarkValid(s_policy, millis())) return;

    s_validation_attempted = true;
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        s_policy.pending_verification = false;
        SIG_LOGW("[ota-health] Boot healthy; OTA image marked valid");
    } else {
        SIG_LOGE("[ota-health] Could not mark OTA image valid: %s",
                 esp_err_to_name(err));
    }
#endif
}

void rollbackIfPending(const char* reason) {
    if (!s_policy.pending_verification) return;

#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    SIG_LOGE("[ota-health] Critical boot failure (%s); rolling back",
             reason ? reason : "unknown");
    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    SIG_LOGE("[ota-health] Rollback request failed: %s", esp_err_to_name(err));
    (void)err;
#else
    (void)reason;
#endif
}

bool isPendingVerification() {
    return s_policy.pending_verification;
}

}  // namespace ota_boot_health
}  // namespace sigurdos
