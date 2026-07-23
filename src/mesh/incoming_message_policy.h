#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "utils/utf8_util.h"

namespace sigurdos {
namespace mesh {

// Anonymous chat requests contain the BaseChatMesh request tag followed by
// SigurdOS' sender timestamp and UTF-8 text.
inline bool decodeAnonymousChat(const uint8_t* data, size_t length,
                                uint32_t& timestamp, char* text,
                                size_t text_size)
{
    constexpr size_t kHeaderSize = 8;
    if (!data || !text || text_size == 0 || length <= kHeaderSize) return false;

    timestamp = static_cast<uint32_t>(data[4]) |
                (static_cast<uint32_t>(data[5]) << 8) |
                (static_cast<uint32_t>(data[6]) << 16) |
                (static_cast<uint32_t>(data[7]) << 24);
    return sigurdos::utf8_copy_truncate_n(
               text, text_size,
               reinterpret_cast<const char*>(data + kHeaderSize),
               length - kHeaderSize) > 0;
}

inline void formatAnonymousSender(const uint8_t* public_key, char* output,
                                  size_t output_size)
{
    if (!output || output_size == 0) return;
    if (!public_key || output_size < 18) {
        sigurdos::utf8_copy_truncate(output, output_size, "anonymous");
        return;
    }
    std::snprintf(output, output_size, "anon_%02x%02x%02x%02x%02x%02x",
                  public_key[0], public_key[1], public_key[2],
                  public_key[3], public_key[4], public_key[5]);
}

// Group text normally uses "<sender>: <message>". Treat an invalid or
// missing sender field as untrusted metadata while retaining the content.
inline bool parseGroupText(const char* raw, char* sender, size_t sender_size,
                           char* message, size_t message_size)
{
    if (!sender || sender_size == 0 || !message || message_size == 0) {
        return false;
    }
    if (!raw) {
        sigurdos::utf8_copy_truncate(sender, sender_size, "unknown");
        message[0] = '\0';
        return false;
    }

    const char* delimiter = std::strstr(raw, ": ");
    if (delimiter && delimiter > raw) {
        const size_t sender_length = static_cast<size_t>(delimiter - raw);
        const bool valid_sender = sender_length < sender_size &&
            sigurdos::utf8_truncate_bytes(raw, sender_length) == sender_length;
        if (valid_sender) {
            std::memcpy(sender, raw, sender_length);
            sender[sender_length] = '\0';
            sigurdos::utf8_copy_truncate(message, message_size, delimiter + 2);
            return true;
        }
    }

    sigurdos::utf8_copy_truncate(sender, sender_size, "unknown");
    sigurdos::utf8_copy_truncate(message, message_size, raw);
    return false;
}

} // namespace mesh
} // namespace sigurdos
