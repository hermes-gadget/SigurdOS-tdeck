// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "comms/ble_frame_queue.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using sigurdos::comms::BleFrameQueue;

constexpr size_t MAX_LEN = 176;   // MAX_FRAME_SIZE on target
constexpr size_t CAPACITY = 4;    // FRAME_QUEUE_SIZE on target

using Queue = BleFrameQueue<MAX_LEN, CAPACITY>;

// Frames carry a 32-bit sequence number followed by a deterministic fill so
// the consumer can detect torn or corrupted frames.
size_t buildFrame(uint32_t seq, uint8_t* out)
{
    const size_t len = 5 + (seq % (MAX_LEN - 5));
    memcpy(out, &seq, sizeof(seq));
    for (size_t i = sizeof(seq); i < len; i++) {
        out[i] = (uint8_t)(seq ^ (0x5A + i));
    }
    return len;
}

bool frameValid(const uint8_t* frame, size_t len, uint32_t* seq_out)
{
    if (len < 5) return false;
    uint32_t seq = 0;
    memcpy(&seq, frame, sizeof(seq));
    if (len != 5 + (seq % (MAX_LEN - 5))) return false;
    for (size_t i = sizeof(seq); i < len; i++) {
        if (frame[i] != (uint8_t)(seq ^ (0x5A + i))) return false;
    }
    *seq_out = seq;
    return true;
}

TEST(BleFrameQueue, PreservesFifoOrderAndContents)
{
    Queue queue;
    uint8_t frame[MAX_LEN];
    for (uint32_t seq = 0; seq < CAPACITY; seq++) {
        const size_t len = buildFrame(seq, frame);
        EXPECT_TRUE(queue.push(frame, len));
    }
    EXPECT_EQ(queue.size(), CAPACITY);

    for (uint32_t expected = 0; expected < CAPACITY; expected++) {
        uint8_t out[MAX_LEN];
        const size_t len = queue.pop(out);
        ASSERT_GT(len, 0u);
        uint32_t seq = 0;
        ASSERT_TRUE(frameValid(out, len, &seq));
        EXPECT_EQ(seq, expected);
    }
    EXPECT_EQ(queue.size(), 0u);
}

TEST(BleFrameQueue, DropsWhenFullAndRejectsBadFrames)
{
    Queue queue;
    uint8_t frame[MAX_LEN] = {1};
    for (size_t i = 0; i < CAPACITY; i++) {
        EXPECT_TRUE(queue.push(frame, 8));
    }
    EXPECT_FALSE(queue.push(frame, 8));          // full
    EXPECT_EQ(queue.size(), CAPACITY);

    queue.clear();
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_FALSE(queue.push(nullptr, 8));        // null data
    EXPECT_FALSE(queue.push(frame, 0));          // empty
    EXPECT_FALSE(queue.push(frame, MAX_LEN + 1));// oversize

    uint8_t out[MAX_LEN];
    EXPECT_EQ(queue.pop(out), 0u);               // empty pop
    EXPECT_EQ(queue.pop(nullptr), 0u);           // null dest
}

TEST(BleFrameQueue, WrapsAroundRepeatedly)
{
    Queue queue;
    uint8_t frame[MAX_LEN];
    uint8_t out[MAX_LEN];
    uint32_t next_pop = 0;
    for (uint32_t seq = 0; seq < 40; seq++) {
        const size_t len = buildFrame(seq, frame);
        ASSERT_TRUE(queue.push(frame, len));
        if (seq % 2 == 1) {                       // drain in bursts of two
            for (int i = 0; i < 2; i++) {
                const size_t got = queue.pop(out);
                ASSERT_GT(got, 0u);
                uint32_t got_seq = 0;
                ASSERT_TRUE(frameValid(out, got, &got_seq));
                EXPECT_EQ(got_seq, next_pop++);
            }
        }
    }
    EXPECT_EQ(queue.size(), 0u);
}

// The NET-002 scenario: a producer thread (standing in for the Bluedroid host
// task) pushes while a consumer thread (the app loop) pops. Every frame that
// crosses must arrive intact and in order; frames pushed while the queue is
// full are dropped by the producer, never corrupted.
TEST(BleFrameQueue, ConcurrentProducerConsumerStress)
{
    constexpr uint32_t TOTAL = 20000;
    Queue queue;
    std::atomic<uint32_t> dropped{0};
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        uint8_t frame[MAX_LEN];
        for (uint32_t seq = 0; seq < TOTAL; seq++) {
            const size_t len = buildFrame(seq, frame);
            if (!queue.push(frame, len)) {
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    uint32_t received = 0;
    uint32_t last_seq = 0;
    bool have_last = false;
    bool corrupt = false;
    bool out_of_order = false;
    uint8_t out[MAX_LEN];
    for (;;) {
        const size_t len = queue.pop(out);
        if (len == 0) {
            if (producer_done.load(std::memory_order_acquire) &&
                queue.size() == 0) {
                break;
            }
            std::this_thread::yield();
            continue;
        }
        uint32_t seq = 0;
        if (!frameValid(out, len, &seq)) {
            corrupt = true;
            break;
        }
        if (have_last && seq <= last_seq) {
            out_of_order = true;
            break;
        }
        last_seq = seq;
        have_last = true;
        received++;
    }
    producer.join();

    EXPECT_FALSE(corrupt);
    EXPECT_FALSE(out_of_order);
    EXPECT_EQ(received + dropped.load(), TOTAL);
    EXPECT_GT(received, 0u);
}

} // namespace
