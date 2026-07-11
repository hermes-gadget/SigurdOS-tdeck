# Contact Store

**The contact persistence module — SPIFFS-based save/load of the contact list with a versioned binary file format and legacy fallback.**

Extracted from `mesh_wrapper.cpp` in PR #598, the `contact_store` module was later extended with a versioned binary file format in PR #603. It lives as a self-contained unit: a flat C-style API over callback-decoupled save and load paths, with no dependency on MeshCore `ContactInfo` types.

---

## Table of Contents

- [I. Overview](#i-overview)
- [II. File Format Specification](#ii-file-format-specification)
- [III. Versioning & Legacy Support](#iii-versioning--legacy-support)
- [IV. Public API](#iv-public-api)
- [V. SPIFFS Storage Path](#v-spiffs-storage-path)
- [VI. Testing](#vi-testing)
- [VII. Source Files](#vii-source-files)

---

## I. Overview

The contact store persists the mesh contact list across reboots. It is used by `mesh_wrapper.cpp` during shutdown (`saveContacts()`) and startup (`loadContacts()`).

### Design Principle: Callback-Decoupled Save/Load

The two core functions — `contactStoreSave` and `contactStoreLoad` — do **not** know about any MeshCore type. Instead they accept function pointers (callbacks) that the caller provides:

- `contactStoreSave(count, readFn, ctx)` iterates with `readFn(i, &contact, ctx)` to get each `StoredContact`.
- `contactStoreLoad(writeFn, ctx)` calls `writeFn(contact, ctx)` for each successfully deserialised contact.

This decoupling means the store never includes `MeshCore.h` and is testable in isolation.

```cpp
using ContactStoreReadFn  = bool (*)(int index, StoredContact* out, void* ctx);
using ContactStoreWriteFn = bool (*)(const StoredContact& contact, void* ctx);
```

Convenience wrappers `contactStoreSaveAll` and `contactStoreLoadAll` use internal anonymous-namespace helpers (`readFromArray` / `writeToArray`) to provide simple array-in/array-out access for clients that already have a flat array of `StoredContact`.

### Data Structures

```cpp
static constexpr size_t SIGURDOS_CONTACT_PUBKEY_LEN = 32;
static constexpr size_t SIGURDOS_CONTACT_NAME_LEN    = 32;
static constexpr size_t SIGURDOS_CONTACT_PATH_LEN    = 64;

struct StoredContact {
    uint8_t pub_key[SIGURDOS_CONTACT_PUBKEY_LEN];  // 32-byte X25519 public key
    char    name[SIGURDOS_CONTACT_NAME_LEN];         // 32-byte display name (null-terminated)
    uint8_t type;                                     // contact type (see ContactInfo::Type)
    uint8_t flags;                                    // complete MeshCore flags byte
    uint8_t out_path_len;                             // 0..64, or 0xFF when unknown
    uint8_t out_path[SIGURDOS_CONTACT_PATH_LEN];
    uint32_t last_advert_timestamp;
    uint32_t lastmod;
    int32_t gps_lat, gps_lon;                         // six decimal places
    uint32_t sync_since;
};
```

The derived ECDH shared secret and its validity flag are deliberately absent.
They depend on the local identity and are recomputed lazily after loading.

The maximum number of contacts (`MAX_CONTACTS`, defined in `BaseChatMesh.h`) is **32** for the T-Deck configuration.

---

## II. File Format Specification

The file stored on SPIFFS (or the native filesystem on host builds) has a **versioned binary format** introduced in PR #603.

### On-Disk Layout (Version 2)

```
Offset  Size  Field             Description
------  ----  ----------------- -------------------------------------------
  0      4    MAGIC             Bytes: 'S' (0x53), 'G' (0x47), 'C' (0x43), 0xB1
  4      1    VERSION           Format version byte (2)
  5      4    RECORD_COUNT      int32 little-endian — number of contacts

  9    151    RECORD[0]         First contact record
160    151    RECORD[1]         Second contact record
  ...         ...
         151 * N               Remaining records
```

### Record Layout (151 bytes per contact)

```
Offset  Size  Field         Description
------  ----  ------------- -------------------------------
  0     32    pub_key        X25519 public key (raw bytes)
 32     32    name           Display name (bytes, zero-padded)
 64      1    type           Contact type (uint8_t)
 65      1    flags          Complete MeshCore flags byte
 66      1    out_path_len   Route length 0..64, or 0xFF unknown
 67     64    out_path       Route bytes (fixed-width storage)
131      4    last_advert    Advert timestamp by the contact's clock
135      4    lastmod        Modification timestamp by the local clock
139      4    gps_lat        Signed latitude, six decimal places
143      4    gps_lon        Signed longitude, six decimal places
147      4    sync_since     Message synchronization cursor
        --                   ------------------------------
        151    TOTAL
```

### Constants

| Symbol | Value | Notes |
|--------|-------|-------|
| `CONTACT_STORE_MAGIC` | `{'S', 'G', 'C', 0xB1}` | 4-byte magic — when read as a little-endian `int32` = `0xB1434753` |
| `CONTACT_STORE_VERSION` | 2 | Current file format version |
| `CONTACT_STORE_V1_RECORD_SIZE` | 66 | Legacy and version-1 record width |
| `CONTACT_STORE_RECORD_SIZE` | 151 | Current version-2 record width |
| `SIGURDOS_CONTACT_PUBKEY_LEN` | 32 | Public key field width |
| `SIGURDOS_CONTACT_NAME_LEN` | 32 | Name field width |

### Binary Layout of a Complete File (Version 2, 2 contacts)

```
53 47 43 B1 02 02 00 00 00  [magic, ver=2, count=2]
<151-byte record>            [contact 0]
<151-byte record>            [contact 1]
```

Total overhead is 9 bytes. Each v2 contact record is fixed at 151 bytes, so a
32-contact store occupies 4,841 bytes.

---

## III. Versioning & Legacy Support

### Legacy and Version-1 Detection

Before PR #603, the contact store wrote a bare `int32 count` followed by
66-byte records. Version 1 added the magic/version header but retained that
record shape. The current loader handles all three shapes:

1. Read the first 4 bytes into `head[]`.
2. If `head` matches `CONTACT_STORE_MAGIC` → **versioned format**:
   - Read VERSION byte.
   - Reject if `version == 0` or `version > CONTACT_STORE_VERSION`.
   - Read `int32 count`.
   - Clamp `count` to `MAX_CONTACTS`.
   - Use 66-byte records for version 1 or 151-byte records for version 2.
3. Else → **bare legacy format**:
   - Interpret `head` directly as a bare `int32 count`.
   - Read version-1 66-byte records.

Fields absent from legacy/version-1 records receive safe defaults: permission
bits are expanded into the flags byte, route length becomes `0xFF` (unknown),
and timestamps, GPS coordinates, path bytes, and sync cursor are zero. The next
successful save migrates the data to version 2.

### Downgrade Safety: Magic as Negative int32

The magic `{'S','G','C',0xB1}` was deliberately chosen so that reading the 4 bytes as a little-endian `int32` yields **`0xB1434753`**, which is a **negative number** (`−1,317,418,157` in decimal).

When firmware older than PR #603 loads a versioned file, it reads those 4 bytes as a contact count and sees a negative number. Legacy firmware has an existing `count <= 0` rejection path, so it treats the file as empty/erased — a **safe downgrade failure** rather than attempting to parse garbage records.

The converse collision (a legacy file whose count matches the magic) is impossible: legacy counts are always positive (1..32), and the magic is always negative when interpreted as an `int32`.

```
Magic bytes        →  53 47 43 B1
As little-endian
int32              →  0xB1434753
As signed int32    →  −1317418157   ← negative → legacy `n <= 0` check rejects it
```

### Version Rejection

When loading a versioned file, the VERSION byte is checked against `CONTACT_STORE_VERSION`. Any version **greater than** the current known version is rejected (`contactStoreLoad` returns 0). This prevents future-format data from being parsed incorrectly.

### Field Termination

During deserialisation, `readContactRecord` zeroes the whole `StoredContact` struct with `memset` and explicitly null-terminates the `name` field at index 31, guaranteeing that callers always see a valid C string even if the on-disk name fills all 32 bytes with no null terminator.

---

## IV. Public API

All symbols live in `sigurdos::mesh::`.

### Core Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `contactStoreBegin` | `bool ()` | Initialise the underlying filesystem (SPIFFS `begin`). Call once at boot. Returns false if the FS cannot be mounted. |
| `contactStoreClear` | `bool ()` | Delete the contact store file. Returns false if the FS is unavailable or the file cannot be removed. |
| `contactStoreSave` | `bool (int count, ContactStoreReadFn read, void* ctx)` | Save `count` contacts via callback. If `count ≤ 0` the file is deleted (empty store). Writes versioned header, then iterates `read` to write records. |
| `contactStoreLoad` | `int (ContactStoreWriteFn write, void* ctx)` | Load contacts from file, delivering each via `write` callback. Returns the number of contacts loaded (0 on failure or empty store). Auto-detects legacy vs versioned format. |

### Convenience Wrappers

| Function | Signature | Description |
|----------|-----------|-------------|
| `contactStoreSaveAll` | `bool (const StoredContact* contacts, int count)` | Save an array of contacts (one-shot). If `count ≤ 0` the file is deleted. |
| `contactStoreLoadAll` | `int (StoredContact* out, int max)` | Load up to `max` contacts into a caller-provided array. Returns the count actually loaded. |
| `contactStoreSetNativePath` | `void (const char* path)` | **Host-only** (`!ESP32_PLATFORM`). Override the file path used by the native-side store (default: `/tmp/sigurdos_contacts.bin`). |

### Internal Helpers (`detail` namespace)

| Function | Signature | Description |
|----------|-----------|-------------|
| `writeContactRecord` | `void (const StoredContact&, uint8_t* rec, size_t len)` | Serialise one contact into a 151-byte v2 record buffer. |
| `readContactRecord` | `bool (StoredContact&, const uint8_t* rec, size_t len)` | Deserialise a v2 record and reject invalid route lengths. |
| `readContactRecordV1` | `bool (StoredContact&, const uint8_t* rec, size_t len)` | Deserialise a 66-byte legacy/v1 record with safe defaults. |

### Callback Type Aliases

```cpp
using ContactStoreReadFn  = bool (*)(int index, StoredContact* out, void* ctx);
using ContactStoreWriteFn = bool (*)(const StoredContact& contact, void* ctx);
```

---

## V. SPIFFS Storage Path

| Platform | Path | Details |
|----------|------|---------|
| **ESP32** (SPIFFS) | `/contacts` | Constant `STORE_PATH` in the anonymous namespace. Opened via `SPIFFS.open("/contacts", "r"/"w")`. |
| **Host** (native test) | `/tmp/sigurdos_contacts.bin` | Default path in `g_native_path`. Overridable at runtime via `contactStoreSetNativePath()`. |

The host path is mutable so that unit tests can isolate each test case to a unique temporary file (see test setup below).

---

## VI. Testing

Tests live in `test/test_contact_store/test_contact_store.cpp` (native-only, no hardware required). They use Google Test (`gtest`) and run with `pio test -e native_test -f test_contact_store`.

### Test Cases

| Test | What It Verifies |
|------|-----------------|
| `EmptyStoreRemovesExistingFile` | Saving 0 contacts deletes any existing file on disk. |
| `SaveWritesVersionedBytes` | `contactStoreSaveAll` produces exact v2 golden bytes. |
| `LegacyFileLoadsUnchanged` | A hand-written bare legacy file loads and expands permission bits safely. |
| `VersionOneFileMigratesWithSafeDefaults` | A version-1 file loads with unknown route and zero metadata. |
| `MagicParsesAsNegativeCountOnOldFirmware` | Versioned file's first 4 bytes, when interpreted as `int32` by legacy code, produce a negative value. |
| `UnknownVersionRejected` | A file with `VERSION = CONTACT_STORE_VERSION + 1` is rejected (load returns 0). |
| `VersionedCountClampedToMaxContacts` | If the file's record count exceeds `MAX_CONTACTS`, it is clamped to `MAX_CONTACTS` during load. |
| `TruncatedVersionedFileKeepsCompleteRecords` | Read stops at first incomplete record — only fully-decodable records are returned. |
| `RoundTripPreservesOrderAndTerminatesName` | Save then load preserves order/basic fields and terminates names. |
| `VersionTwoRoundTripPreservesAllNonDerivedMetadata` | Flags, route, timestamps, GPS, and sync cursor round-trip. |
| `InvalidRouteLength*` | Invalid stored or callback-provided route lengths are rejected safely. |
| `TruncatedFileKeepsCompleteRecordsBeforeEof` | Same as the versioned truncation test, but for legacy-format files. |
| `NonPositiveCountRejected` | Files containing count 0 or -3 are ignored (returns 0 contacts). |
| `LoadAllStopsAtCallerCapacity` | `contactStoreLoadAll` returns at most `max` contacts even if the file has more. |

### Test Infrastructure

Each test creates an isolated environment:

```cpp
void SetUp() override {
    std::snprintf(path, sizeof(path), "/tmp/sigurdos_contact_store_%d.bin",
                  ::testing::UnitTest::GetInstance()->random_seed());
    sigurdos::mesh::contactStoreSetNativePath(path);
    std::remove(path);
    ASSERT_TRUE(sigurdos::mesh::contactStoreBegin());
    ASSERT_TRUE(sigurdos::mesh::contactStoreClear());
}
```

Helper functions (`appendInt`, `appendRecord`, `appendVersionedHeader`, `writeBytes`, `readFile`, `fileExists`) construct raw binary payloads to simulate files on disk, allowing precise control over format variations and corruption scenarios.

---

## VII. Source Files

| File | Purpose |
|------|---------|
| `src/mesh/contact_store.h` | Public header — `StoredContact`, callback typedefs, function declarations, compile-time constants. |
| `src/mesh/contact_store.cpp` | Implementation — SPIFFS/host file I/O, binary serialisation, versioned header, legacy fallback, convenience wrappers. |
| `test/test_contact_store/test_contact_store.cpp` | Unit tests — golden bytes, legacy compat, version rejection, truncation, clamping, round-trip. |
| `src/mesh/mesh_wrapper.cpp` | Consumer — `saveContacts()` / `loadContacts()` call the store API at shutdown and boot. |
| `lib/meshcore/src/helpers/BaseChatMesh.h` | Provider of `MAX_CONTACTS` (32) and MeshCore `ContactInfo` that `mesh_wrapper` converts to/from `StoredContact`. |

---

## References

- PR #598 — `contact_store` module extraction from `mesh_wrapper`.
- PR #603 — Versioned binary file format with magic, version byte, legacy detection, and downgrade safety.
- [`MESH_NETWORKING.md`](MESH_NETWORKING.md) — Broader mesh layer architecture, including contact discovery and the contact list LRU.
