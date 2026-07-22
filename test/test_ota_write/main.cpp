#include <gtest/gtest.h>

#include "hal/ota_write_policy.h"

#include <limits>

namespace {

using sigurdos::hal::OtaWriteResult;
using sigurdos::hal::otaRecordExactWrite;

TEST(OtaWritePolicyTest, ExactChunksAdvanceWithoutExceedingImage) {
    size_t next = 0;
    EXPECT_EQ(otaRecordExactWrite(0, 512, 512, 1024, &next),
              OtaWriteResult::Accepted);
    EXPECT_EQ(next, 512U);
    EXPECT_EQ(otaRecordExactWrite(next, 512, 512, 1024, &next),
              OtaWriteResult::Accepted);
    EXPECT_EQ(next, 1024U);
}

TEST(OtaWritePolicyTest, EveryPartialWriteIsRejectedWithoutAdvancing) {
    for (size_t written = 0; written < 64; ++written) {
        size_t next = 17;
        EXPECT_EQ(otaRecordExactWrite(17, 64, written, 1024, &next),
                  OtaWriteResult::ShortWrite) << written;
        EXPECT_EQ(next, 17U) << written;
    }
}

TEST(OtaWritePolicyTest, EmptyOverrunOverflowAndNullOutputAreRejected) {
    size_t next = 9;
    EXPECT_EQ(otaRecordExactWrite(9, 0, 0, 10, &next),
              OtaWriteResult::EmptyChunk);
    EXPECT_EQ(otaRecordExactWrite(9, 2, 2, 10, &next),
              OtaWriteResult::ExceedsExpected);
    EXPECT_EQ(otaRecordExactWrite(std::numeric_limits<size_t>::max(), 1, 1,
                                  0, &next),
              OtaWriteResult::CounterOverflow);
    EXPECT_EQ(otaRecordExactWrite(0, 1, 1, 1, nullptr),
              OtaWriteResult::InvalidArgument);
}

struct FakeUpdateSession {
    size_t accepted = 0;
    int writes = 0;
    int aborts = 0;
    bool failed = false;

    void consume(size_t requested, size_t actual, size_t total) {
        if (failed) return;
        ++writes;
        size_t next = accepted;
        const OtaWriteResult result = otaRecordExactWrite(
            accepted, requested, actual, total, &next);
        if (result != OtaWriteResult::Accepted) {
            failed = true;
            ++aborts;
            return;
        }
        accepted = next;
    }
};

TEST(OtaWritePolicyTest, FailureAbortsOnceAndRetryCannotMutateProgress) {
    FakeUpdateSession update;
    update.consume(128, 128, 512);
    update.consume(128, 63, 512);
    update.consume(128, 128, 512);

    EXPECT_TRUE(update.failed);
    EXPECT_EQ(update.accepted, 128U);
    EXPECT_EQ(update.writes, 2);
    EXPECT_EQ(update.aborts, 1);
}

TEST(OtaWritePolicyTest, DisconnectAfterCompleteChunksLeavesBoundedProgress) {
    FakeUpdateSession update;
    update.consume(256, 256, 1024);
    update.consume(256, 256, 1024);

    EXPECT_FALSE(update.failed);
    EXPECT_EQ(update.accepted, 512U);
    EXPECT_LT(update.accepted, 1024U);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
