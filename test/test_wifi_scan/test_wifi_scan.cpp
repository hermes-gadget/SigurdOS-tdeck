// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include <gtest/gtest.h>

#include <cstring>

#include "hal/wifi_ota.h"
#include "hal/wifi_coordinator.h"

namespace {

sigurdos::wifi_scan::APInfo make_ap(const char* ssid, int rssi, int channel,
                                    bool encrypted) {
    sigurdos::wifi_scan::APInfo ap{};
    std::strncpy(ap.ssid, ssid, sizeof(ap.ssid) - 1);
    ap.rssi = rssi;
    ap.channel = channel;
    ap.encrypted = encrypted;
    return ap;
}

sigurdos::wifi::RadioMode requested_mode(sigurdos::wifi::Owner owner) {
    using sigurdos::wifi::Owner;
    using sigurdos::wifi::RadioMode;
    return owner == Owner::ApOta ? RadioMode::Ap : RadioMode::Sta;
}

TEST(WifiScanTest, MaxApCountIsDocumentedLimit) {
    EXPECT_EQ(sigurdos::wifi_scan::SIGURDOS_WIFI_SCAN_MAX_APS, 30);
}

TEST(WifiScanTest, LimitScanCountRejectsInvalidInputs) {
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(-1, 10), 0);
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(0, 10), 0);
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(5, 0), 0);
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(5, -1), 0);
}

TEST(WifiScanTest, LimitScanCountUsesSmallerOfFoundCapacityAndMax) {
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(12, 20), 12);
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(40, 20), 20);
    EXPECT_EQ(sigurdos::wifi_scan::limitScanCount(40, 64), 30);
}

TEST(WifiScanTest, SortByRssiOrdersStrongestFirst) {
    sigurdos::wifi_scan::APInfo aps[] = {
        make_ap("weak", -88, 11, true),
        make_ap("strong", -42, 1, false),
        make_ap("middle", -66, 6, true),
    };

    sigurdos::wifi_scan::sortByRssi(aps, 3);

    EXPECT_STREQ(aps[0].ssid, "strong");
    EXPECT_EQ(aps[0].rssi, -42);
    EXPECT_STREQ(aps[1].ssid, "middle");
    EXPECT_EQ(aps[1].rssi, -66);
    EXPECT_STREQ(aps[2].ssid, "weak");
    EXPECT_EQ(aps[2].rssi, -88);
}

TEST(WifiScanTest, SortByRssiPreservesTieOrder) {
    sigurdos::wifi_scan::APInfo aps[] = {
        make_ap("first", -55, 1, false),
        make_ap("second", -55, 6, true),
        make_ap("third", -55, 11, true),
    };

    sigurdos::wifi_scan::sortByRssi(aps, 3);

    EXPECT_STREQ(aps[0].ssid, "first");
    EXPECT_STREQ(aps[1].ssid, "second");
    EXPECT_STREQ(aps[2].ssid, "third");
}

TEST(WifiScanTest, SortByRssiHandlesNullAndTrivialInputs) {
    sigurdos::wifi_scan::sortByRssi(nullptr, 3);
    sigurdos::wifi_scan::sortByRssi(nullptr, 0);

    sigurdos::wifi_scan::APInfo ap = make_ap("solo", -50, 1, false);
    sigurdos::wifi_scan::sortByRssi(&ap, 0);
    sigurdos::wifi_scan::sortByRssi(&ap, 1);

    EXPECT_STREQ(ap.ssid, "solo");
    EXPECT_EQ(ap.rssi, -50);
}

TEST(WifiScanTest, LongScanResultsAreConsumedAcrossBoundedBatches) {
    using sigurdos::wifi_scan::AsyncScanState;
    using sigurdos::wifi_scan::Status;
    AsyncScanState state;
    const uint32_t token = state.start();
    state.resultsReady(token, 30, 30);

    int consumed = 0;
    while (state.status(token) == Status::Running) {
        int batch = 0;
        int index = -1;
        while (batch < sigurdos::wifi_scan::SIGURDOS_WIFI_SCAN_RESULTS_PER_POLL &&
               state.takeNext(token, index)) {
            EXPECT_EQ(index, consumed++);
            ++batch;
        }
        EXPECT_LE(batch, sigurdos::wifi_scan::SIGURDOS_WIFI_SCAN_RESULTS_PER_POLL);
    }

    EXPECT_EQ(consumed, 30);
    EXPECT_EQ(state.status(token), Status::Complete);
}

TEST(WifiScanTest, LongRunningSurveyRemainsResponsiveAndCancellable) {
    using sigurdos::wifi_scan::AsyncScanState;
    using sigurdos::wifi_scan::Status;
    AsyncScanState state;
    const uint32_t token = state.start();

    for (int poll = 0; poll < 1000; ++poll) {
        EXPECT_EQ(state.status(token), Status::Running);
        EXPECT_EQ(state.count(token), 0);
    }

    EXPECT_TRUE(state.finish(token, Status::Cancelled));
    EXPECT_EQ(state.status(token), Status::Cancelled);
}

TEST(WifiScanTest, NoResultsCompletesWithoutConsumption) {
    sigurdos::wifi_scan::AsyncScanState state;
    const uint32_t token = state.start();
    state.resultsReady(token, 0, 30);

    int index = -1;
    EXPECT_FALSE(state.takeNext(token, index));
    EXPECT_EQ(state.count(token), 0);
    EXPECT_EQ(state.status(token), sigurdos::wifi_scan::Status::Complete);
}

