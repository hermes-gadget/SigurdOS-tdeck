#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <MeshCore.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace sigurdos::mesh {

static constexpr size_t SIGURDOS_ADVERT_BLOB_MIN_LEN =
    PUB_KEY_SIZE + 4 + SIGNATURE_SIZE;
static constexpr size_t SIGURDOS_ADVERT_BLOB_MAX_LEN =
    2 + 32 + PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE;
static constexpr size_t SIGURDOS_ADVERT_BLOB_KEY_PREFIX_LEN = 8;
static constexpr size_t SIGURDOS_ADVERT_BLOB_LEGACY_KEY_PREFIX_LEN = 4;

inline bool advertBlobLengthValid(size_t length)
{
    return length >= SIGURDOS_ADVERT_BLOB_MIN_LEN &&
           length <= SIGURDOS_ADVERT_BLOB_MAX_LEN;
}

inline bool advertBlobFitsOutput(size_t length, size_t capacity)
{
    return advertBlobLengthValid(length) && length <= capacity;
}

inline bool makeAdvertBlobPath(const uint8_t* key, int key_len,
                               char* out, size_t out_size,
                               bool legacy = false)
{
    if (!key || key_len <= 0 || !out || out_size == 0) return false;
    size_t prefix_len = legacy ? SIGURDOS_ADVERT_BLOB_LEGACY_KEY_PREFIX_LEN
                               : SIGURDOS_ADVERT_BLOB_KEY_PREFIX_LEN;
    if (prefix_len > (size_t)key_len) prefix_len = (size_t)key_len;
    const size_t required = 6 + prefix_len * 2 + 1;  // "/blob_" + hex + NUL
    if (out_size < required) return false;

    size_t pos = 0;
    const int prefix_written = snprintf(out, out_size, "/blob_");
    if (prefix_written != 6) return false;
    pos = 6;
    for (size_t i = 0; i < prefix_len; ++i) {
        const int written = snprintf(out + pos, out_size - pos, "%02x", key[i]);
        if (written != 2) return false;
        pos += 2;
    }
    return true;
}

static_assert(SIGURDOS_ADVERT_BLOB_MAX_LEN <= MAX_TRANS_UNIT,
              "advert blobs must fit MeshCore's internal transport buffer");

}  // namespace sigurdos::mesh
