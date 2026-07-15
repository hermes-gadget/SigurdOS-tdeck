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

} // namespace
