// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>

namespace sigurdos::ui {

enum class NotificationEvent : uint8_t {
    DirectMessage,
    Mention,
    CompanionConnected,
    CompanionDisconnected,
    LoginSuccess,
    LoginFailure,
    LowBattery,
    StorageFull,
    OtaFailure,
};

enum class NotificationLevel : uint8_t { Info, Important, Critical };

struct NotificationPolicy {
    NotificationLevel level;
    uint32_t duration_ms;
    bool sticky;
};

inline NotificationPolicy notification_policy(NotificationEvent event)
{
    switch (event) {
        case NotificationEvent::Mention:
        case NotificationEvent::LoginFailure:
            return {NotificationLevel::Important, 6000, false};
        case NotificationEvent::LowBattery:
        case NotificationEvent::StorageFull:
        case NotificationEvent::OtaFailure:
            return {NotificationLevel::Critical, 0, true};
        default:
            return {NotificationLevel::Info, 4000, false};
    }
}

inline bool notification_low_battery(int percent)
{
    return percent >= 0 && percent <= 15;
}

inline bool notification_storage_low(uint64_t free_bytes, uint64_t total_bytes)
{
    if (total_bytes == 0) return false;
    constexpr uint64_t ONE_MIB = 1024ULL * 1024ULL;
    return free_bytes <= ONE_MIB || free_bytes <= total_bytes / 50ULL;
}

inline bool notification_contains_mention(const char* text, const char* own_name)
{
    if (!text || !own_name || own_name[0] == '\0') return false;
    const size_t name_len = std::strlen(own_name);
    for (const char* p = text; *p; ++p) {
        const bool left_boundary = p == text ||
            !std::isalnum(static_cast<unsigned char>(p[-1]));
        if (!left_boundary) continue;
        size_t i = 0;
        while (i < name_len && p[i] &&
               std::tolower(static_cast<unsigned char>(p[i])) ==
               std::tolower(static_cast<unsigned char>(own_name[i]))) {
            ++i;
        }
        if (i != name_len) continue;
        if (!std::isalnum(static_cast<unsigned char>(p[i]))) return true;
    }
    return false;
}

struct NotificationItem {
    NotificationEvent event = NotificationEvent::DirectMessage;
    NotificationLevel level = NotificationLevel::Info;
    uint32_t duration_ms = 0;
    bool sticky = false;
    char text[96] = {};
};

class NotificationQueue {
public:
    static constexpr uint8_t CAPACITY = 4;

    void reset()
    {
        _active = false;
        _count = 0;
        _started_ms = 0;
        _current = {};
    }

    void post(NotificationEvent event, const char* text, uint32_t now_ms)
    {
        NotificationItem item;
        item.event = event;
        const NotificationPolicy policy = notification_policy(event);
        item.level = policy.level;
        item.duration_ms = policy.duration_ms;
        item.sticky = policy.sticky;
        if (text) {
            std::strncpy(item.text, text, sizeof(item.text) - 1);
            item.text[sizeof(item.text) - 1] = '\0';
        }

        if (!_active) {
            activate(item, now_ms);
            return;
        }
        if (static_cast<uint8_t>(item.level) > static_cast<uint8_t>(_current.level)) {
            enqueue_front(_current);
            activate(item, now_ms);
            return;
        }
        enqueue(item);
    }

    bool tick(uint32_t now_ms)
    {
        if (!_active || _current.sticky) return false;
        if (static_cast<uint32_t>(now_ms - _started_ms) < _current.duration_ms) {
            return false;
        }
        advance(now_ms);
        return true;
    }

    void dismiss(uint32_t now_ms) { advance(now_ms); }

    bool has_current() const { return _active; }
    const NotificationItem& current() const { return _current; }
    uint8_t pending_count() const { return _count; }

private:
    void activate(const NotificationItem& item, uint32_t now_ms)
    {
        _current = item;
        _started_ms = now_ms;
        _active = true;
    }

    void enqueue(const NotificationItem& item)
    {
        if (_count == CAPACITY - 1) {
            for (uint8_t i = 1; i < _count; ++i) _pending[i - 1] = _pending[i];
            --_count;
        }
        _pending[_count++] = item;
    }

    void enqueue_front(const NotificationItem& item)
    {
        if (_count == CAPACITY - 1) --_count;
        for (uint8_t i = _count; i > 0; --i) _pending[i] = _pending[i - 1];
        _pending[0] = item;
        ++_count;
    }

    void advance(uint32_t now_ms)
    {
        if (_count == 0) {
            _active = false;
            _current = {};
            return;
        }
        NotificationItem next = _pending[0];
        for (uint8_t i = 1; i < _count; ++i) _pending[i - 1] = _pending[i];
        --_count;
        activate(next, now_ms);
    }

    NotificationItem _current{};
    NotificationItem _pending[CAPACITY - 1]{};
    uint32_t _started_ms = 0;
    uint8_t _count = 0;
    bool _active = false;
};

} // namespace sigurdos::ui
