#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "hal/atomic_file.h"
#include "mesh/message_store.h"

namespace {

sigurdos::mesh::StoredMessage makeMsg(const char* conversation,
                                      const char* sender,
                                      const char* text,
                                      uint32_t ts,
                                      bool self,
                                      bool channel)
{
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, conversation, sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, sender, sizeof(msg.sender) - 1);
    std::strncpy(msg.text, text, sizeof(msg.text) - 1);
    msg.timestamp = ts;
    msg.is_self = self;
    msg.is_channel = channel;
    msg.rssi = -70;
    msg.snr_quarters = 12;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);
    return msg;
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

void writeFile(const char* path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              (std::streamsize)bytes.size());
}

std::vector<uint8_t> downgradeStore(const std::vector<uint8_t>& current,
                                    uint8_t version)
{
    uint32_t count = 0;
    std::memcpy(&count, current.data() + 5, sizeof(count));
    const size_t header = version == 4
        ? sigurdos::mesh::detail::MESSAGE_STORE_V4_HEADER_SIZE
        : sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE;
    std::vector<uint8_t> old(current.begin(), current.begin() + header);
    old[4] = version;
    constexpr size_t attempt_offset =
        4 + sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN +
        sigurdos::mesh::SIGURDOS_MSG_SENDER_LEN +
        sigurdos::mesh::SIGURDOS_MSG_TEXT_LEN + 4 +
        sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN + 2 + 1 + 1 + 1;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t start = sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
            (size_t)i * sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE;
        old.insert(old.end(), current.begin() + start,
                   current.begin() + start + attempt_offset);
        old.insert(old.end(), current.begin() + start + attempt_offset + 1,
                   current.begin() + start +
                       sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE);
        old.back() &= 0x0F;
    }
    return old;
}

class MessageStoreTest : public ::testing::Test {
protected:
    char path[128]{};

    void SetUp() override {
        std::snprintf(path, sizeof(path), "/tmp/sigurdos_msg_store_%d.bin",
                      ::testing::UnitTest::GetInstance()->random_seed());
        sigurdos::mesh::messageStoreSetNativePath(path);
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        std::remove(path);
        std::remove((std::string(path) + ".tmp").c_str());
        ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
    }

    void TearDown() override {
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        std::remove(path);
        std::remove((std::string(path) + ".tmp").c_str());
    }
};

TEST_F(MessageStoreTest, AppendLoadAndDedup) {
    auto msg = makeMsg("DM: Alice", "Alice", "hello", 42, false, false);

    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 4);
    ASSERT_EQ(n, 1);
    EXPECT_STREQ(out[0].conversation, "DM: Alice");
    EXPECT_STREQ(out[0].sender, "Alice");
    EXPECT_STREQ(out[0].text, "hello");
    EXPECT_EQ(out[0].timestamp, 42u);
    EXPECT_FALSE(out[0].is_self);
}

TEST_F(MessageStoreTest, PathLenRoundTrips) {
    auto msg = makeMsg("Public", "Alice", "Alice: hi", 100, false, true);
    msg.path_len = 0x83;  // distinct from the zero-init default
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    sigurdos::mesh::StoredMessage out[2]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 2);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].path_len, 0x83);
}

TEST_F(MessageStoreTest, LoadRecentFiltersAndPreservesOrder) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("#test", "Bob", "two", 2, false, true)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "three", 3, true, false)));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadRecent("DM: Alice", out, 4);
    ASSERT_EQ(n, 2);
    EXPECT_STREQ(out[0].text, "one");
    EXPECT_STREQ(out[1].text, "three");
}

