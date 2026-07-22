#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "mesh/contact_store.h"
#include "mesh/contact_uri.h"
#include "hal/atomic_file.h"
#include "mesh/mesh_wrapper.h"

namespace {

using sigurdos::mesh::SIGURDOS_CONTACT_NAME_LEN;
using sigurdos::mesh::SIGURDOS_CONTACT_PUBKEY_LEN;
using sigurdos::mesh::StoredContact;

StoredContact makeContact(uint8_t seed, const char* name, uint8_t type, uint8_t flags)
{
    StoredContact contact{};
    for (size_t i = 0; i < SIGURDOS_CONTACT_PUBKEY_LEN; i++) {
        contact.pub_key[i] = (uint8_t)(seed + i);
    }
    std::strncpy(contact.name, name, sizeof(contact.name) - 1);
    contact.type = type;
    contact.flags = flags;
    contact.out_path_len = sigurdos::mesh::SIGURDOS_CONTACT_PATH_UNKNOWN;
    return contact;
}

StoredContact makeRichContact(uint8_t seed, const char* name)
{
    StoredContact contact = makeContact(seed, name, 3, 0xA7);
    contact.out_path_len = 5;
    for (size_t i = 0; i < sizeof(contact.out_path); ++i) {
        contact.out_path[i] = (uint8_t)(seed ^ i);
    }
    contact.last_advert_timestamp = 0x10203040U + seed;
    contact.lastmod = 0x20304050U + seed;
    contact.gps_lat = 51501234 + seed;
    contact.gps_lon = -1234567 - seed;
    contact.sync_since = 0x30405060U + seed;
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

void appendV1Record(std::vector<uint8_t>& bytes, const StoredContact& contact,
                    uint8_t legacy_perm)
{
    bytes.insert(bytes.end(), contact.pub_key, contact.pub_key + SIGURDOS_CONTACT_PUBKEY_LEN);
    bytes.insert(bytes.end(), contact.name, contact.name + SIGURDOS_CONTACT_NAME_LEN);
    bytes.push_back(contact.type);
    bytes.push_back(legacy_perm);
}

void appendRecord(std::vector<uint8_t>& bytes, const StoredContact& contact)
{
    uint8_t record[sigurdos::mesh::detail::CONTACT_STORE_RECORD_SIZE] = {};
    sigurdos::mesh::detail::writeContactRecord(contact, record, sizeof(record));
    bytes.insert(bytes.end(), record, record + sizeof(record));
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
        makeContact(0x10, "Alice", 2, 0),
        makeContact(0x40, "Bob", 3, 0),
    };

    // Hand-written legacy file: bare count + records, no magic/version.
    std::vector<uint8_t> legacy;
    appendInt(legacy, 2);
    appendV1Record(legacy, contacts[0], 1);
    appendV1Record(legacy, contacts[1], 2);
    writeBytes(path, legacy);

    StoredContact out[2]{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 2), 2);
    EXPECT_EQ(std::memcmp(out[0].pub_key, contacts[0].pub_key, SIGURDOS_CONTACT_PUBKEY_LEN), 0);
    EXPECT_STREQ(out[0].name, "Alice");
    EXPECT_EQ(out[0].type, 2);
    EXPECT_EQ(out[0].flags, 0x02);
    EXPECT_EQ(out[0].out_path_len, sigurdos::mesh::SIGURDOS_CONTACT_PATH_UNKNOWN);
    EXPECT_STREQ(out[1].name, "Bob");
    EXPECT_EQ(out[1].type, 3);
    EXPECT_EQ(out[1].flags, 0x04);
    EXPECT_EQ(out[1].lastmod, 0U);
}

TEST_F(ContactStoreTest, VersionOneFileMigratesWithSafeDefaults) {
    StoredContact contact = makeContact(0x10, "Alice", 2, 0);
    std::vector<uint8_t> raw;
    appendVersionedHeader(raw, 1, 1);
    appendV1Record(raw, contact, 3);
    writeBytes(path, raw);

    StoredContact out{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.name, "Alice");
    EXPECT_EQ(out.flags, 0x06);
    EXPECT_EQ(out.out_path_len, sigurdos::mesh::SIGURDOS_CONTACT_PATH_UNKNOWN);
    EXPECT_EQ(out.last_advert_timestamp, 0U);
    EXPECT_EQ(out.gps_lat, 0);
    EXPECT_EQ(out.sync_since, 0U);
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
    EXPECT_EQ(out[0].flags, 1);
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
    EXPECT_EQ(out[0].flags, 0);

    EXPECT_EQ(std::memcmp(out[1].pub_key, contacts[1].pub_key, SIGURDOS_CONTACT_PUBKEY_LEN), 0);
    EXPECT_EQ(out[1].name[SIGURDOS_CONTACT_NAME_LEN - 1], '\0');
    EXPECT_EQ(out[1].type, 8);
    EXPECT_EQ(out[1].flags, 3);
}

