#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos::ui {

static constexpr uint32_t RESPONSE_WAIT_TIMEOUT_MS = 10000;

enum class ResponseWaitPhase {
    Loading,
    Ready,
    SendFailed,
    TimedOut,
};

class ResponseWaitState {
public:
    void begin(uint32_t started_at_ms, bool request_sent)
    {
        started_ = true;
        request_sent_ = request_sent;
        started_at_ms_ = started_at_ms;
    }

    void ensureStarted(uint32_t now_ms)
    {
        if (!started_) begin(now_ms, true);
    }

    ResponseWaitPhase phase(uint32_t now_ms, bool response_ready,
                            uint32_t timeout_ms = RESPONSE_WAIT_TIMEOUT_MS) const
    {
        if (response_ready) return ResponseWaitPhase::Ready;
        if (started_ && !request_sent_) return ResponseWaitPhase::SendFailed;
        if (started_ && static_cast<uint32_t>(now_ms - started_at_ms_) >= timeout_ms) {
            return ResponseWaitPhase::TimedOut;
        }
        return ResponseWaitPhase::Loading;
    }

    void reset()
    {
        started_ = false;
        request_sent_ = false;
        started_at_ms_ = 0;
    }

private:
    bool started_ = false;
    bool request_sent_ = false;
    uint32_t started_at_ms_ = 0;
};

} // namespace sigurdos::ui