TEST_F(MessageStoreTest, StoreRotatesToNewestRecords) {
    const uint32_t appended = sigurdos::mesh::MESSAGE_STORE_MAX_RECORDS + 1;
    for (uint32_t i = 1; i <= appended; i++) {
        char text[24];
        std::snprintf(text, sizeof(text), "msg%lu", (unsigned long)i);
        EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
            makeMsg("DM: Alice", "Alice", text, i, false, false)));
    }

    EXPECT_EQ(sigurdos::mesh::messageStoreCount(),
              (int)sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    std::vector<sigurdos::mesh::StoredMessage> out(
        sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    int n = sigurdos::mesh::messageStoreLoadAll(out.data(), (int)out.size());
    ASSERT_EQ(n, (int)sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    EXPECT_EQ(out.front().timestamp,
              appended - sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS + 1);
    EXPECT_EQ(out.back().timestamp, appended);
    EXPECT_EQ(readFile(path).size(),
              sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
                  sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS *
                      sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE);

    const uint32_t first_id = out.front().store_id;
    const uint32_t first_timestamp = out.front().timestamp;
    ASSERT_TRUE(sigurdos::mesh::messageStoreMarkAcked(
        "DM: Alice", first_timestamp));
    ASSERT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(first_id));
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(out.data(), (int)out.size()),
              (int)sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    EXPECT_TRUE(out.front().acked);
    EXPECT_TRUE(out.front().companion_sent);
}

TEST_F(MessageStoreTest, DedupIdentityIncludesTextTypeAndSenderPrefix) {
    auto plain = makeMsg("DM: Alice", "Alice", "same", 42, false, false);
    auto cli = plain;
    cli.txt_type = 1;
    auto other_sender_key = plain;
    other_sender_key.sender_prefix[0] ^= 0x01;

    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(plain));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(cli));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(other_sender_key));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 3);
}

TEST_F(MessageStoreTest, DistinctBodiesInSameSecondAreNotDeduplicated) {
    auto first = makeMsg("DM: Alice", "Alice", "first", 42, false, false);
    auto second = first;
    std::strncpy(second.text, "second", sizeof(second.text) - 1);

    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(first));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(second));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(first));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 2);
}

TEST_F(MessageStoreTest, DistinctSignedExtrasInSameSecondAreNotDeduplicated) {
    auto first = makeMsg("DM: Alice", "Alice", "signed", 42, false, false);
    first.txt_type = 2;
    first.extra_len = 4;
    first.extra[0] = 0x11;
    auto second = first;
    second.extra[0] = 0x22;

    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(first));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(second));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 2);
}

TEST_F(MessageStoreTest, LegacyZeroPrefixFallsBackToSenderNameForDedup) {
    auto current = makeMsg("DM: Alice", "Alice", "same", 42, false, false);
    auto legacy = current;
    std::memset(legacy.sender_prefix, 0, sizeof(legacy.sender_prefix));

    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(current));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(legacy));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
}

TEST_F(MessageStoreTest, MarkAckedUpdatesStoredMessage) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "sent", 77, true, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkAcked("DM: Alice", 77));

    sigurdos::mesh::StoredMessage out[2]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 2);
    ASSERT_EQ(n, 1);
    EXPECT_TRUE(out[0].acked);
    EXPECT_FALSE(out[0].confirmation_lost);
}

TEST_F(MessageStoreTest, ConfirmationLostPersistsAndAckClearsIt) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "sent", 78, true, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkConfirmationLost("DM: Alice", 78));

    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_FALSE(out.acked);
    EXPECT_TRUE(out.confirmation_lost);

    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkAcked("DM: Alice", 78));
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_TRUE(out.acked);
    EXPECT_FALSE(out.confirmation_lost);
}

TEST_F(MessageStoreTest, RebootMarksOnlyPendingSelfDmsAsLost) {
    auto acked = makeMsg("DM: Acked", "self", "acked", 81, true, false);
    acked.acked = true;
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(acked));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Pending", "self", "pending", 82, true, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("Public", "self", "channel", 83, true, true)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Incoming", "peer", "incoming", 84, false, false)));

    EXPECT_EQ(sigurdos::mesh::messageStoreMarkOrphanedPendingLost(), 1);
    EXPECT_EQ(sigurdos::mesh::messageStoreMarkOrphanedPendingLost(), 0);

    sigurdos::mesh::StoredMessage out[4]{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(out, 4), 4);
    EXPECT_FALSE(out[0].confirmation_lost);
    EXPECT_TRUE(out[1].confirmation_lost);
    EXPECT_FALSE(out[2].confirmation_lost);
    EXPECT_FALSE(out[3].confirmation_lost);
}