TEST_F(ContactStoreTest, VersionTwoRoundTripPreservesAllNonDerivedMetadata) {
    StoredContact contact = makeRichContact(0x21, "Rich");
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(&contact, 1));

    StoredContact out{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 1);
    EXPECT_EQ(std::memcmp(out.pub_key, contact.pub_key, sizeof(out.pub_key)), 0);
    EXPECT_STREQ(out.name, "Rich");
    EXPECT_EQ(out.type, contact.type);
    EXPECT_EQ(out.flags, 0xA7);
    EXPECT_EQ(out.out_path_len, 5);
    EXPECT_EQ(std::memcmp(out.out_path, contact.out_path, sizeof(out.out_path)), 0);
    EXPECT_EQ(out.last_advert_timestamp, contact.last_advert_timestamp);
    EXPECT_EQ(out.lastmod, contact.lastmod);
    EXPECT_EQ(out.gps_lat, contact.gps_lat);
    EXPECT_EQ(out.gps_lon, contact.gps_lon);
    EXPECT_EQ(out.sync_since, contact.sync_since);
}

TEST_F(ContactStoreTest, InvalidRouteLengthIsRejectedWithoutReplacingLive) {
    StoredContact live = makeRichContact(0x10, "Live");
    StoredContact invalid = makeRichContact(0x40, "Invalid");
    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(&live, 1));
    invalid.out_path_len = 0x61;  // 33 two-byte hashes exceed the 64-byte buffer
    EXPECT_FALSE(sigurdos::mesh::contactStoreSaveAll(&invalid, 1));

    StoredContact out{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 1);
    EXPECT_STREQ(out.name, "Live");
    EXPECT_EQ(out.flags, live.flags);
}

TEST_F(ContactStoreTest, InvalidRouteLengthInVersionTwoRecordStopsLoad) {
    StoredContact invalid = makeRichContact(0x40, "Invalid");
    invalid.out_path_len = 0xC1;  // hash-size mode 3 is reserved
    std::vector<uint8_t> raw;
    appendVersionedHeader(raw, 1, sigurdos::mesh::detail::CONTACT_STORE_VERSION);
    appendRecord(raw, invalid);
    writeBytes(path, raw);

    StoredContact out{};
    EXPECT_EQ(sigurdos::mesh::contactStoreLoadAll(&out, 1), 0);
}

TEST_F(ContactStoreTest, EncodedMultiByteRoutesRoundTrip) {
    StoredContact contacts[2] = {
        makeRichContact(0x20, "TwoByteHashes"),
        makeRichContact(0x60, "ThreeByteHashes"),
    };
    contacts[0].out_path_len = 0x60;  // 32 hashes * 2 bytes = 64 bytes
    contacts[1].out_path_len = 0x95;  // 21 hashes * 3 bytes = 63 bytes

    ASSERT_TRUE(sigurdos::mesh::contactStoreSaveAll(contacts, 2));
    StoredContact out[2]{};
    ASSERT_EQ(sigurdos::mesh::contactStoreLoadAll(out, 2), 2);
    EXPECT_EQ(out[0].out_path_len, 0x60);
    EXPECT_EQ(out[1].out_path_len, 0x95);
    EXPECT_EQ(std::memcmp(out[0].out_path, contacts[0].out_path,
                          sizeof(out[0].out_path)), 0);
    EXPECT_EQ(std::memcmp(out[1].out_path, contacts[1].out_path,
                          sizeof(out[1].out_path)), 0);
}

