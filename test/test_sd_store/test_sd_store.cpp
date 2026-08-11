#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mesh/message_store.h"
#include "mesh/sd_message_store.h"

namespace {

using sigurdos::mesh::StoredMessage;

static constexpr uint64_t MAX_ENCODED_READY_BYTES =
    static_cast<uint64_t>(sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE) +
    static_cast<uint64_t>(sigurdos::mesh::SD_MESSAGE_STORE_MAX_RECORDS + 1) *
        static_cast<uint64_t>(sigurdos::mesh::detail::MESSAGE_STORE_RECORD_SIZE);

StoredMessage makeMsg(uint32_t timestamp, const char* text = nullptr)
{
    StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    if (text) {
        std::strncpy(msg.text, text, sizeof(msg.text) - 1);
    } else {
        std::snprintf(msg.text, sizeof(msg.text), "message-%lu",
                      static_cast<unsigned long>(timestamp));
    }
    msg.timestamp = timestamp;
    msg.rssi = -70;
    msg.snr_quarters = 12;
    for (size_t i = 0; i < sizeof(msg.sender_prefix); ++i) {
        msg.sender_prefix[i] = static_cast<uint8_t>(0xA0 + i);
    }
    return msg;
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

void removeStoreFiles(const std::string& path)
{
    std::remove(path.c_str());
    std::remove((path + sigurdos::mesh::SD_MESSAGE_STORE_TEMP_SUFFIX).c_str());
    std::remove((path + sigurdos::mesh::SD_MESSAGE_STORE_READY_SUFFIX).c_str());
    std::remove((path + sigurdos::mesh::SD_MESSAGE_STORE_CORRUPT_SUFFIX).c_str());
}

bool fileExists(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

// Test double for companionAdapterPrepareFactoryReset(): the callback must
// observe the live selected backend before the SD reset deletes anything.
struct ResetPreflightRecorder {
    std::string live_path;
    bool result = true;
    bool called = false;
    bool backend_selected = false;
    bool live_file_present = false;

    static bool run(void* raw)
    {
        auto* self = static_cast<ResetPreflightRecorder*>(raw);
        self->called = true;
        self->backend_selected =
            sigurdos::mesh::detail::messageStoreBackendSelected();
        self->live_file_present = fileExists(self->live_path);
        return self->result;
    }
};

class SdMessageStoreTest : public ::testing::Test {
protected:
    std::string root;
    std::string spiffs_path;

    void SetUp() override
    {
        const auto* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        std::string suffix = test_info ? test_info->name() : "unknown";
        for (char& c : suffix) {
            if (c < 'A' || (c > 'Z' && c < 'a') || c > 'z') c = '_';
        }
        root = "/tmp/sigurdos_sd_store_" + suffix;
        spiffs_path = root + ".spiffs";
        const std::string sd_path = root + "/msgs";
        removeStoreFiles(spiffs_path);
        removeStoreFiles(sd_path);
        std::remove((root + "/msgs/ignored").c_str());
        (void)std::remove(sd_path.c_str());
        (void)std::remove(root.c_str());

        sigurdos::mesh::sdMessageStoreResetNative();
        sigurdos::mesh::sdMessageStoreSetNativeRoot(root.c_str());
        sigurdos::mesh::sdMessageStoreSetNativeMounted(true);
        sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(-1);
        sigurdos::mesh::sdMessageStoreSetNativeRemoveFailure(false);
        sigurdos::mesh::messageStoreSetNativePath(spiffs_path.c_str());
        sigurdos::mesh::detail::messageStoreSelectDefaultBackend();
        ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
    }

    void TearDown() override
    {
        sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(-1);
        sigurdos::mesh::sdMessageStoreSetNativeRemoveFailure(false);
        sigurdos::mesh::sdMessageStoreSetNativeMounted(false);
        sigurdos::mesh::detail::messageStoreSelectDefaultBackend();
        removeStoreFiles(spiffs_path);
        removeStoreFiles(root + "/msgs");
        (void)std::remove((root + "/msgs").c_str());
        (void)std::remove(root.c_str());
    }

    void selectSd()
    {
        ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
        ASSERT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());
        ASSERT_FALSE(sigurdos::mesh::sdMessageStoreDegraded());
    }
};

TEST_F(SdMessageStoreTest, SelectsSdAndUsesFiveThousandRecordCapacity)
{
    selectSd();
    EXPECT_EQ(sigurdos::mesh::sdMessageStoreCapacity(),
              sigurdos::mesh::SD_MESSAGE_STORE_MAX_RECORDS);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(1, "hello")));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);

    StoredMessage out{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.text, "hello");
    EXPECT_FALSE(readFile(spiffs_path).empty());
    EXPECT_FALSE(readFile(sigurdos::mesh::sdMessageStoreNativePath()).empty());
}

