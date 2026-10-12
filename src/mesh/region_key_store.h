#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// SigurdOS-owned RAM store for private ($) region transport keys.
//
// MeshCore's TransportKeyStore keeps such keys in a private cache whose
// save/remove/clear methods are hardware-keystore TODOs that fail on ESP32
// builds, so SigurdOS retains the runtime semantics here and serves region
// reads through regionTransportKeysFor(). Same capacity and replacement
// semantics as the original in-core implementation.

#include <cstdint>
#include <cstring>

#include <helpers/TransportKeyStore.h> // TransportKey, MAX_TKS_ENTRIES

namespace sigurdos {
namespace mesh {

class RegionKeyStore {
public:
    bool clear() {
        num_ = 0;
        return true;
    }

    int loadKeysFor(uint16_t id, TransportKey keys[], int max_num) const {
        int n = 0;
        for (int i = 0; i < num_ && n < max_num; ++i) {
            if (ids_[i] == id) keys[n++] = keys_[i];
        }
        return n;
    }

    bool saveKeysFor(uint16_t id, const TransportKey keys[], int num) {
        if (id == 0 || num < 0 || num > MAX_TKS_ENTRIES ||
            (num > 0 && keys == nullptr)) {
            return false;
        }

        // Build the replacement off to the side so a full store leaves the
        // current cache untouched. Multiple keys for one region stay contiguous.
        uint16_t next_ids[MAX_TKS_ENTRIES];
        TransportKey next_keys[MAX_TKS_ENTRIES];
        int next_num = 0;
        for (int i = 0; i < num_; ++i) {
            if (ids_[i] == id) continue;
            next_ids[next_num] = ids_[i];
            next_keys[next_num++] = keys_[i];
        }
        if (next_num + num > MAX_TKS_ENTRIES) return false;
        for (int i = 0; i < num; ++i) {
            next_ids[next_num] = id;
            next_keys[next_num++] = keys[i];
        }

        std::memcpy(ids_, next_ids, sizeof(uint16_t) * next_num);
        std::memcpy(keys_, next_keys, sizeof(TransportKey) * next_num);
        num_ = next_num;
        return true;
    }

    bool removeKeys(uint16_t id) {
        if (id == 0) return false;
        int dest = 0;
        for (int i = 0; i < num_; ++i) {
            if (ids_[i] == id) continue;
            ids_[dest] = ids_[i];
            keys_[dest++] = keys_[i];
        }
        num_ = dest;
        return true;
    }

private:
    uint16_t ids_[MAX_TKS_ENTRIES]{};
    TransportKey keys_[MAX_TKS_ENTRIES]{};
    int num_ = 0;
};

} // namespace mesh
} // namespace sigurdos
