// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Characterization tests for the pure helpers extracted while giving the
// companion adapter a real translation-unit boundary (ARCH-001, #820):
// the flood-scope key hex codec (scope_key_hex.h) and the shared
// "DM: <name>" conversation-key formatter (mesh_wrapper_internal.h).
// These pin the legacy semantics exactly.

#include <gtest/gtest.h>
#include "mesh/scope_key_hex.h"
#include "mesh/mesh_wrapper_internal.h"

#include <cstring>

namespace {

using sigurdos::mesh::scopeKeyHexEncode;
using sigurdos::mesh::scopeKeyHexDecode;
using sigurdos::mesh::formatDmConversation;

TEST(ScopeKeyHex, EncodeEmitsLowercaseAndNulTerminates)
{
    const uint8_t key[16] = {0x00, 0x01, 0x0A, 0x0F, 0x10, 0x7F, 0x80, 0xAB,
                             0xCD, 0xEF, 0xFF, 0x42, 0x99, 0x5A, 0xA5, 0x3C};
    char out[33];
    memset(out, 'X', sizeof(out));
    scopeKeyHexEncode(key, out);
    EXPECT_STREQ(out, "00010a0f107f80abcdefff42995aa53c");
    EXPECT_EQ(out[32], '\0');
}

TEST(ScopeKeyHex, RoundTripPreservesAllBytes)
{
    uint8_t key[16];
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)(i * 17 + 3);
    char hex[33];
    scopeKeyHexEncode(key, hex);
    uint8_t back[16] = {0};
    scopeKeyHexDecode(hex, back);
    EXPECT_EQ(memcmp(key, back, 16), 0);
}

TEST(ScopeKeyHex, DecodeIsCaseInsensitive)
{
    uint8_t lower[16], upper[16];
    scopeKeyHexDecode("00010a0f107f80abcdefff42995aa53c", lower);
    scopeKeyHexDecode("00010A0F107F80ABCDEFFF42995AA53C", upper);
    EXPECT_EQ(memcmp(lower, upper, 16), 0);
    EXPECT_EQ(lower[7], 0xAB);
}

TEST(ScopeKeyHex, NonHexCharactersDecodeToZeroNibbles)
{
    // Legacy behavior: an invalid character contributes a zero nibble
    // instead of failing the decode.
    uint8_t out[16];
    memset(out, 0xEE, sizeof(out));
    scopeKeyHexDecode("zzg57Zz9!!~~--++..##qqrrssttuuvv", out);
    EXPECT_EQ(out[0], 0x00);  // "zz"
    EXPECT_EQ(out[1], 0x05);  // "g5" — only the low nibble is valid
    EXPECT_EQ(out[2], 0x70);  // "7Z" — only the high nibble is valid
    EXPECT_EQ(out[3], 0x09);  // "z9"
    for (int i = 4; i < 16; i++) EXPECT_EQ(out[i], 0x00) << "byte " << i;
}

TEST(ScopeKeyHex, NullArgumentsAreNoOps)
{
    uint8_t key[16] = {1};
    char hex[33] = "unchanged";
    scopeKeyHexEncode(nullptr, hex);
    EXPECT_STREQ(hex, "unchanged");
    scopeKeyHexEncode(key, nullptr);   // must not crash
    uint8_t out[16] = {7};
    scopeKeyHexDecode(nullptr, out);
    EXPECT_EQ(out[0], 7);
    scopeKeyHexDecode("00", nullptr);  // must not crash
}

TEST(FormatDmConversation, PrefixesNameWithDmMarker)
{
    char out[40];
    formatDmConversation(out, sizeof(out), "alice");
    EXPECT_STREQ(out, "DM: alice");
}

TEST(FormatDmConversation, NullNameYieldsBarePrefix)
{
    char out[40];
    formatDmConversation(out, sizeof(out), nullptr);
    EXPECT_STREQ(out, "DM: ");
}

TEST(FormatDmConversation, TruncatesToBufferAndNulTerminates)
{
    char out[7];
    formatDmConversation(out, sizeof(out), "alice");
    EXPECT_STREQ(out, "DM: al");   // 6 chars + NUL

    char tiny[3];
    formatDmConversation(tiny, sizeof(tiny), "alice");
    EXPECT_STREQ(tiny, "DM");      // prefix itself truncates

    char one[1];
    one[0] = 'X';
    formatDmConversation(one, sizeof(one), "alice");
    EXPECT_STREQ(one, "");
}

TEST(FormatDmConversation, ZeroSizeAndNullOutAreNoOps)
{
    char out[4] = "abc";
    formatDmConversation(out, 0, "alice");
    EXPECT_STREQ(out, "abc");
    formatDmConversation(nullptr, 16, "alice");  // must not crash
}

} // namespace
