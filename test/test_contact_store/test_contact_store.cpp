#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "mesh/contact_store.h"
#include "hal/atomic_file.h"

namespace {

using sigurdos::mesh::SIGURDOS_CONTACT_NAME_LEN;
using sigurdos::mesh::SIGURDOS_CONTACT_PUBKEY_LEN;
using sigurdos::mesh::StoredContact;

StoredContact makeContact(uint8_t seed, const char* name, uint8_t type, uint8_t perm)
{
    StoredContact contact{};
    for (size_t i = 0; i < SIGURDOS_CONTACT_PUBKEY_LEN; i++) {
        contact.pub_key[i] = (uint8_t)(seed + i);
    }
    std::strncpy(contact.name, name, sizeof(contact.name) - 1);
    contact.type = type;
    contact.perm = perm;
    return contact;
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

bool fileExists(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

void writeBytes(const char* path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
}

void appendInt(std::vector<uint8_t>& bytes, int value)
{
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(value));
}

void appendRecord(std::vector<uint8_t>& bytes, const StoredContact& contact)
{
    bytes.insert(bytes.end(), contact.pub_key, contact.pub_key + SIGURDOS_CONTACT_PUBKEY_LEN);
    bytes.insert(bytes.end(), contact.name, contact.name + SIGURDOS_CONTACT_NAME_LEN);
    bytes.push_back(contact.type);
    bytes.push_back(contact.perm);
}

void appendVersionedHeader(std::vector<uint8_t>& bytes, int count, uint8_t version)
{
    bytes.insert(bytes.end(), sigurdos::mesh::detail::CONTACT_STORE_MAGIC,
                 sigurdos::mesh::detail::CONTACT_STORE_MAGIC +
                     sizeof(sigurdos::mesh::detail::CONTACT_STORE_MAGIC));
    bytes.push_back(version);
    appendInt(bytes, count);
}

class ContactStoreTest : public ::testing::Test {
protected:
    char path[128]{};

    void SetUp() override {
        std::snprintf(path, sizeof(path), "/tmp/sigurdos_contact_store_%d.bin",
                      ::testing::UnitTest::GetInstance()->random_seed());
        sigurdos::mesh::contactStoreSetNativePath(path);
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        std::remove(path);
        std::remove((std::string(path) + ".tmp").c_str());
        ASSERT_TRUE(sigurdos::mesh::contactStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::contactStoreClear());
    }

    void TearDown() override {
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        std::remove(path);
        std::remove((std::string(path) + ".tmp").c_str());
    }
};

TEST_F(ContactStoreTest, EmptyStoreRemovesExistingFile) {
    std::vector<uint8_t> junk{0xAA, 0xBB, 0xCC};
    writeBytes(path, junk);
    ASSERT_FALSE(readFile(path).empty());

    EXPECT_TRUE(sigurdos::mesh::contactStoreSaveAll(nullptr, 0));
    EXPECT_FALSE(fileExists(path));
}

TEST_F(ContactStoreTest, SaveWritesVersionedBytes) {
    StoredContact contacts[2] = {
        makeContact(0x10, "Alice", 2, 1),
        makeContact(0x40, "Bob", 3, 2),
    };

    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(contacts, 2));

    std::vector<uint8_t> expected;
    appendVersionedHeader(expected, 2, sigurdos::mesh::detail::CONTACT_STORE_VERSION);
    appendRecord(expected, contacts[0]);
    appendRecord(expected, contacts[1]);

    EXPECT_EQ(readFile(path), expected);
}

TEST_F(ContactStoreTest, LegacyFileLoadsUnchanged) {
    StoredContact contacts[2] = {
        makeContact(0x10, "Alice", 2, 1),
        makeContact(0x40, "Bob", 3, 2),
    };

    // Hand-written legacy file: bare count + records, no magic/version.
    std::vector<uint8_t> legacy;
    appendInt(legacy, 2);
    appendRecord(legacy, contacts[0]);
    appendRecord(legacy, contacts[1]);
    writeBytes(path, legacy);

    StoredContact out[2]{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 2), 2);
    EXPECT_EQ(std::memcmp(out[0].pub_key, contacts[0].pub_key, SIGURDOS_CONTACT_PUBKEY_LEN), 0);
    EXPECT_STREQ(out[0].name, "Alice");
    EXPECT_EQ(out[0].type, 2);
    EXPECT_EQ(out[0].perm, 1);
    EXPECT_STREQ(out[1].name, "Bob");
    EXPECT_EQ(out[1].type, 3);
    EXPECT_EQ(out[1].perm, 2);
}

TEST_F(ContactStoreTest, MagicParsesAsNegativeCountOnOldFirmware) {
    // Downgrade simulation: firmware older than the versioned format reads
    // the first 4 bytes of the file as the contact count. The magic was
    // chosen so that value is negative, tripping the legacy `n <= 0` check.
    // The converse collision — a legacy file whose count equals the magic —
    // is impossible for the same reason: legacy counts are always positive.
    StoredContact contact = makeContact(0x10, "Alice", 2, 1);
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(&contact, 1));

    std::vector<uint8_t> raw = readFile(path);
    ASSERT_GE(raw.size(), sizeof(int));
    int legacy_count = 0;
    std::memcpy(&legacy_count, raw.data(), sizeof(legacy_count));
    EXPECT_LT(legacy_count, 0);
}

TEST_F(ContactStoreTest, UnknownVersionRejected) {
    std::vector<uint8_t> raw;
    appendVersionedHeader(raw, 1, sigurdos::mesh::detail::CONTACT_STORE_VERSION + 1);
    appendRecord(raw, makeContact(0x10, "Alice", 2, 1));
    writeBytes(path, raw);

    StoredContact out[2]{};
    EXPECT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 2), 0);
}