TEST_F(SdMessageStoreTest, CompactsOnlyAfterCapacityAndKeepsRecentBatch)
{
    selectSd();
    for (uint32_t timestamp = 1;
         timestamp <= sigurdos::mesh::SD_MESSAGE_STORE_MAX_RECORDS + 1;
         ++timestamp) {
        ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(timestamp)));
    }

    EXPECT_EQ(sigurdos::mesh::messageStoreCount(),
              static_cast<int>(sigurdos::mesh::SD_MESSAGE_STORE_COMPACT_TO_RECORDS));
    std::vector<StoredMessage> messages(
        sigurdos::mesh::SD_MESSAGE_STORE_COMPACT_TO_RECORDS);
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(
                  messages.data(), static_cast<int>(messages.size())),
              static_cast<int>(messages.size()));
    EXPECT_EQ(messages.front().timestamp,
              sigurdos::mesh::SD_MESSAGE_STORE_MAX_RECORDS + 1 -
                  sigurdos::mesh::SD_MESSAGE_STORE_COMPACT_TO_RECORDS + 1);
    EXPECT_EQ(messages.back().timestamp,
              sigurdos::mesh::SD_MESSAGE_STORE_MAX_RECORDS + 1);
}

TEST_F(SdMessageStoreTest, DeduplicatesAndRecoversTornAppendAfterReboot)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(1, "same")));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(1, "same")));
    ASSERT_EQ(sigurdos::mesh::messageStoreCount(), 1);

    std::ofstream out(sigurdos::mesh::sdMessageStoreNativePath(),
                      std::ios::binary | std::ios::app);
    const uint8_t torn[7] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    out.write(reinterpret_cast<const char*>(torn), sizeof(torn));
    out.close();

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(false));
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    EXPECT_TRUE(sigurdos::mesh::messageStoreLastRecoveryResult() ==
                    sigurdos::mesh::MessageStoreRecoveryResult::Repaired ||
                sigurdos::mesh::messageStoreLastRecoveryResult() ==
                    sigurdos::mesh::MessageStoreRecoveryResult::Salvaged);
}

TEST_F(SdMessageStoreTest, PromotesValidatedReadyFileAfterPowerLoss)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(1, "durable")));
    const std::string live = sigurdos::mesh::sdMessageStoreNativePath();
    const std::string ready = live + ".ready";
    const auto bytes = readFile(live);
    ASSERT_FALSE(bytes.empty());
    {
        std::ofstream out(ready, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    ASSERT_EQ(std::remove(live.c_str()), 0);

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(false));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    StoredMessage restored{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&restored, 1), 1);
    EXPECT_STREQ(restored.text, "durable");
    EXPECT_FALSE(std::ifstream(ready).good());
}

TEST_F(SdMessageStoreTest, RejectsOversizedReadyBeforeAllocation)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(2, "live")));
    const std::string ready =
        std::string(sigurdos::mesh::sdMessageStoreNativePath()) +
        sigurdos::mesh::SD_MESSAGE_STORE_READY_SUFFIX;
    {
        std::ofstream out(ready, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out.seekp(static_cast<std::streamoff>(
            MAX_ENCODED_READY_BYTES));
        out.put('\0');
    }

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(false));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    EXPECT_FALSE(std::ifstream(ready).good());
}

