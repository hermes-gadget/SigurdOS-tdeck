// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/spi_shared.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace {

TEST(SharedSpiArbiterTest, AcquiresAndReleasesEachDeviceBoundary)
{
    for (const SigurdosSharedSpiDevice device : {
             SigurdosSharedSpiDevice::Display,
             SigurdosSharedSpiDevice::SdCard,
             SigurdosSharedSpiDevice::Radio}) {
        SigurdosSharedSpiGuard guard(device, 0);
        EXPECT_TRUE(guard.acquired());
    }
}

TEST(SharedSpiArbiterTest, RecursiveTakeDoesNotSelfDeadlock)
{
    SigurdosSharedSpiGuard radio(
        SigurdosSharedSpiDevice::Radio, 0);
    ASSERT_TRUE(radio.acquired());

    // Incoming-message persistence can enter the SD boundary synchronously
    // from MeshCore's radio loop, so cross-device recursion is intentional.
    SigurdosSharedSpiGuard sd(
        SigurdosSharedSpiDevice::SdCard, 0);
    EXPECT_TRUE(sd.acquired());

    sd.release();
    radio.release();
    SigurdosSharedSpiGuard display(
        SigurdosSharedSpiDevice::Display, 0);
    EXPECT_TRUE(display.acquired());
}

TEST(SharedSpiArbiterTest, ContendedTakeTimesOutWithinBound)
{
    std::promise<void> holder_ready;
    std::promise<void> release_holder;
    std::shared_future<void> release = release_holder.get_future().share();
    std::atomic<bool> holder_acquired{false};

    std::thread holder([&]() {
        SigurdosSharedSpiGuard sd(
            SigurdosSharedSpiDevice::SdCard, 100);
        holder_acquired.store(sd.acquired(), std::memory_order_release);
        holder_ready.set_value();
        release.wait();
    });

    holder_ready.get_future().wait();
    const bool acquired = holder_acquired.load(std::memory_order_acquire);
    if (!acquired) {
        release_holder.set_value();
        holder.join();
    }
    ASSERT_TRUE(acquired);

    const auto started = std::chrono::steady_clock::now();
    SigurdosSharedSpiGuard radio(
        SigurdosSharedSpiDevice::Radio, 10);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    EXPECT_FALSE(radio.acquired());
    EXPECT_GE(elapsed.count(), 5);
    EXPECT_LT(elapsed.count(), 250);

    release_holder.set_value();
    holder.join();
}

TEST(SharedSpiArbiterTest, TimeoutDoesNotPoisonNextAcquisition)
{
    std::promise<void> holder_ready;
    std::promise<void> release_holder;
    std::shared_future<void> release = release_holder.get_future().share();
    std::atomic<bool> holder_acquired{false};

    std::thread holder([&]() {
        SigurdosSharedSpiGuard display(
            SigurdosSharedSpiDevice::Display, 100);
        holder_acquired.store(display.acquired(), std::memory_order_release);
        holder_ready.set_value();
        release.wait();
    });

    holder_ready.get_future().wait();
    const bool acquired = holder_acquired.load(std::memory_order_acquire);
    if (!acquired) {
        release_holder.set_value();
        holder.join();
    }
    ASSERT_TRUE(acquired);

    EXPECT_FALSE(sigurdos_shared_spi_try_lock(
        SigurdosSharedSpiDevice::Radio, 5));
    release_holder.set_value();
    holder.join();

    SigurdosSharedSpiGuard radio(
        SigurdosSharedSpiDevice::Radio, 50);
    EXPECT_TRUE(radio.acquired());
}

} // namespace
