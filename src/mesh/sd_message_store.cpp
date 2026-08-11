// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "sd_message_store.h"

#include "message_store.h"
#include "hal/sdcard.h"
#include "hal/spi_shared.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#if !defined(ESP32_PLATFORM)
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace sigurdos::mesh {

namespace detail {
bool messageStoreBackendDegraded();
}

namespace {

static bool g_using_sd = false;
static bool g_degraded = false;
// Recovery accepts at most one spare record beyond the steady-state capacity
// so a torn append can be salvaged without allocating from an untrusted file
// length. This is the maximum encoded .msgs/.msgs.ready byte count.
static constexpr uint64_t SD_MESSAGE_STORE_MAX_ENCODED_BYTES =
    static_cast<uint64_t>(detail::MESSAGE_STORE_HEADER_SIZE) +
    static_cast<uint64_t>(SD_MESSAGE_STORE_MAX_RECORDS + 1) *
        static_cast<uint64_t>(detail::MESSAGE_STORE_RECORD_SIZE);

static bool encodedStoreSizeAllowed(uint64_t size)
{
    return size != 0 &&
           size <= static_cast<uint64_t>(SIZE_MAX) &&
           size <= SD_MESSAGE_STORE_MAX_ENCODED_BYTES;
}

#if !defined(ESP32_PLATFORM)
static char g_native_root[160] = "/tmp/sigurdos_sd_store";
static char g_native_path[192] = "/tmp/sigurdos_sd_store/msgs";
static bool g_native_mounted = false;
static int g_native_append_write_limit = -1;
static bool g_native_remove_failure = false;

static void updateNativePath()
{
    std::snprintf(g_native_path, sizeof(g_native_path), "%s/msgs", g_native_root);
}

static bool nativeDirectory(const char* path)
{
    struct stat info {};
    return path && ::stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool nativeEnsure()
{
    if (!g_native_mounted) return false;
    if (::mkdir(g_native_root, 0777) != 0 && errno != EEXIST) return false;
    return nativeDirectory(g_native_root);
}

static bool nativeExists(const char* path)
{
    struct stat info {};
    return path && ::stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static size_t nativeSize(const char* path)
{
    struct stat info {};
    if (!path || ::stat(path, &info) != 0 || info.st_size < 0) return 0;
    return static_cast<size_t>(info.st_size);
}

static bool nativeReadAt(const char* path, size_t offset,
                         uint8_t* data, size_t len)
{
    if (!path || (len > 0 && !data) || offset > static_cast<size_t>(LONG_MAX)) {
        return false;
    }
    if (len == 0) return true;
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    const bool ok = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
        std::fread(data, 1, len, file) == len;
    std::fclose(file);
    return ok;
}

static bool nativeWriteAt(const char* path, size_t offset,
                          const uint8_t* data, size_t len)
{
    if (!path || (len > 0 && !data) || offset > static_cast<size_t>(LONG_MAX) ||
        !nativeEnsure()) return false;
    if (len == 0) return true;
    FILE* file = std::fopen(path, "r+b");
    if (!file) file = std::fopen(path, "w+b");
    if (!file) return false;
    bool ok = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
        std::fwrite(data, 1, len, file) == len;
    if (ok) ok = std::fflush(file) == 0;
    if (std::fclose(file) != 0) ok = false;
    return ok;
}

static bool nativeAppend(const char* path, const uint8_t* data, size_t len)
{
    if (!path || (len > 0 && !data) || !nativeEnsure()) return false;
    if (len == 0) return true;
    FILE* file = std::fopen(path, "ab");
    if (!file) return false;
    size_t write_len = len;
    if (g_native_append_write_limit >= 0 &&
        static_cast<size_t>(g_native_append_write_limit) < write_len) {
        write_len = static_cast<size_t>(g_native_append_write_limit);
    }
    bool ok = std::fwrite(data, 1, write_len, file) == len;
    if (ok) ok = std::fflush(file) == 0;
    if (std::fclose(file) != 0) ok = false;
    return ok;
}

static bool nativeRemove(const char* path)
{
    if (g_native_remove_failure) return false;
    return path && (std::remove(path) == 0 || errno == ENOENT);
}

static bool nativeRename(const char* from, const char* to)
{
    return from && to && std::rename(from, to) == 0;
}
#endif

static bool appendPath(const char* path, const char* suffix,
                       char* out, size_t out_size)
{
    if (!path || !suffix || !out || out_size == 0) return false;
    const int written = std::snprintf(out, out_size, "%s%s", path, suffix);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static bool validateStoreBytes(const uint8_t* data, size_t len, void*)
{
    if (!data || len < detail::MESSAGE_STORE_HEADER_SIZE) return false;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    uint32_t next_id = 0;
    std::memcpy(&magic, data, sizeof(magic));
    std::memcpy(&version, data + 4, sizeof(version));
    std::memcpy(&count, data + 5, sizeof(count));
    std::memcpy(&next_id, data + 9, sizeof(next_id));
    if (magic != detail::MESSAGE_STORE_MAGIC ||
        version != detail::MESSAGE_STORE_VERSION || next_id == 0 ||
        count > SD_MESSAGE_STORE_MAX_RECORDS + 1 ||
        len != detail::MESSAGE_STORE_HEADER_SIZE +
            static_cast<size_t>(count) * detail::MESSAGE_STORE_RECORD_SIZE) {
        return false;
    }

    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t store_id = 0;
        const size_t offset = detail::MESSAGE_STORE_HEADER_SIZE +
            static_cast<size_t>(i) * detail::MESSAGE_STORE_RECORD_SIZE;
        std::memcpy(&store_id, data + offset, sizeof(store_id));
        if (store_id == 0 || store_id == next_id) return false;
        ids.push_back(store_id);
    }
    std::sort(ids.begin(), ids.end());
    for (size_t i = 1; i < ids.size(); ++i) {
        if (ids[i - 1] == ids[i]) return false;
    }
    return true;
}

#if !defined(ESP32_PLATFORM)
static bool nativeReadWhole(const char* path, std::vector<uint8_t>& data)
{
    const size_t size = nativeSize(path);
    if (!nativeExists(path) || !encodedStoreSizeAllowed(size)) return false;
    try {
        data.resize(size);
    } catch (const std::bad_alloc&) {
        data.clear();
        return false;
    }
    return nativeReadAt(path, 0, data.data(), data.size());
}

static bool nativeRecoverPending(const char* path,
                                 detail::MessageStoreValidateBytesFn validate,
                                 void* validate_ctx)
{
    char temp_path[256];
    char ready_path[256];
    if (!appendPath(path, SD_MESSAGE_STORE_TEMP_SUFFIX,
                    temp_path, sizeof(temp_path)) ||
        !appendPath(path, SD_MESSAGE_STORE_READY_SUFFIX,
                    ready_path, sizeof(ready_path))) return false;

    if (nativeExists(ready_path)) {
        std::vector<uint8_t> ready;
        const bool valid = nativeReadWhole(ready_path, ready) &&
            validate && validate(ready.data(), ready.size(), validate_ctx);
        if (!valid) {
            if (!nativeRemove(ready_path)) return false;
            if (!nativeExists(path)) return false;
        } else {
            if (nativeExists(path) && !nativeRemove(path)) return false;
            if (!nativeRename(ready_path, path)) return false;
        }
    }
    return !nativeExists(temp_path) || nativeRemove(temp_path);
}

static bool nativeReplace(const char* path, const uint8_t* data, size_t len)
{
    if (!path || (len > 0 && !data) || !nativeEnsure() ||
        !nativeRecoverPending(path, validateStoreBytes, nullptr)) return false;
    char temp_path[256];
    char ready_path[256];
    if (!appendPath(path, SD_MESSAGE_STORE_TEMP_SUFFIX,
                    temp_path, sizeof(temp_path)) ||
        !appendPath(path, SD_MESSAGE_STORE_READY_SUFFIX,
                    ready_path, sizeof(ready_path))) return false;
    if (!nativeRemove(temp_path) || !nativeRemove(ready_path)) return false;

    FILE* file = std::fopen(temp_path, "wb");
    if (!file) return false;
    bool ok = len == 0 || std::fwrite(data, 1, len, file) == len;
    if (ok) ok = std::fflush(file) == 0 && ::fsync(::fileno(file)) == 0;
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        nativeRemove(temp_path);
        return false;
    }

    std::vector<uint8_t> written;
    if (!nativeReadWhole(temp_path, written) ||
        !validateStoreBytes(written.data(), written.size(), nullptr) ||
        !nativeRename(temp_path, ready_path)) {
        nativeRemove(temp_path);
        return false;
    }
    if (nativeExists(path) && !nativeRemove(path)) return false;
    return nativeRename(ready_path, path);
}
#endif

static const char* backendPath(void*)
{
#if defined(ESP32_PLATFORM)
    return SD_MESSAGE_STORE_PATH;
#else
    return g_native_path;
#endif
}

static bool backendEnsure(void*)
{
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_mounted();
#else
    return nativeEnsure();
#endif
}

static bool backendExists(void*, const char* path)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_exists(path);
#else
    return nativeExists(path);
#endif
}

static size_t backendSize(void*, const char* path)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return 0;
#endif
#if defined(ESP32_PLATFORM)
    return static_cast<size_t>(sigurdos_sdcard_file_size(path));
#else
    return nativeSize(path);
#endif
}

static bool backendReadAt(void*, const char* path, size_t offset,
                          uint8_t* data, size_t len)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_read_at(path, offset, data, len);
#else
    return nativeReadAt(path, offset, data, len);
#endif
}