TEST_F(MessageStoreTest, DeliveryStateUpdatesAtEverySupportedStoreSize) {
    auto appendThrough = [&](uint32_t target) {
        for (uint32_t i = (uint32_t)sigurdos::mesh::messageStoreCount() + 1;
             i <= target; ++i) {
            char text[24];
            std::snprintf(text, sizeof(text), "fill%lu", (unsigned long)i);
            const bool self = i == 256 || i == 257 || i == 448 || i == 512;
            ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
                makeMsg("DM: Alice", self ? "self" : "Alice", text, i,
                        self, false)));
        }
    };

    appendThrough(256);
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkConfirmationLost("DM: Alice", 256));
    appendThrough(257);
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkConfirmationLost("DM: Alice", 257));
    appendThrough(448);
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkConfirmationLost("DM: Alice", 448));
    appendThrough(512);
    EXPECT_EQ(sigurdos::mesh::messageStoreMarkOrphanedPendingLost(), 1);

    std::vector<sigurdos::mesh::StoredMessage> out(512);
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(out.data(), (int)out.size()), 512);
    for (uint32_t timestamp : {256U, 257U, 448U, 512U}) {
        EXPECT_TRUE(out[timestamp - 1].confirmation_lost) << timestamp;
    }
}

TEST_F(MessageStoreTest, StoreIdIsMonotonic) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "first", 1, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "second", 2, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "third", 3, false, false)));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 4);
    ASSERT_EQ(n, 3);
    EXPECT_EQ(out[0].store_id, 1u);
    EXPECT_EQ(out[1].store_id, 2u);
    EXPECT_EQ(out[2].store_id, 3u);
}

TEST_F(MessageStoreTest, MarkCompanionSentMarksOnlyOneRecord) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "two", 2, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "three", 3, false, false)));

    // Mark only the second record (store_id=2).
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(2));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 4);
    ASSERT_EQ(n, 3);
    // Record 0 (store_id=1): NOT marked
    EXPECT_FALSE(out[0].companion_sent);
    // Record 1 (store_id=2): marked
    EXPECT_TRUE(out[1].companion_sent);
    // Record 2 (store_id=3): NOT marked
    EXPECT_FALSE(out[2].companion_sent);
}

TEST_F(MessageStoreTest, MarkCompanionSentUpdatesOnlyTheTargetFlagByte) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "two", 2, false, false)));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "three", 3, false, false)));
    const auto before = readFile(path);

    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Write);
    ASSERT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(2));
    const auto after = readFile(path);
    ASSERT_EQ(after.size(), before.size());

    const size_t target = sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
        sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE * 2 - 1;
    ASSERT_LT(target, after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        if (i == target) {
            EXPECT_EQ(after[i], (uint8_t)(before[i] | 0x08));
        } else {
            EXPECT_EQ(after[i], before[i]) << "unexpected change at byte " << i;
        }
    }
    EXPECT_TRUE(readFile((std::string(path) + ".tmp").c_str()).empty());

    ASSERT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(2));
    EXPECT_EQ(readFile(path), after);
}

TEST_F(MessageStoreTest, MarkCompanionSentNotFoundReturnsFalse) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    EXPECT_FALSE(sigurdos::mesh::messageStoreMarkCompanionSent(999));
}

TEST_F(MessageStoreTest, LoadUnsentOnlyReturnsUnmarkedRecords) {
    auto m1 = makeMsg("DM: Alice", "Alice", "one", 1, false, false);
    auto m2 = makeMsg("DM: Alice", "Alice", "two", 2, false, false);
    auto m3 = makeMsg("DM: Bob", "Bob", "three", 3, false, false);

    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(m1));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(m2));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(m3));

    // Mark the second record as sent.
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(2));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadUnsent(out, 4);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(out[0].store_id, 1u);
    EXPECT_EQ(out[1].store_id, 3u);
}

TEST_F(MessageStoreTest, LoadUnsentPagesOldestIncomingRecordsFirst) {
    for (uint32_t i = 1; i <= 20; ++i) {
        char text[24];
        std::snprintf(text, sizeof(text), "message-%lu", (unsigned long)i);
        auto msg = makeMsg("DM: Alice", i == 3 ? "self" : "Alice", text,
                           i, i == 3, false);
        ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    }
    ASSERT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(2));

    sigurdos::mesh::StoredMessage out[16]{};
    const int n = sigurdos::mesh::messageStoreLoadUnsent(out, 16);
    ASSERT_EQ(n, 16);
    EXPECT_EQ(out[0].store_id, 1u);
    EXPECT_EQ(out[1].store_id, 4u);  // sent id 2 and self-sent id 3 are skipped
    EXPECT_EQ(out[15].store_id, 18u);
}

