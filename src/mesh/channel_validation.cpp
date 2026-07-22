// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "channel_validation.h"
#include <cctype>
#include <cstring>

namespace sigurdos {
namespace mesh {

static constexpr size_t MAX_CHAN_NAME = 31;

static bool is_valid_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '-';
}

static bool validate_body(const char* start, size_t len, const char** reason) {
    if (len == 0) {
        if (reason) *reason = "Name is just '#'";
        return false;
    }
    if (start[0] == '-') {
        if (reason) *reason = "Leading dash not allowed";
        return false;
    }
    if (start[len - 1] == '-') {
        if (reason) *reason = "Trailing dash not allowed";
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = start[i];
        if (!is_valid_char(c)) {
            if (reason) *reason = "Invalid character in name";
            return false;
        }
        if (c == '-' && i > 0 && start[i - 1] == '-') {
            if (reason) *reason = "Consecutive dashes not allowed";
            return false;
        }
    }
    return true;
}

bool channel_name_valid(const char* name, const char** reason) {
    if (!name || !name[0]) {
        if (reason) *reason = "Name is empty";
        return false;
    }

    // Trim whitespace by finding first/last non-space
    const char* start = name;
    while (*start && isspace((unsigned char)*start)) start++;
    const char* end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;

    size_t len = (size_t)(end - start);
    if (len == 0) {
        if (reason) *reason = "Name is empty after trimming";
        return false;
    }

    if (len > MAX_CHAN_NAME) {
        if (reason) {
            *reason = *start == '#'
                ? "Name too long (max 31 chars including '#')"
                : "Name too long (max 31 chars)";
        }
        return false;
    }

    // Optional leading '#' — it has already counted toward the stored limit.
    if (*start == '#') {
        start++;
        len--;
    }

    return validate_body(start, len, reason);
}

bool hashtag_channel_name_normalise(const char* name, char* out,
                                    size_t out_size, const char** reason) {
    if (!name || !name[0] || !out || out_size == 0) {
        if (reason) *reason = "Name is empty";
        return false;
    }

    const char* start = name;
    while (*start && isspace((unsigned char)*start)) start++;
    const char* end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    if (start < end && *start == '#') start++;

    const size_t body_len = (size_t)(end - start);
    const size_t stored_len = body_len + 1;
    if (stored_len > MAX_CHAN_NAME || stored_len + 1 > out_size) {
        if (reason) *reason = "Name too long (max '#' plus 30 chars)";
        return false;
    }
    if (!validate_body(start, body_len, reason)) return false;

    out[0] = '#';
    memcpy(out + 1, start, body_len);
    out[stored_len] = '\0';
    return true;
}

size_t channel_name_sanitise(char* name, size_t max_len) {
    if (!name || max_len == 0) return 0;

    // Trim
    char* start = name;
    while (*start && isspace((unsigned char)*start)) start++;
    char* end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';

    size_t len = (size_t)(end - start);
    if (len == 0) return 0;

    // Strip leading '#'
    if (*start == '#') {
        start++;
        len--;
    }

    // Move to front if trimming changed position
    if (start != name) {
        memmove(name, start, len + 1);
    }

    const size_t output_limit = max_len - 1;
    const size_t effective_limit = output_limit < MAX_CHAN_NAME ? output_limit : MAX_CHAN_NAME;
    if (len > effective_limit) {
        len = effective_limit;
    }

    // Lowercase
    for (size_t i = 0; i < len; i++) {
        name[i] = (char)tolower((unsigned char)name[i]);
    }

    name[len] = '\0';

    return len;
}

bool channel_psk_decode_16(const char* encoded, uint8_t out[16]) {
    if (!encoded || !out || std::strlen(encoded) != 24 ||
        encoded[22] != '=' || encoded[23] != '=') return false;
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t values[22];
    for (size_t i = 0; i < sizeof(values); ++i) {
        const char* found = std::strchr(alphabet, encoded[i]);
        if (!found) return false;
        values[i] = (uint8_t)(found - alphabet);
    }
    // Sixteen bytes leave two significant bits in the final Base64 symbol.
    // Canonical encoding requires its unused low four bits to be zero.
    if ((values[21] & 0x0F) != 0) return false;

    size_t written = 0;
    uint32_t bits = 0;
    int bit_count = 0;
    for (size_t i = 0; i < sizeof(values); ++i) {
        bits = (bits << 6) | values[i];
        bit_count += 6;
        while (bit_count >= 8 && written < 16) {
            bit_count -= 8;
            out[written++] = (uint8_t)(bits >> bit_count);
            bits &= bit_count == 0 ? 0 : ((1u << bit_count) - 1u);
        }
    }
    return written == 16 && bit_count == 4 && bits == 0;
}

}  // namespace mesh
}  // namespace sigurdos