static bool backendWriteAt(void*, const char* path, size_t offset,
                           const uint8_t* data, size_t len)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_write_at(path, offset, data, len);
#else
    return nativeWriteAt(path, offset, data, len);
#endif
}

static bool backendAppend(void*, const char* path, const uint8_t* data, size_t len)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_append(path, data, len);
#else
    return nativeAppend(path, data, len);
#endif
}

static bool backendRecover(void*, const char* path,
                           detail::MessageStoreValidateBytesFn validate,
                           void* validate_ctx);

static bool backendReplace(void*, const char* path, const uint8_t* data, size_t len)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
    if (!validateStoreBytes(data, len, nullptr)) return false;
#if defined(ESP32_PLATFORM)
    if (!backendRecover(nullptr, path, validateStoreBytes, nullptr)) return false;
    return sigurdos_sdcard_write(path, data, len);
#else
    return nativeReplace(path, data, len);
#endif
}

static bool backendRecover(void*, const char* path,
                           detail::MessageStoreValidateBytesFn validate,
                           void* validate_ctx)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    if (!sigurdos_sdcard_mounted() || !path || !validate) return false;
    char temp_path[256];
    char ready_path[256];
    if (!appendPath(path, SD_MESSAGE_STORE_TEMP_SUFFIX,
                    temp_path, sizeof(temp_path)) ||
        !appendPath(path, SD_MESSAGE_STORE_READY_SUFFIX,
                    ready_path, sizeof(ready_path))) return false;

    if (sigurdos_sdcard_exists(ready_path)) {
        const uint64_t ready_size = sigurdos_sdcard_file_size(ready_path);
        std::unique_ptr<uint8_t[]> ready;
        bool valid = false;
        if (encodedStoreSizeAllowed(ready_size)) {
            const size_t bounded_size = static_cast<size_t>(ready_size);
            ready.reset(new (std::nothrow) uint8_t[bounded_size]);
            valid = ready &&
                sigurdos_sdcard_read_at(ready_path, 0, ready.get(), bounded_size) &&
                validate(ready.get(), bounded_size, validate_ctx);
        }
        if (!valid) {
            if (!sigurdos_sdcard_remove_path(ready_path)) return false;
            if (!sigurdos_sdcard_exists(path)) return false;
        } else {
            if (!sigurdos_sdcard_remove_path(path)) return false;
            if (!sigurdos_sdcard_rename_path(ready_path, path)) return false;
        }
    }
    if (sigurdos_sdcard_exists(temp_path) &&
        !sigurdos_sdcard_remove_path(temp_path)) return false;
    return true;
