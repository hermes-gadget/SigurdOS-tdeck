// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Characterization tests for the pure helpers extracted while giving the
// companion adapter a real translation-unit boundary (ARCH-001, #820):
// the flood-scope key hex codec (scope_key_hex.h) and the shared
// "DM: <name>" conversation-key formatter (mesh_wrapper_internal.h).
// These pin the validated codec semantics.

#include <gtest/gtest.h>
#include "mesh/scope_key_hex.h"
#include "mesh/anonymous_message_policy.h"
#include "mesh/mesh_init_lifecycle.h"
#include "mesh/mesh_wrapper_internal.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using sigurdos::mesh::scopeKeyHexEncode;
using sigurdos::mesh::scopeKeyHexDecode;
using sigurdos::mesh::scopeKeyBase64Decode;
using sigurdos::mesh::formatDmConversation;
using sigurdos::mesh::detail::MeshInitState;

TEST(AnonymousMessagePolicy, EnforcesExactAsciiTextBoundary)
{
    const std::string accepted(sigurdos::mesh::MAX_ANON_TEXT_BYTES, 'a');
    const std::string rejected(sigurdos::mesh::MAX_ANON_TEXT_BYTES + 1, 'a');
    size_t text_len = 0;

    EXPECT_TRUE(sigurdos::mesh::anonymousTextFits(accepted.c_str(), &text_len));
    EXPECT_EQ(text_len, sigurdos::mesh::MAX_ANON_TEXT_BYTES);
    EXPECT_FALSE(sigurdos::mesh::anonymousTextFits(rejected.c_str(), &text_len));
    EXPECT_EQ(text_len, 0u);
}

TEST(AnonymousMessagePolicy, CountsUtf8PayloadInBytes)
{
    const std::string accepted(173, 'a');
    const std::string rejected(174, 'a');

    EXPECT_TRUE(sigurdos::mesh::anonymousTextFits(
        (accepted + "\xC3\xA9").c_str()));
    EXPECT_FALSE(sigurdos::mesh::anonymousTextFits(
        (rejected + "\xC3\xA9").c_str()));
}

TEST(AnonymousMessagePolicy, EnforcesInnerRequestBudget)
{
    EXPECT_TRUE(sigurdos::mesh::anonymousRequestFits(
        sigurdos::mesh::MAX_ANON_REQUEST_BYTES));
    EXPECT_FALSE(sigurdos::mesh::anonymousRequestFits(
        sigurdos::mesh::MAX_ANON_REQUEST_BYTES + 1));
    EXPECT_FALSE(sigurdos::mesh::anonymousTextFits(nullptr));
    EXPECT_FALSE(sigurdos::mesh::anonymousTextFits(""));
}

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
    ASSERT_TRUE(scopeKeyHexDecode(hex, back));
    EXPECT_EQ(memcmp(key, back, 16), 0);
}

TEST(ScopeKeyHex, DecodeIsCaseInsensitive)
{
    uint8_t lower[16], upper[16];
    ASSERT_TRUE(scopeKeyHexDecode(
        "00010a0f107f80abcdefff42995aa53c", lower));
    ASSERT_TRUE(scopeKeyHexDecode(
        "00010A0F107F80ABCDEFFF42995AA53C", upper));
    EXPECT_EQ(memcmp(lower, upper, 16), 0);
    EXPECT_EQ(lower[7], 0xAB);
}

TEST(ScopeKeyHex, RejectsMalformedAndZeroKeysWithoutPartialOutput)
{
    uint8_t out[16];
    memset(out, 0xEE, sizeof(out));
    EXPECT_FALSE(scopeKeyHexDecode(
        "zzg57Zz9!!~~--++..##qqrrssttuuvv", out));
    EXPECT_FALSE(scopeKeyHexDecode("1234", out));
    EXPECT_FALSE(scopeKeyHexDecode(
        "00000000000000000000000000000000", out));
    for (uint8_t byte : out) EXPECT_EQ(byte, 0xEE);
}

TEST(ScopeKeyHex, NullArgumentsAreNoOps)
{
    uint8_t key[16] = {1};
    char hex[33] = "unchanged";
    scopeKeyHexEncode(nullptr, hex);
    EXPECT_STREQ(hex, "unchanged");
    scopeKeyHexEncode(key, nullptr);   // must not crash
    uint8_t out[16] = {7};
    EXPECT_FALSE(scopeKeyHexDecode(nullptr, out));
    EXPECT_EQ(out[0], 7);
    EXPECT_FALSE(scopeKeyHexDecode("00", nullptr));
}

