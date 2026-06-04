// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "regions.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <mbedtls/sha256.h>
#include <cstring>

namespace sigurdos {
namespace mesh {

static constexpr const char* REGIONS_FILE = "/regions.dat";

// ── Persistence ────────────────────────────────────────

int loadRegions(SigurdRegion* out, int max) {
    if (!out || max <= 0) return 0;

    if (!SPIFFS.begin(true)) {
        return 0;  // SPIFFS not available
    }

    File f = SPIFFS.open(REGIONS_FILE, "r");
    if (!f) {
        SPIFFS.end();
        return 0;
    }

    size_t sz = f.size();
    if (sz < 4) { f.close(); SPIFFS.end(); return 0; }

    uint32_t count = 0;
    size_t rd = f.read((uint8_t*)&count, 4);
    if (rd != 4) { f.close(); SPIFFS.end(); return 0; }

    // Guard against corrupt/spoofed count values
    if (count > (uint32_t)max) count = max;
    if (count == 0) { f.close(); SPIFFS.end(); return 0; }

    size_t expected = count * sizeof(SigurdRegion);
    if (sz - 4 < expected) { f.close(); SPIFFS.end(); return 0; }

    rd = f.read((uint8_t*)out, expected);
    f.close();
    SPIFFS.end();

    // Force null-terminate all names in case of truncation
    for (uint32_t i = 0; i < count; i++) {
        out[i].name[sizeof(out[i].name) - 1] = '\0';
    }

    return (rd >= expected) ? (int)count : 0;
}

bool saveRegions(const SigurdRegion* list, int count) {
    if (!list || count < 0 || count > SIGURD_MAX_REGIONS) return false;

    if (!SPIFFS.begin(true)) return false;

    File f = SPIFFS.open(REGIONS_FILE, "w");
    if (!f) { SPIFFS.end(); return false; }

    uint32_t cnt = (uint32_t)count;
    f.write((const uint8_t*)&cnt, 4);
    f.write((const uint8_t*)list, count * sizeof(SigurdRegion));
    f.close();
    SPIFFS.end();
    return true;
}

// ── Key derivation ─────────────────────────────────────

bool deriveRegionKey(const char* name, uint8_t* out_key16) {
    if (!name || !out_key16 || name[0] == '\0') return false;

    // Only # regions are auto-derived. $ regions use a user-supplied secret.
    if (name[0] != '#') return false;

    // Compute SHA256 of the full name (including the '#' prefix)
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const uint8_t*)name, strlen(name));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    memcpy(out_key16, hash, 16);
    return true;
}

} // namespace mesh
} // namespace sigurdos
