// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "hal/atomic_file.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace sigurdos::storage;

static constexpr uint32_t MAGIC = 0x53414631;
static constexpr const char* LIVE_PATH = "/tmp/sigurdos_atomic_file_test.bin";

struct Payload {
    const char* text;
    bool fail_writer;
};

bool writePayload(AtomicFileWriter& writer, void* raw)
{
    auto* payload = static_cast<Payload*>(raw);
    if (!payload || !payload->text || payload->fail_writer) return false;
    const uint16_t length = (uint16_t)std::strlen(payload->text);
    return writer.write(&MAGIC, sizeof(MAGIC)) == sizeof(MAGIC) &&
           writer.write(&length, sizeof(length)) == sizeof(length) &&
           writer.write(payload->text, length) == length;
}

bool validatePayload(AtomicFileReader& reader, void*)
{
    uint32_t magic = 0;
    uint16_t length = 0;
    if (reader.size() < sizeof(magic) + sizeof(length)) return false;
    if (!reader.seek(0) || reader.read(&magic, sizeof(magic)) != sizeof(magic) ||
        reader.read(&length, sizeof(length)) != sizeof(length)) {
        return false;
    }
    return magic == MAGIC &&
           reader.size() == sizeof(magic) + sizeof(length) + length;
}

std::string readPayload(const char* path)
{
    FILE* file = std::fopen(path, "rb");
    if (!file) return {};
    uint32_t magic = 0;
    uint16_t length = 0;
    std::fread(&magic, 1, sizeof(magic), file);
    std::fread(&length, 1, sizeof(length), file);
    std::string result(length, '\0');
    if (length > 0) std::fread(&result[0], 1, length, file);
    std::fclose(file);
    return magic == MAGIC ? result : std::string();
}

void writeRaw(const char* path, const char* value)
{
    FILE* file = std::fopen(path, "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(value, 1, std::strlen(value), file), std::strlen(value));
    std::fclose(file);
}

class AtomicFileTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        atomicFileSetNativeFault(AtomicFileNativeFault::None);
        std::remove(LIVE_PATH);
        std::remove("/tmp/sigurdos_atomic_file_test.bin.tmp");
    }

    void TearDown() override
    {
        atomicFileSetNativeFault(AtomicFileNativeFault::None);
        std::remove(LIVE_PATH);
        std::remove("/tmp/sigurdos_atomic_file_test.bin.tmp");
    }

    void save(const char* value)
    {
        Payload payload{value, false};
        ASSERT_TRUE(atomicFileReplace(LIVE_PATH, writePayload, &payload,
                                      validatePayload, nullptr));
    }
};

TEST_F(AtomicFileTest, ReplacesLiveOnlyAfterValidatedWrite)
{
    save("old");
    Payload replacement{"new", false};
    ASSERT_TRUE(atomicFileReplace(LIVE_PATH, writePayload, &replacement,
                                  validatePayload, nullptr));
    EXPECT_EQ(readPayload(LIVE_PATH), "new");
}

TEST_F(AtomicFileTest, WriterFailurePreservesLiveFile)
{
    save("old");
    Payload replacement{"new", true};
    EXPECT_FALSE(atomicFileReplace(LIVE_PATH, writePayload, &replacement,
                                   validatePayload, nullptr));
    EXPECT_EQ(readPayload(LIVE_PATH), "old");
}

TEST_F(AtomicFileTest, OpenWriteAndCloseFaultsPreserveLiveFile)
{
    save("old");
    Payload replacement{"new", false};
    for (AtomicFileNativeFault fault : {AtomicFileNativeFault::TempOpen,
                                        AtomicFileNativeFault::Write,
                                        AtomicFileNativeFault::Close}) {
        atomicFileSetNativeFault(fault);
        EXPECT_FALSE(atomicFileReplace(LIVE_PATH, writePayload, &replacement,
                                       validatePayload, nullptr));
        EXPECT_EQ(readPayload(LIVE_PATH), "old");
    }
}

TEST_F(AtomicFileTest, ValidationFailureDiscardsTempAndPreservesLive)
{
    save("old");
    Payload replacement{"new", false};
    atomicFileSetNativeFault(AtomicFileNativeFault::Validate);
    EXPECT_FALSE(atomicFileReplace(LIVE_PATH, writePayload, &replacement,
                                   validatePayload, nullptr));
    EXPECT_EQ(readPayload(LIVE_PATH), "old");
    EXPECT_EQ(std::fopen("/tmp/sigurdos_atomic_file_test.bin.tmp", "rb"), nullptr);
}

TEST_F(AtomicFileTest, RenameFailureLeavesValidatedTempForRecovery)
{
    save("old");
    Payload replacement{"new", false};
    atomicFileSetNativeFault(AtomicFileNativeFault::Rename);
    EXPECT_FALSE(atomicFileReplace(LIVE_PATH, writePayload, &replacement,
                                   validatePayload, nullptr));
    // The live file was cleared for the SPIFFS rename, so only the validated
    // temp survives the interrupted commit — recovery must finish it.
    EXPECT_EQ(readPayload("/tmp/sigurdos_atomic_file_test.bin.tmp"), "new");

    atomicFileSetNativeFault(AtomicFileNativeFault::None);
    EXPECT_TRUE(atomicFileRecover(LIVE_PATH, validatePayload, nullptr));
    EXPECT_EQ(readPayload(LIVE_PATH), "new");
    EXPECT_EQ(std::fopen("/tmp/sigurdos_atomic_file_test.bin.tmp", "rb"), nullptr);
}

TEST_F(AtomicFileTest, RecoverPromotesValidTempOverExistingLive)
{
    // Crash after the temp validated but before the live file was removed:
    // both files exist. SPIFFS cannot rename over the live name (#837), so
    // recovery must clear it before promoting the temp.
    save("old");
    Payload staged{"new", false};
    ASSERT_TRUE(atomicFileReplace("/tmp/sigurdos_atomic_file_test.bin.tmp",
                                  writePayload, &staged,
                                  validatePayload, nullptr));

    EXPECT_TRUE(atomicFileRecover(LIVE_PATH, validatePayload, nullptr));
    EXPECT_EQ(readPayload(LIVE_PATH), "new");
    EXPECT_EQ(std::fopen("/tmp/sigurdos_atomic_file_test.bin.tmp", "rb"), nullptr);
}

TEST_F(AtomicFileTest, InvalidTempNeverReplacesValidLive)
{
    save("old");
    writeRaw("/tmp/sigurdos_atomic_file_test.bin.tmp", "partial");
    EXPECT_TRUE(atomicFileRecover(LIVE_PATH, validatePayload, nullptr));
    EXPECT_EQ(readPayload(LIVE_PATH), "old");
    EXPECT_EQ(std::fopen("/tmp/sigurdos_atomic_file_test.bin.tmp", "rb"), nullptr);
}

TEST_F(AtomicFileTest, TempPathRejectsTruncation)
{
    char out[8] = {};
    EXPECT_FALSE(atomicFileTempPath(LIVE_PATH, out, sizeof(out)));
    EXPECT_FALSE(atomicFileTempPath(nullptr, out, sizeof(out)));
}

}  // namespace