TEST(ScopeKeyBase64, DecodesCanonicalPrivateRegionKey)
{
    uint8_t out[16]{};
    ASSERT_TRUE(scopeKeyBase64Decode("AAECAwQFBgcICQoLDA0ODw==", out));
    for (int i = 0; i < 16; ++i) EXPECT_EQ(out[i], (uint8_t)i);
}

TEST(ScopeKeyBase64, RejectsMalformedOrNonCanonicalKeys)
{
    uint8_t out[16]{};
    EXPECT_FALSE(scopeKeyBase64Decode(nullptr, out));
    EXPECT_FALSE(scopeKeyBase64Decode("AAECAwQFBgcICQoLDA0ODw=", out));
    EXPECT_FALSE(scopeKeyBase64Decode("AAECAwQFBgcICQoLDA0ODw=!", out));
    EXPECT_FALSE(scopeKeyBase64Decode("AAECAwQFBgcICQoLDA0ODx==", out));
    EXPECT_FALSE(scopeKeyBase64Decode("AAAAAAAAAAAAAAAAAAAAAA==", out));
    EXPECT_FALSE(scopeKeyBase64Decode("AAECAwQFBgcICQoLDA0ODw==", nullptr));
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

struct TrackedInitResource {
    explicit TrackedInitResource(int value) : id(value) {}
    ~TrackedInitResource() { destroyed.push_back(id); }

    int id;
    static std::vector<int> destroyed;
};

std::vector<int> TrackedInitResource::destroyed;

TEST(MeshInitLifecycle, DistinguishesStoppedClockOnlyAndReady)
{
    EXPECT_FALSE(sigurdos::mesh::detail::meshInitUsable(
        MeshInitState::Stopped));
    EXPECT_TRUE(sigurdos::mesh::detail::meshInitUsable(
        MeshInitState::ClockOnly));
    EXPECT_TRUE(sigurdos::mesh::detail::meshInitUsable(
        MeshInitState::Ready));
}

TEST(MeshInitLifecycle, EarlyShutdownQuiescesAndSleepsWithoutPersistence)
{
    std::vector<int> order;
    const bool persisted = sigurdos::mesh::detail::coordinateShutdown(
        MeshInitState::Stopped,
        [&order]() { order.push_back(1); },
        [&order]() {
            order.push_back(2);
            return false;
        },
        [&order]() { order.push_back(3); });

    EXPECT_TRUE(persisted);
    EXPECT_EQ(order, (std::vector<int>{1, 3}));
}

TEST(MeshInitLifecycle, UsableShutdownPersistsBeforeSleepEvenOnWriteFailure)
{
    std::vector<int> order;
    const bool persisted = sigurdos::mesh::detail::coordinateShutdown(
        MeshInitState::Ready,
        [&order]() { order.push_back(1); },
        [&order]() {
            order.push_back(2);
            return false;
        },
        [&order]() { order.push_back(3); });

    EXPECT_FALSE(persisted);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(MeshInitLifecycle, CleansEveryPartialAllocationInReverseOrder)
{
    for (int allocated = 0; allocated <= 4; ++allocated) {
        TrackedInitResource::destroyed.clear();
        TrackedInitResource* module = allocated >= 1
            ? new TrackedInitResource(1) : nullptr;
        TrackedInitResource* radio = allocated >= 2
            ? new TrackedInitResource(2) : nullptr;
        TrackedInitResource* driver = allocated >= 3
            ? new TrackedInitResource(3) : nullptr;
        TrackedInitResource* mesh = allocated >= 4
            ? new TrackedInitResource(4) : nullptr;
        bool module_cleanup_called = false;

        sigurdos::mesh::detail::cleanupMeshInitResources(
            mesh, driver, radio, module,
            [&module_cleanup_called](TrackedInitResource&) {
                module_cleanup_called = true;
            });

        EXPECT_EQ(mesh, nullptr);
        EXPECT_EQ(driver, nullptr);
        EXPECT_EQ(radio, nullptr);
        EXPECT_EQ(module, nullptr);
        EXPECT_EQ(module_cleanup_called, allocated >= 1);
        ASSERT_EQ(TrackedInitResource::destroyed.size(),
                  (size_t)allocated);
        for (int i = 0; i < allocated; ++i) {
            EXPECT_EQ(TrackedInitResource::destroyed[i], allocated - i);
        }
    }
}

} // namespace