TEST_F(MessageStoreTest, MetadataRoundTrips) {
    auto msg = makeMsg("DM: Alice", "Alice", "hello", 42, false, false);
    msg.txt_type = 2;  // COMPANION_TXT_SIGNED_PLAIN
    msg.extra_len = 4;
    msg.extra[0] = 0xDE;
    msg.extra[1] = 0xAD;
    msg.extra[2] = 0xBE;
    msg.extra[3] = 0xEF;
    msg.sender_prefix[0] = 0x11;
    msg.sender_prefix[1] = 0x22;
    msg.sender_prefix[2] = 0x33;
    msg.sender_prefix[3] = 0x44;
    msg.sender_prefix[4] = 0x55;
    msg.sender_prefix[5] = 0x66;
    msg.attempt = 2;
    msg.attempt_known = true;
    msg.route_known = true;
    msg.route_flood = true;

    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    sigurdos::mesh::StoredMessage out[2]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 2);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].txt_type, 2u);
    EXPECT_EQ(out[0].extra_len, 4u);
    EXPECT_EQ(out[0].extra[0], 0xDE);
    EXPECT_EQ(out[0].extra[1], 0xAD);
    EXPECT_EQ(out[0].extra[2], 0xBE);
    EXPECT_EQ(out[0].extra[3], 0xEF);
    EXPECT_EQ(out[0].sender_prefix[0], 0x11);
    EXPECT_EQ(out[0].sender_prefix[1], 0x22);
    EXPECT_EQ(out[0].sender_prefix[2], 0x33);
    EXPECT_EQ(out[0].attempt, 2u);
    EXPECT_TRUE(out[0].attempt_known);
    EXPECT_TRUE(out[0].route_known);
    EXPECT_TRUE(out[0].route_flood);
}

TEST_F(MessageStoreTest, LookupByIdAndIdentityReturnsDetailRecord) {
    auto msg = makeMsg("DM: Alice", "Alice", "detail", 99, false, false);
    uint32_t store_id = 0;
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg, &store_id));
    sigurdos::mesh::StoredMessage out{};
    ASSERT_TRUE(sigurdos::mesh::messageStoreGetById(store_id, out));
    EXPECT_STREQ(out.text, "detail");
    ASSERT_TRUE(sigurdos::mesh::messageStoreFindRecent(
        "DM: Alice", "Alice", "detail", 100, false, out));
    EXPECT_EQ(out.store_id, store_id);
}

TEST_F(MessageStoreTest, Version5MigratesWithUnknownAttempt) {
    auto msg = makeMsg("DM: Alice", "Alice", "legacy-v5", 42, false, false);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    auto raw = readFile(path);
    ASSERT_EQ(raw.size(), sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
                          sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE);
    raw[4] = 5;
    constexpr size_t attempt_offset =
        4 + sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN +
        sigurdos::mesh::SIGURDOS_MSG_SENDER_LEN +
        sigurdos::mesh::SIGURDOS_MSG_TEXT_LEN + 4 +
        sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN + 2 + 1 + 1 + 1;
    raw.erase(raw.begin() + sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
              attempt_offset);
    raw.back() &= 0x0F;
    writeFile(path, raw);

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.text, "legacy-v5");
    EXPECT_FALSE(out.attempt_known);
    EXPECT_TRUE(out.route_known);
}

TEST_F(MessageStoreTest, Version5MigrationKeepsCompleteRecordsBeforeTornTail) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "first", 1, false, false)));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "second", 2, false, false)));
    auto v5 = downgradeStore(readFile(path), 5);
    v5.resize(sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
              sigurdos::mesh::detail::MESSAGE_STORE_V5_RECORD_SIZE + 17);
    writeFile(path, v5);

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.text, "first");
}

TEST_F(MessageStoreTest, UnknownVersionIsPreservedInsteadOfErased) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "future", 1, false, false)));
    auto future = readFile(path);
    future[4] = sigurdos::mesh::detail::MESSAGE_STORE_VERSION + 1;
    writeFile(path, future);

    EXPECT_FALSE(sigurdos::mesh::messageStoreBegin());
    EXPECT_EQ(readFile(path), future);
}

TEST_F(MessageStoreTest, BeginRecoversRecordWrittenBeforeHeaderCount) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "complete", 42, false, false)));
    auto raw = readFile(path);
    ASSERT_GE(raw.size(), sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
                          sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE);
    const uint32_t stale_count = 0;
    std::memcpy(raw.data() + 5, &stale_count, sizeof(stale_count));
    writeFile(path, raw);

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.text, "complete");
}

TEST_F(MessageStoreTest, BeginDropsOnlyTornTailAndCorrectsCount) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "first", 1, false, false)));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "second", 2, false, false)));
    auto raw = readFile(path);
    raw.resize(sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
               sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE + 13);
    writeFile(path, raw);

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.text, "first");
}