TEST(WifiScanTest, ErrorAndCancellationAreTerminal) {
    using sigurdos::wifi_scan::AsyncScanState;
    using sigurdos::wifi_scan::Status;
    AsyncScanState state;

    const uint32_t failed = state.start();
    EXPECT_TRUE(state.finish(failed, Status::Error));
    EXPECT_EQ(state.status(failed), Status::Error);
    EXPECT_FALSE(state.finish(failed, Status::Cancelled));

    const uint32_t cancelled = state.start();
    EXPECT_TRUE(state.finish(cancelled, Status::Cancelled));
    EXPECT_EQ(state.status(cancelled), Status::Cancelled);
}

TEST(WifiScanTest, NavigationDeleteCannotCancelNewerScan) {
    using sigurdos::wifi_scan::AsyncScanState;
    using sigurdos::wifi_scan::Status;
    AsyncScanState state;
    const uint32_t old_screen_token = state.start();
    EXPECT_TRUE(state.finish(old_screen_token, Status::Cancelled));

    const uint32_t new_screen_token = state.start();
    EXPECT_NE(old_screen_token, new_screen_token);
    EXPECT_FALSE(state.finish(old_screen_token, Status::Cancelled));
    EXPECT_EQ(state.status(new_screen_token), Status::Running);
}

TEST(WifiCoordinatorTest, EveryOwnerPairHasAnExplicitTransitionPolicy) {
    using sigurdos::wifi::Coordinator;
    using sigurdos::wifi::Owner;
    using sigurdos::wifi::RadioMode;
    constexpr Owner owners[] = {
        Owner::Sta, Owner::Scan, Owner::ApOta, Owner::GitHubOta,
    };

    for (Owner active : owners) {
        for (Owner requested : owners) {
            Coordinator coordinator;
            ASSERT_TRUE(coordinator.acquire(active, requested_mode(active),
                                            RadioMode::Off));
            const bool expected = requested == active ||
                                  (active == Owner::Sta && requested != Owner::Sta);
            EXPECT_EQ(coordinator.canAcquire(requested), expected);
            EXPECT_EQ(coordinator.acquire(requested, requested_mode(requested),
                                          coordinator.currentMode()), expected);
            EXPECT_EQ(coordinator.currentOwner(), expected ? requested : active);
        }
    }
}

TEST(WifiCoordinatorTest, ScanAndOtaCleanupRestoreSuspendedStaLease) {
    using sigurdos::wifi::Coordinator;
    using sigurdos::wifi::Owner;
    using sigurdos::wifi::RadioMode;
    constexpr Owner transient_owners[] = {
        Owner::Scan, Owner::ApOta, Owner::GitHubOta,
    };

    for (Owner transient : transient_owners) {
        Coordinator coordinator;
        ASSERT_TRUE(coordinator.acquire(Owner::Sta, RadioMode::Sta,
                                        RadioMode::Off));
        ASSERT_TRUE(coordinator.acquire(transient, requested_mode(transient),
                                        RadioMode::Sta));

        const auto transient_release = coordinator.release(transient);
        EXPECT_TRUE(transient_release.released);
        EXPECT_EQ(transient_release.restored_owner, Owner::Sta);
        EXPECT_EQ(transient_release.restored_mode, RadioMode::Sta);
        EXPECT_EQ(coordinator.currentOwner(), Owner::Sta);

        const auto sta_release = coordinator.release(Owner::Sta);
        EXPECT_TRUE(sta_release.released);
        EXPECT_EQ(sta_release.restored_owner, Owner::None);
        EXPECT_EQ(sta_release.restored_mode, RadioMode::Off);
    }
}

TEST(WifiCoordinatorTest, TimeoutOrErrorCleanupRestoresPreexistingMode) {
    using sigurdos::wifi::Coordinator;
    using sigurdos::wifi::Owner;
    using sigurdos::wifi::RadioMode;
    constexpr Owner owners[] = {
        Owner::Sta, Owner::Scan, Owner::ApOta, Owner::GitHubOta,
    };

    for (Owner owner : owners) {
        Coordinator coordinator;
        ASSERT_TRUE(coordinator.acquire(owner, requested_mode(owner),
                                        RadioMode::ApSta));
        const auto cleanup = coordinator.release(owner);
        EXPECT_TRUE(cleanup.released);
        EXPECT_EQ(cleanup.restored_owner, Owner::None);
        EXPECT_EQ(cleanup.restored_mode, RadioMode::ApSta);
        EXPECT_EQ(coordinator.currentMode(), RadioMode::ApSta);
    }
}

TEST(WifiCoordinatorTest, ConflictAndWrongReleaseNeverMutateActiveLease) {
    using sigurdos::wifi::Coordinator;
    using sigurdos::wifi::Owner;
    using sigurdos::wifi::RadioMode;
    Coordinator coordinator;
    ASSERT_TRUE(coordinator.acquire(Owner::GitHubOta, RadioMode::Sta,
                                    RadioMode::Off));

    EXPECT_FALSE(coordinator.acquire(Owner::Scan, RadioMode::Sta,
                                     RadioMode::Sta));
    EXPECT_FALSE(coordinator.release(Owner::Scan).released);
    EXPECT_EQ(coordinator.currentOwner(), Owner::GitHubOta);
    EXPECT_EQ(coordinator.depth(), 1);
}

} // namespace
