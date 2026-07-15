#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "utils/utf8_util.h"

namespace sigurdos::ui {

enum class RepeaterTranscriptType : uint8_t {
    Command,
    CliReply,
};

struct RepeaterTranscriptEntry {
    char contact[32] = {};
    char text[160] = {};
    uint32_t timestamp = 0;
    RepeaterTranscriptType type = RepeaterTranscriptType::Command;
};

template <size_t Capacity>
class RepeaterTranscript {
public:
    static_assert(Capacity > 0, "repeater transcript must have capacity");

    using Entry = RepeaterTranscriptEntry;

    bool append(const char* contact, RepeaterTranscriptType type,
                uint32_t timestamp, const char* text)
    {
        if (!contact || !contact[0] || !text || !text[0]) return false;

        size_t target = (head_ + count_) % Capacity;
        if (count_ == Capacity) {
            target = head_;
            head_ = (head_ + 1) % Capacity;
        } else {
            count_++;
        }

        Entry& entry = entries_[target];
        sigurdos::utf8_copy_truncate(entry.contact, sizeof(entry.contact), contact);
        sigurdos::utf8_copy_truncate(entry.text, sizeof(entry.text), text);
        entry.timestamp = timestamp;
        entry.type = type;
        return true;
    }

    const Entry* at(size_t index) const
    {
        if (index >= count_) return nullptr;
        return &entries_[(head_ + index) % Capacity];
    }

    size_t size() const { return count_; }

private:
    Entry entries_[Capacity] = {};
    size_t head_ = 0;
    size_t count_ = 0;
};

inline bool format_repeater_cli_line(char* out, size_t out_size, uint32_t timestamp,
                                     const char* marker, const char* text)
{
    if (!out || out_size == 0) return false;
    const uint32_t seconds = timestamp % 86400u;
    const int written = timestamp == 0
        ? std::snprintf(out, out_size, "[--:--:--] %s%s\n",
                        marker ? marker : "", text ? text : "")
        : std::snprintf(out, out_size, "[%02u:%02u:%02u] %s%s\n",
                        seconds / 3600u, (seconds / 60u) % 60u, seconds % 60u,
                        marker ? marker : "", text ? text : "");
    return written >= 0 && static_cast<size_t>(written) < out_size;
}

inline bool format_repeater_cli_command(char* out, size_t out_size, uint32_t timestamp,
                                        const char* text)
{
    return format_repeater_cli_line(out, out_size, timestamp, "> [CMD] ", text);
}

inline bool format_repeater_cli_reply(char* out, size_t out_size, uint32_t timestamp,
                                      const char* text)
{
    return format_repeater_cli_line(out, out_size, timestamp, "< [CLI] ", text);
}

inline bool format_repeater_transcript_entry(
    char* out, size_t out_size,
    const RepeaterTranscriptEntry& entry)
{
    return entry.type == RepeaterTranscriptType::Command
        ? format_repeater_cli_command(out, out_size, entry.timestamp, entry.text)
        : format_repeater_cli_reply(out, out_size, entry.timestamp, entry.text);
}

} // namespace sigurdos::ui