TEST_F(ContactStoreTest, VersionedCountClampedToMaxContacts) {
    const int over = MAX_CONTACTS + 5;
    std::vector<uint8_t> raw;
    appendVersionedHeader(raw, over, sigurdos::mesh::detail::CONTACT_STORE_VERSION);
    for (int i = 0; i < over; i++) {
        appendRecord(raw, makeContact((uint8_t)i, "Contact", 1, 0));
    }
    writeBytes(path, raw);

    std::vector<StoredContact> out((size_t)over);
    EXPECT_EQ(sigurdos::mesh::contactStoreLoadAll(out.data(), over), MAX_CONTACTS);
}

TEST_F(ContactStoreTest, TruncatedVersionedFileKeepsCompleteRecords) {
    StoredContact first = makeContact(0x20, "First", 1, 1);
    StoredContact second = makeContact(0x60, "Second", 2, 2);

    std::vector<uint8_t> raw;
    appendVersionedHeader(raw, 2, sigurdos::mesh::detail::CONTACT_STORE_VERSION);
    appendRecord(raw, first);
    appendRecord(raw, second);
    raw.resize(raw.size() - 12);  // cut the second record short
    writeBytes(path, raw);

    StoredContact out[4]{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 4), 1);
    EXPECT_STREQ(out[0].name, "First");
    EXPECT_EQ(out[0].type, 1);
    EXPECT_EQ(out[0].perm, 1);
}

TEST_F(ContactStoreTest, RoundTripPreservesOrderAndTerminatesName) {
    StoredContact contacts[2] = {
        makeContact(0x01, "Alice", 7, 0),
        makeContact(0x80, "01234567890123456789012345678901", 8, 3),
    };
    std::memset(contacts[1].name, 'X', SIGURDOS_CONTACT_NAME_LEN);

    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(contacts, 2));

    StoredContact out[2]{};
    int loaded = sigurdos::mesh::contactStoreLoadAll(out, 2);
    ASSERT_EQ(loaded, 2);
    EXPECT_EQ(std::memcmp(out[0].pub_key, contacts[0].pub_key, SIGURDOS_CONTACT_PUBKEY_LEN), 0);
    EXPECT_STREQ(out[0].name, "Alice");
    EXPECT_EQ(out[0].type, 7);
    EXPECT_EQ(out[0].perm, 0);

    EXPECT_EQ(std::memcmp(out[1].pub_key, contacts[1].pub_key, SIGURDOS_CONTACT_PUBKEY_LEN), 0);
    EXPECT_EQ(out[1].name[SIGURDOS_CONTACT_NAME_LEN - 1], '\0');
    EXPECT_EQ(out[1].type, 8);
    EXPECT_EQ(out[1].perm, 3);
}

TEST_F(ContactStoreTest, TruncatedFileKeepsCompleteRecordsBeforeEof) {
    StoredContact first = makeContact(0x20, "First", 1, 1);
    StoredContact second = makeContact(0x60, "Second", 2, 2);

    std::vector<uint8_t> raw;
    appendInt(raw, 2);
    appendRecord(raw, first);
    appendRecord(raw, second);
    raw.resize(sizeof(int) + sigurdos::mesh::detail::CONTACT_STORE_RECORD_SIZE + 12);
    writeBytes(path, raw);

    StoredContact out[4]{};
    int loaded = sigurdos::mesh::contactStoreLoadAll(out, 4);
    ASSERT_EQ(loaded, 1);
    EXPECT_STREQ(out[0].name, "First");
    EXPECT_EQ(out[0].type, 1);
    EXPECT_EQ(out[0].perm, 1);
}

TEST_F(ContactStoreTest, NonPositiveCountRejected) {
    for (int count : {0, -3}) {
        std::vector<uint8_t> raw;
        appendInt(raw, count);
        writeBytes(path, raw);

        StoredContact out[2]{};
        EXPECT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 2), 0);
    }
}

TEST_F(ContactStoreTest, LoadAllStopsAtCallerCapacity) {
    StoredContact contacts[2] = {
        makeContact(0x30, "One", 1, 0),
        makeContact(0x50, "Two", 2, 1),
    };
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(contacts, 2));

    StoredContact out[1]{};
    EXPECT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 1), 1);
    EXPECT_STREQ(out[0].name, "One");
}

TEST_F(ContactStoreTest, WriteFailurePreservesLiveStore) {
    StoredContact old_contact = makeContact(0x10, "Old", 1, 0);
    StoredContact new_contact = makeContact(0x40, "New", 2, 1);
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(&old_contact, 1));

    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Write);
    EXPECT_FALSE(sigurdos::mesh::contactStoreSaveAll(&new_contact, 1));
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);

    StoredContact out{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.name, "Old");
}

TEST_F(ContactStoreTest, RenameFailureRecoversValidatedNewStore) {
    StoredContact old_contact = makeContact(0x10, "Old", 1, 0);
    StoredContact new_contact = makeContact(0x40, "New", 2, 1);
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(&old_contact, 1));

    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Rename);
    EXPECT_FALSE(sigurdos::mesh::contactStoreSaveAll(&new_contact, 1));
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);

    StoredContact out{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.name, "New");
}

TEST_F(ContactStoreTest, InvalidTempNeverReplacesLiveStore) {
    StoredContact old_contact = makeContact(0x10, "Old", 1, 0);
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(&old_contact, 1));
    const std::string temp_path = std::string(path) + ".tmp";
    writeBytes(temp_path.c_str(), {0x01, 0x02, 0x03});

    StoredContact out{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.name, "Old");
    EXPECT_FALSE(fileExists(temp_path.c_str()));
}

} // namespace