TEST_F(SdMessageStoreTest, RemovesTruncatedReadyWithoutAbortingRecovery)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(3, "live")));
    const std::string ready =
        std::string(sigurdos::mesh::sdMessageStoreNativePath()) +
        sigurdos::mesh::SD_MESSAGE_STORE_READY_SUFFIX;
    {
        std::ofstream out(ready, std::ios::binary | std::ios::trunc);
        out.write("short", 5);
    }

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(false));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    EXPECT_FALSE(std::ifstream(ready).good());
}

TEST_F(SdMessageStoreTest, MalformedReadyDoesNotQuarantineValidLiveStore)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(4, "live")));
    const std::string ready =
        std::string(sigurdos::mesh::sdMessageStoreNativePath()) +
        sigurdos::mesh::SD_MESSAGE_STORE_READY_SUFFIX;
    {
        std::ofstream out(ready, std::ios::binary | std::ios::trunc);
        const uint8_t malformed[
            sigurdos::mesh::detail::MESSAGE_STORE_HEADER_SIZE] = {};
        out.write(reinterpret_cast<const char*>(malformed), sizeof(malformed));
    }

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(false));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    EXPECT_FALSE(std::ifstream(ready).good());
    EXPECT_FALSE(std::ifstream(
        std::string(sigurdos::mesh::sdMessageStoreNativePath()) +
        sigurdos::mesh::SD_MESSAGE_STORE_CORRUPT_SUFFIX).good());
}

TEST_F(SdMessageStoreTest, MissingSdFallsBackAndMarksDegraded)
{
    sigurdos::mesh::sdMessageStoreSetNativeMounted(false);
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreDegraded());
    EXPECT_EQ(sigurdos::mesh::sdMessageStoreCapacity(),
              sigurdos::mesh::MESSAGE_STORE_MAX_RECORDS);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(7, "fallback")));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
}

TEST_F(SdMessageStoreTest, MigratesSpiFlashHistoryWhenSdAppears)
{
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(10, "old-one")));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(11, "old-two")));

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreDegraded());
    ASSERT_EQ(sigurdos::mesh::messageStoreCount(), 2);

    StoredMessage messages[2]{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(messages, 2), 2);
    EXPECT_STREQ(messages[0].text, "old-one");
    EXPECT_STREQ(messages[1].text, "old-two");
    // Migration retires the source as an empty valid store after the SD
    // records have been committed and verified.
    sigurdos::mesh::detail::messageStoreSelectDefaultBackend();
    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 0);
    selectSd();
    EXPECT_FALSE(readFile(sigurdos::mesh::sdMessageStoreNativePath()).empty());
}

TEST_F(SdMessageStoreTest, RetiredSpiFlashHistoryIsNotResurrectedAfterSdClear)
{
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(30, "retire-me")));
    selectSd();
    ASSERT_EQ(sigurdos::mesh::messageStoreCount(), 1);

    // Model compaction/removal of the migrated destination followed by a
    // reboot/re-selection. The retired SPIFFS source must not replay the row.
    ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 0);
}

TEST_F(SdMessageStoreTest, FailedMigrationKeepsSourceForRetry)
{
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(40, "retry-me")));
    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(7);

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreDegraded());

    // The source remains available after the partial SD append. A retry with
    // normal writes completes migration and then retires it.
    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(-1);
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
    sigurdos::mesh::detail::messageStoreSelectDefaultBackend();
    ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 0);
}

TEST_F(SdMessageStoreTest, PartialRuntimeAppendFailsOverToSpiFlash)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(1)));
    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(7);
    // A mid-record SD write failure must NOT lose the message: the runtime
    // coordinator fails the append over to SPIFFS and reports degraded.
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(2)));
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreDegraded());
    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(-1);

    // Re-selection (production path: SPIFFS available) migrates the SPIFFS
    // copy back onto the healthy SD card; the torn SD tail is repaired and
    // both messages remain durable.
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 2);
    EXPECT_NE(sigurdos::mesh::messageStoreLastRecoveryResult(),
              sigurdos::mesh::MessageStoreRecoveryResult::Failed);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(3)));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 3);
}