#else
    return path && nativeRecoverPending(path, validate, validate_ctx);
#endif
}

static bool backendRemove(void*, const char* path)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_remove_path(path);
#else
    return nativeRemove(path);
#endif
}

static bool backendRename(void*, const char* from, const char* to)
{
#if defined(ESP32_PLATFORM)
    SigurdosSharedSpiGuard guard(
        SigurdosSharedSpiDevice::SdCard,
        SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS);
    if (!guard) return false;
#endif
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_rename_path(from, to);
#else
    return nativeRename(from, to);
#endif
}

static const detail::MessageStoreBackend SD_MESSAGE_STORE_BACKEND = {
    nullptr,
    backendPath,
    SD_MESSAGE_STORE_MAX_RECORDS,
    SD_MESSAGE_STORE_COMPACT_TO_RECORDS,
    backendEnsure,
    backendExists,
    backendSize,
    backendReadAt,
    backendWriteAt,
    backendAppend,
    backendReplace,
    backendRecover,
    backendRemove,
    backendRename,
};

static bool selectDefaultStore(bool spiffs_available,
                               std::vector<StoredMessage>& migration)
{
    detail::messageStoreSelectDefaultBackend();
    if (!spiffs_available || !messageStoreBegin()) return false;
    const int count = messageStoreCount();
    if (count <= 0) return true;
    migration.resize(static_cast<size_t>(count));
    const int loaded = messageStoreLoadAll(migration.data(), count);
    if (loaded != count) {
        migration.clear();
        return false;
    }
    return true;
}

