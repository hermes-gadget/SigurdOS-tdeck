#pragma once

namespace sigurdos::diagnostics {

// Public esp_reset_reason_t numeric values from ESP-IDF.
inline bool reset_retains_crash_evidence(int reason) {
    return reason == 4 || reason == 5 || reason == 6 || reason == 7 ||
           reason == 9;
}

}  // namespace sigurdos::diagnostics