TEST_F(SdMessageStoreTest, FactoryResetCompanionInterlockRunsBeforeHistoryDeletion)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(21, "before-reset")));

    const std::string keep_path = root + "/keep.txt";
    {
        std::ofstream keep(keep_path, std::ios::binary | std::ios::trunc);
        keep << "user file";
    }

    ResetPreflightRecorder preflight;
    preflight.live_path = sigurdos::mesh::sdMessageStoreNativePath();
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreReset(
        ResetPreflightRecorder::run, &preflight));
    EXPECT_TRUE(preflight.called);
    EXPECT_TRUE(preflight.backend_selected);
    EXPECT_TRUE(preflight.live_file_present);
    EXPECT_FALSE(fileExists(preflight.live_path));
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_FALSE(sigurdos::mesh::detail::messageStoreBackendSelected());
    EXPECT_TRUE(fileExists(keep_path));

    // Model the next boot: the same mounted card is selected again, but the
    // reset store is empty rather than replaying the old conversation.
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 0);
}

TEST_F(SdMessageStoreTest, FactoryResetRemovesAllRecoveryArtifacts)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(25, "artifacts")));
    const std::string live = sigurdos::mesh::sdMessageStoreNativePath();
    const std::string temp = live + sigurdos::mesh::SD_MESSAGE_STORE_TEMP_SUFFIX;
    const std::string ready = live + sigurdos::mesh::SD_MESSAGE_STORE_READY_SUFFIX;
    const std::string corrupt = live + sigurdos::mesh::SD_MESSAGE_STORE_CORRUPT_SUFFIX;
    {
        std::ofstream(temp, std::ios::binary) << "partial";
        std::ofstream(ready, std::ios::binary) << "candidate";
        std::ofstream(corrupt, std::ios::binary) << "quarantined";
    }

    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreReset());
    EXPECT_FALSE(fileExists(live));
    EXPECT_FALSE(fileExists(temp));
    EXPECT_FALSE(fileExists(ready));
    EXPECT_FALSE(fileExists(corrupt));
}

TEST_F(SdMessageStoreTest, FactoryResetWithoutActiveSdBackendIsSafeNoOp)
{
    sigurdos::mesh::sdMessageStoreSetNativeMounted(false);

    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreReset());
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_FALSE(sigurdos::mesh::detail::messageStoreBackendSelected());
}

TEST_F(SdMessageStoreTest, FactoryResetFailsClosedWhenSelectedSdIsUnmounted)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(22, "must-survive")));
    sigurdos::mesh::sdMessageStoreSetNativeMounted(false);

    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreReset());
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_FALSE(sigurdos::mesh::detail::messageStoreBackendSelected());

    // The reset did not claim success, so the store remains recoverable once
    // the card is available again.
    sigurdos::mesh::sdMessageStoreSetNativeMounted(true);
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
}

TEST_F(SdMessageStoreTest, FactoryResetReportsDeletionFailureAndKeepsHistory)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(23, "retry-me")));
    sigurdos::mesh::sdMessageStoreSetNativeRemoveFailure(true);

    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreReset());
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_FALSE(sigurdos::mesh::detail::messageStoreBackendSelected());

    sigurdos::mesh::sdMessageStoreSetNativeRemoveFailure(false);
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);
}

TEST_F(SdMessageStoreTest, FactoryResetCompanionInterlockFailurePreventsHistoryDeletion)
{
    selectSd();
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(makeMsg(24, "quiesce")));
    ResetPreflightRecorder preflight;
    preflight.live_path = sigurdos::mesh::sdMessageStoreNativePath();
    preflight.result = false;

    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreReset(
        ResetPreflightRecorder::run, &preflight));
    EXPECT_TRUE(preflight.called);
    EXPECT_TRUE(preflight.backend_selected);
    EXPECT_TRUE(preflight.live_file_present);
    EXPECT_TRUE(fileExists(preflight.live_path));
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_FALSE(sigurdos::mesh::detail::messageStoreBackendSelected());
}

} // namespace