static bool migrationIsDurablyRepresented(
    const std::vector<StoredMessage>& migration)
{
    if (migration.empty()) return true;
    const int count = messageStoreCount();
    if (count < static_cast<int>(migration.size())) return false;

    std::vector<StoredMessage> destination(static_cast<size_t>(count));
    if (messageStoreLoadAll(destination.data(), count) != count) return false;
    for (const StoredMessage& source : migration) {
        bool found = false;
        for (const StoredMessage& candidate : destination) {
            if (detail::storedMessageSameIdentity(source, candidate)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static bool retireDefaultStore(const std::vector<StoredMessage>& migration)
{
    if (migration.empty()) return true;
    detail::messageStoreSelectDefaultBackend();
    // The source is retired only after the SD backend has committed and been
    // read back above. messageStoreClear() is an atomic empty-store replace,
    // so a failed clear leaves the original source available for retry.
    return messageStoreBegin() && messageStoreClear();
}

static bool restoreDefaultStoreWithMigration(
    const std::vector<StoredMessage>& migration)
{
    detail::messageStoreSelectDefaultBackend();
    if (!messageStoreBegin() || !messageStoreClear()) return false;
    for (const StoredMessage& message : migration) {
        if (!messageStoreAppend(message)) return false;
    }
    return true;
}

} // namespace

bool sdMessageStoreSelect(bool spiffs_available)
{
    g_using_sd = false;
    g_degraded = false;

    std::vector<StoredMessage> migration;
    const bool spiffs_ready = selectDefaultStore(spiffs_available, migration);
#if defined(ESP32_PLATFORM)
    const bool card_ready = sigurdos_sdcard_mounted();
#else
    const bool card_ready = g_native_mounted;
#endif
    if (!card_ready) {
        g_degraded = true;
        return spiffs_ready;
    }

    const auto fallBackToDefault = [&]() {
        detail::messageStoreSelectDefaultBackend();
        if (spiffs_ready) (void)messageStoreBegin();
        g_degraded = true;
        return spiffs_ready;
    };

    detail::messageStoreSelectBackend(&SD_MESSAGE_STORE_BACKEND);
    if (!messageStoreBegin()) {
        return fallBackToDefault();
    }

    for (const StoredMessage& message : migration) {
        // Migration must NOT use the public append path: its runtime failover
        // would retry a failed SD write into the SPIFFS source and make the
        // migration look successful, after which the source is retired and the
        // data is lost. A failed migration must keep the source for retry.
        if (!detail::messageStoreAppendOnce(message, nullptr)) {
            return fallBackToDefault();
        }
    }

    if (!migration.empty()) {
        if (!migrationIsDurablyRepresented(migration)) {
            return fallBackToDefault();
        }

        if (!retireDefaultStore(migration)) {
            return fallBackToDefault();
        }

        // Retiring the source changes the selected backend; re-open SD so the
        // caller always returns with the SD store active. If that re-open
        // fails, restore the source records before falling back to SPIFFS.
        detail::messageStoreSelectBackend(&SD_MESSAGE_STORE_BACKEND);
        if (!messageStoreBegin()) {
            const bool restored = restoreDefaultStoreWithMigration(migration);
            if (!restored) {
                g_degraded = true;
                return false;
            }
            return fallBackToDefault();
        }
    }

    g_using_sd = true;
    g_degraded = false;
    return true;
}

namespace {

static void restoreDefaultMessageStore()
{
    detail::messageStoreSelectDefaultBackend();
    (void)messageStoreBegin();
}

static bool sdMessageStoreMounted()
{
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_mounted();
#else
    return g_native_mounted;
#endif
}

static bool resetPath(const char* path, const char* suffix,
                      char* out, size_t out_size)
{
    if (!path || !suffix || !out || out_size == 0) return false;
    if (suffix[0] == '\0') {
        const int written = std::snprintf(out, out_size, "%s", path);
        if (written <= 0 || static_cast<size_t>(written) >= out_size) return false;
    } else if (!appendPath(path, suffix, out, out_size)) {
        return false;
    }

    const bool removed = backendRemove(nullptr, out);
    return removed && !backendExists(nullptr, out);
}

} // namespace

bool sdMessageStoreReset(SdMessageStoreResetPreflightFn preflight,
                         void* preflight_context)
{
    const bool was_using_sd = g_using_sd;

    // The preflight must run before deselecting or deleting the backend so a
    // transport failure cannot be mistaken for a successful storage reset.
    if (preflight && !preflight(preflight_context)) {
        detail::messageStoreSelectDefaultBackend();
        g_using_sd = false;
        g_degraded = true;
        restoreDefaultMessageStore();
        return false;
    }

    // Message-store operations open files for each call, so selecting the
    // default backend is the close/quiesce operation for the SD backend. It
    // also invalidates the in-memory recovery/index state before deletion.
    detail::messageStoreSelectDefaultBackend();
    g_using_sd = false;
    g_degraded = false;

    // No active SD backend means there is no SD history to erase. If an
    // active backend lost its mount, however, reset cannot claim that history
    // was deleted and must fail closed.
    if (!sdMessageStoreMounted()) {
        g_degraded = true;
        restoreDefaultMessageStore();
        return !was_using_sd;
    }

    const char* const suffixes[] = {
        SD_MESSAGE_STORE_TEMP_SUFFIX,
        SD_MESSAGE_STORE_READY_SUFFIX,
        SD_MESSAGE_STORE_CORRUPT_SUFFIX,
        "",
    };
    bool removed = true;
    for (const char* suffix : suffixes) {
        char path[256];
        if (!resetPath(SD_MESSAGE_STORE_BACKEND.path(nullptr), suffix,
                       path, sizeof(path))) {
            removed = false;
        }
    }

    if (!removed) {
        // Keep the runtime usable after a non-rebooting failure, but do not
        // leave the SD backend selected after its files may be incomplete.
        g_degraded = true;
        restoreDefaultMessageStore();
        return false;
    }
    return true;
}

bool sdMessageStoreUsingSd()
{
    // message_store.cpp can demote a live SD backend after a media/write
    // failure. Keep the UI-facing SD status consistent with that synchronized
    // coordinator even though this module owns the selection flag.
    return g_using_sd && !detail::messageStoreBackendDegraded();
}

bool sdMessageStoreDegraded()
{
    return g_degraded || detail::messageStoreBackendDegraded();
}

uint32_t sdMessageStoreCapacity()
{
    return sdMessageStoreUsingSd() ? SD_MESSAGE_STORE_MAX_RECORDS
                                   : MESSAGE_STORE_MAX_RECORDS;
}

uint64_t sdMessageStoreFreeBytes()
{
#if defined(ESP32_PLATFORM)
    return sigurdos_sdcard_mounted() ? sigurdos_sdcard_free_bytes() : 0;
#else
    if (!g_native_mounted) return 0;
    struct statvfs info {};
    if (::statvfs(g_native_root, &info) != 0) return 0;
    return static_cast<uint64_t>(info.f_bavail) * info.f_frsize;
#endif
}

#if !defined(ESP32_PLATFORM)
void sdMessageStoreSetNativeRoot(const char* root)
{
    if (!root || !root[0]) return;
    std::strncpy(g_native_root, root, sizeof(g_native_root) - 1);
    g_native_root[sizeof(g_native_root) - 1] = '\0';
    updateNativePath();
}

void sdMessageStoreSetNativeMounted(bool mounted)
{
    g_native_mounted = mounted;
}

void sdMessageStoreSetNativeAppendWriteLimit(int bytes)
{
    g_native_append_write_limit = bytes < -1 ? -1 : bytes;
}

void sdMessageStoreSetNativeRemoveFailure(bool fail)
{
    g_native_remove_failure = fail;
}

void sdMessageStoreResetNative()
{
    std::strncpy(g_native_root, "/tmp/sigurdos_sd_store", sizeof(g_native_root) - 1);
    g_native_root[sizeof(g_native_root) - 1] = '\0';
    updateNativePath();
    g_native_mounted = false;
    g_native_append_write_limit = -1;
    g_native_remove_failure = false;
    g_using_sd = false;
    g_degraded = false;
}

const char* sdMessageStoreNativePath()
{
    return g_native_path;
}
#endif

} // namespace sigurdos::mesh
