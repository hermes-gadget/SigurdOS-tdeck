#pragma once

namespace sigurdos::diagnostics {

inline bool debug_text_capture_allowed(bool capture_enabled,
                                       bool sensitive_subtree) {
    return capture_enabled && !sensitive_subtree;
}

}  // namespace sigurdos::diagnostics
