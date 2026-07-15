// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <cstdint>

#include "hal/display_allocation_policy.h"
#include "hal/ota_allocation_policy.h"

namespace {

struct DisplayHarness {
    uint32_t fail_mask = 0;
    int calls = 0;
    int deleted_inputs = 0;

    void* allocate() {
        const int call = calls++;
        if ((fail_mask & (1U << call)) != 0) return nullptr;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(call + 1));
    }

    static void* createDisplay(void* raw, int32_t, int32_t) {
        return static_cast<DisplayHarness*>(raw)->allocate();
    }
    static void* createInput(void* raw) {
        return static_cast<DisplayHarness*>(raw)->allocate();
    }
    static void* createGroup(void* raw) {
        return static_cast<DisplayHarness*>(raw)->allocate();
    }
    static void deleteInput(void* raw, void*) {
        static_cast<DisplayHarness*>(raw)->deleted_inputs++;
    }

    sigurdos::hal::DisplayAllocationOps ops() {
        return {this, createDisplay, createInput, createGroup, deleteInput};
    }
};

TEST(HalOomTest, DisplayCreationFailureIsReturnedBeforeDereference) {
    DisplayHarness failing;
    failing.fail_mask = 1U;
    void* display = reinterpret_cast<void*>(1);

    EXPECT_FALSE(sigurdos::hal::allocate_display_object(
        failing.ops(), 320, 240, &display));
    EXPECT_EQ(nullptr, display);

    DisplayHarness successful;
    EXPECT_TRUE(sigurdos::hal::allocate_display_object(
        successful.ops(), 320, 240, &display));
    EXPECT_NE(nullptr, display);
}

TEST(HalOomTest, EachInputAllocationFailureDegradesOnlyThatClass) {
    for (int failed_call = 0; failed_call < 3; ++failed_call) {
        DisplayHarness harness;
        harness.fail_mask = 1U << failed_call;

        const auto objects = sigurdos::hal::allocate_display_inputs(harness.ops());

        EXPECT_EQ(failed_call != 0, objects.touch != nullptr);
        EXPECT_EQ(failed_call != 1, objects.keyboard != nullptr);
        EXPECT_EQ(failed_call != 2, objects.trackball != nullptr);
        EXPECT_NE(nullptr, objects.group);
        EXPECT_FALSE(objects.group_allocation_failed);
        EXPECT_EQ(0, harness.deleted_inputs);
    }
}

TEST(HalOomTest, GroupFailureReleasesOnlyKeyboardAndTrackball) {
    DisplayHarness harness;
    harness.fail_mask = 1U << 3;

    const auto objects = sigurdos::hal::allocate_display_inputs(harness.ops());

    EXPECT_NE(nullptr, objects.touch);
    EXPECT_EQ(nullptr, objects.keyboard);
    EXPECT_EQ(nullptr, objects.trackball);
    EXPECT_EQ(nullptr, objects.group);
    EXPECT_TRUE(objects.group_allocation_failed);
    EXPECT_EQ(2, harness.deleted_inputs);
}

struct OtaHarness {
    sigurdos::hal::OtaAllocationKind fail_kind =
        sigurdos::hal::OtaAllocationKind::WebServer;
    bool should_fail = false;
    int created = 0;
    int destroyed = 0;

    static void* create(void* raw, sigurdos::hal::OtaAllocationKind kind) {
        auto* self = static_cast<OtaHarness*>(raw);
        if (self->should_fail && kind == self->fail_kind) return nullptr;
        self->created++;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(self->created));
    }
    static void destroy(void* raw, sigurdos::hal::OtaAllocationKind, void*) {
        static_cast<OtaHarness*>(raw)->destroyed++;
    }

    sigurdos::hal::OtaAllocationOps ops() {
        return {this, create, destroy};
    }
};

TEST(HalOomTest, WebServerAllocationFailureIsReturned) {
    OtaHarness harness;
    harness.should_fail = true;
    void* server = reinterpret_cast<void*>(1);

    EXPECT_FALSE(sigurdos::hal::allocate_ota_object(
        harness.ops(), sigurdos::hal::OtaAllocationKind::WebServer, &server));
    EXPECT_EQ(nullptr, server);
}

void expectHttpBoundaryFailure(sigurdos::hal::OtaAllocationKind client_kind,
                               sigurdos::hal::OtaAllocationKind http_kind) {
    OtaHarness client_failure;
    client_failure.should_fail = true;
    client_failure.fail_kind = client_kind;
    sigurdos::hal::OtaHttpObjects objects;
    EXPECT_FALSE(sigurdos::hal::allocate_ota_http_objects(
        client_failure.ops(), client_kind, http_kind, &objects));
    EXPECT_EQ(nullptr, objects.client);
    EXPECT_EQ(nullptr, objects.http);
    EXPECT_EQ(0, client_failure.destroyed);

    OtaHarness http_failure;
    http_failure.should_fail = true;
    http_failure.fail_kind = http_kind;
    EXPECT_FALSE(sigurdos::hal::allocate_ota_http_objects(
        http_failure.ops(), client_kind, http_kind, &objects));
    EXPECT_EQ(nullptr, objects.client);
    EXPECT_EQ(nullptr, objects.http);
    EXPECT_EQ(1, http_failure.destroyed);
}

TEST(HalOomTest, ApiConnectionFailuresCleanUpPartialAllocation) {
    expectHttpBoundaryFailure(
        sigurdos::hal::OtaAllocationKind::ApiSecureClient,
        sigurdos::hal::OtaAllocationKind::ApiHttpClient);
}

TEST(HalOomTest, DownloadConnectionFailuresCleanUpPartialAllocation) {
    expectHttpBoundaryFailure(
        sigurdos::hal::OtaAllocationKind::DownloadSecureClient,
        sigurdos::hal::OtaAllocationKind::DownloadHttpClient);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