TEST_F(ContactStoreTest, TruncatedFileKeepsCompleteRecordsBeforeEof) {
    StoredContact first = makeContact(0x20, "First", 1, 1);
    StoredContact second = makeContact(0x60, "Second", 2, 2);

    std::vector<uint8_t> raw;
    appendInt(raw, 2);
    appendV1Record(raw, first, 1);
    appendV1Record(raw, second, 2);
    raw.resize(sizeof(int) + sigurdos::mesh::detail::CONTACT_STORE_V1_RECORD_SIZE + 12);
    writeBytes(path, raw);

    StoredContact out[4]{};
    int loaded = sigurdos::mesh::contactStoreLoadAll(out, 4);
    ASSERT_EQ(loaded, 1);
    EXPECT_STREQ(out[0].name, "First");
    EXPECT_EQ(out[0].type, 1);
    EXPECT_EQ(out[0].flags, 0x02);
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

TEST(ContactSaveDebounce, FlushesOnlyWhenDirtyAndDue) {
    using sigurdos::mesh::contacts_save_is_due;
    EXPECT_FALSE(contacts_save_is_due(false, 100, 1000000));
    EXPECT_FALSE(contacts_save_is_due(true, 100, 30099));
    EXPECT_TRUE(contacts_save_is_due(true, 100, 30100));
}

TEST(ContactSaveDebounce, HandlesMillisWrap) {
    EXPECT_TRUE(sigurdos::mesh::contacts_save_is_due(true, 0xFFFFFF00U,
                                                     0x00007600U));
}

TEST(ContactImportValidation, ParsesStrictQueryContact) {
    sigurdos::mesh::ContactUriFields fields{};
    ASSERT_TRUE(sigurdos::mesh::parseContactAddUri(
        "meshcore://contact/add?name=Alice+Smith&public_key="
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"
        "&type=2", fields));
    EXPECT_STREQ(fields.name, "Alice Smith");
    EXPECT_EQ(fields.type, 2);
}

TEST(ContactImportValidation, RejectsMalformedAndUnsupportedType) {
    sigurdos::mesh::ContactUriFields fields{};
    EXPECT_FALSE(sigurdos::mesh::parseContactAddUri(
        "meshcore://contact/add?name=Alice&public_key="
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"
        "&type=4", fields));
    EXPECT_FALSE(sigurdos::mesh::parseContactAddUri(
        "meshcore://contact/add?name=Alice%GG&public_key="
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
        fields));
}

TEST(ContactImportValidation, RejectsEncodedControl) {
    sigurdos::mesh::ContactUriFields fields{};
    ASSERT_TRUE(sigurdos::mesh::parseContactAddUri(
        "meshcore://contact/add?name=Alice%0AAdmin&public_key="
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
        fields));
    uint8_t key[SIGURDOS_CONTACT_PUBKEY_LEN]{};
    key[0] = 1;
    EXPECT_FALSE(sigurdos::mesh::contactCandidateValid(
        fields.name, key, fields.type));
}

TEST(ContactImportValidation, RejectsZeroKey) {
    uint8_t zero_key[SIGURDOS_CONTACT_PUBKEY_LEN]{};
    EXPECT_FALSE(sigurdos::mesh::contactCandidateValid(
        "Alice", zero_key, ADV_TYPE_CHAT));
}

TEST(ContactImportValidation, DetectsDuplicateNameOrFullKey) {
    uint8_t key_a[SIGURDOS_CONTACT_PUBKEY_LEN]{};
    uint8_t key_b[SIGURDOS_CONTACT_PUBKEY_LEN]{};
    key_a[0] = 1;
    key_b[0] = 2;
    EXPECT_TRUE(sigurdos::mesh::contactCandidateDuplicates(
        "Alice", key_a, "Alice", key_b));
    EXPECT_TRUE(sigurdos::mesh::contactCandidateDuplicates(
        "Alice", key_a, "Bob", key_a));
    EXPECT_FALSE(sigurdos::mesh::contactCandidateDuplicates(
        "Alice", key_a, "Bob", key_b));
}

TEST(ContactImportValidation, CanonicalCandidateSupportsEveryAdvertRole) {
    uint8_t key[SIGURDOS_CONTACT_PUBKEY_LEN]{};
    key[0] = 1;
    EXPECT_FALSE(sigurdos::mesh::contactCandidateValid(
        "node", key, sigurdos::mesh::MESHCORE_ADV_TYPE_NONE));
    for (uint8_t type = sigurdos::mesh::MESHCORE_ADV_TYPE_CHAT;
         type <= sigurdos::mesh::MESHCORE_ADV_TYPE_SENSOR; ++type) {
        EXPECT_TRUE(sigurdos::mesh::contactCandidateValid("node", key, type));
    }
    EXPECT_FALSE(sigurdos::mesh::contactCandidateValid(
        "node", key, sigurdos::mesh::MESHCORE_ADV_TYPE_SENSOR + 1));
}

TEST(ContactRevision, AdvancesPastPeerAndClockValues) {
    uint32_t revision = 0;
    EXPECT_TRUE(sigurdos::mesh::nextContactRevision(100, 200, revision));
    EXPECT_EQ(revision, 201U);
    EXPECT_TRUE(sigurdos::mesh::nextContactRevision(300, revision, revision));
    EXPECT_EQ(revision, 300U);
    EXPECT_FALSE(sigurdos::mesh::nextContactRevision(
        400, UINT32_MAX, revision));
}

} // namespace
