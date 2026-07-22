// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <MeshCore.h>
#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

constexpr std::size_t ANON_OUTER_TAG_BYTES = sizeof(uint32_t);
constexpr std::size_t ANON_TIMESTAMP_BYTES = sizeof(uint32_t);
constexpr std::size_t ANON_TEXT_TERMINATOR_BYTES = 1;
constexpr std::size_t MAX_ANON_REQUEST_BYTES =
    MAX_PACKET_PAYLOAD - ANON_OUTER_TAG_BYTES;
constexpr std::size_t MAX_ANON_TEXT_BYTES =
    MAX_ANON_REQUEST_BYTES - ANON_TIMESTAMP_BYTES -
    ANON_TEXT_TERMINATOR_BYTES;

static_assert(MAX_ANON_REQUEST_BYTES <= UINT8_MAX,
              "anonymous request length must fit MeshCore's uint8_t API");
static_assert(MAX_ANON_TEXT_BYTES == 175,
              "anonymous text budget changed; review protocol framing");

inline bool anonymousRequestFits(std::size_t data_len)
{
    return data_len <= MAX_ANON_REQUEST_BYTES;
}

inline bool anonymousTextFits(const char* text, std::size_t* text_len = nullptr)
{
    if (text_len) *text_len = 0;
    if (!text) return false;

    std::size_t len = 0;
    while (len <= MAX_ANON_TEXT_BYTES && text[len] != '\0') ++len;
    if (len == 0 || len > MAX_ANON_TEXT_BYTES) return false;

    if (text_len) *text_len = len;
    return true;
}

} // namespace mesh
} // namespace sigurdos