TEST_F(MessageStoreTest, RenameFailureRecoversValidatedWholeStoreUpdate) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "sent", 77, true, false)));
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Rename);
    EXPECT_FALSE(sigurdos::mesh::messageStoreMarkAcked("DM: Alice", 77));
    EXPECT_FALSE(readFile((std::string(path) + ".tmp").c_str()).empty());

    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);
    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_TRUE(out.acked);
}

TEST_F(MessageStoreTest, InvalidTempNeverReplacesLiveStore) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "live", 1, false, false)));
    const std::string temp_path = std::string(path) + ".tmp";
    writeFile(temp_path.c_str(), {0x47, 0x53, 0x4d, 0x53, 0x04, 0x01});

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.text, "live");
    EXPECT_TRUE(readFile(temp_path.c_str()).empty());
}

TEST_F(MessageStoreTest, WholeStoreWriteFailurePreservesLiveStore) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "sent", 77, true, false)));
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Write);
    EXPECT_FALSE(sigurdos::mesh::messageStoreMarkAcked("DM: Alice", 77));
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);

    sigurdos::mesh::StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_FALSE(out.acked);
}

TEST_F(MessageStoreTest, FirstAndRotatedIdsAreNonZeroAndNeverReused) {
    std::set<uint32_t> ids;
    const uint32_t appended = sigurdos::mesh::MESSAGE_STORE_MAX_RECORDS + 1;
    for (uint32_t i = 1; i <= appended; ++i) {
        char text[24];
        std::snprintf(text, sizeof(text), "msg%lu", (unsigned long)i);
        uint32_t id = 0;
        ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
            makeMsg("DM: Alice", "Alice", text, i, false, false), &id));
        EXPECT_NE(id, 0U);
        EXPECT_TRUE(ids.insert(id).second) << "duplicate ID at append " << i;
    }
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(),
              (int)sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    std::vector<sigurdos::mesh::StoredMessage> out(
        sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(out.data(), (int)out.size()),
              (int)sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS);
    EXPECT_EQ(out.front().store_id,
              appended - sigurdos::mesh::MESSAGE_STORE_COMPACT_TO_RECORDS + 1);
    EXPECT_EQ(out.back().store_id, appended);
    EXPECT_FALSE(sigurdos::mesh::messageStoreMarkCompanionSent(0));
}

TEST_F(MessageStoreTest, VersionFourMigratesAmbiguousIdsToNonZeroSequence) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "two", 2, false, false)));
    const auto current = readFile(path);
    ASSERT_EQ(current.size(), sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE +
                         2 * sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE);
    std::vector<uint8_t> v4 = downgradeStore(current, 4);
    const uint32_t zero = 0;
    const uint32_t reused = 1;
    std::memcpy(v4.data() + sigurdos::mesh::detail::MESSAGE_STORE_V4_HEADER_SIZE,
                &zero, sizeof(zero));
    std::memcpy(v4.data() + sigurdos::mesh::detail::MESSAGE_STORE_V4_HEADER_SIZE +
                    sigurdos::mesh::detail::MESSAGE_STORE_V5_RECORD_SIZE,
                &reused, sizeof(reused));
    writeFile(path, v4);

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    sigurdos::mesh::StoredMessage out[2]{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(out, 2), 2);
    EXPECT_EQ(out[0].store_id, 1U);
    EXPECT_EQ(out[1].store_id, 2U);
    const auto migrated = readFile(path);
    ASSERT_GE(migrated.size(), sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE);
    EXPECT_EQ(migrated[4], sigurdos::mesh::detail::MESSAGE_STORE_VERSION);
}

TEST_F(MessageStoreTest, ValidVersionFourTempIsRecoveredBeforeMigration) {
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "old", 1, false, false)));
    const auto old_live = readFile(path);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "new", 2, false, false)));
    const auto current_new = readFile(path);
    std::vector<uint8_t> v4_temp = downgradeStore(current_new, 4);
    writeFile(path, old_live);
    writeFile((std::string(path) + ".tmp").c_str(), v4_temp);

    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    sigurdos::mesh::StoredMessage out[2]{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(out, 2), 2);
    EXPECT_STREQ(out[0].text, "old");
    EXPECT_STREQ(out[1].text, "new");
    EXPECT_EQ(out[0].store_id, 1U);
    EXPECT_EQ(out[1].store_id, 2U);
}

} // namespace
