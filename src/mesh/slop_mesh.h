// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Minimal MeshCore Mesh subclass for SlopOS-TDeck.
// Based on advice from Claude Code analysis of MeshCore internals.
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#pragma once
#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/AdvertDataHelpers.h>
#include <hal/prefs.h>

namespace slopos {
namespace mesh {

static constexpr int SLOP_MAX_CONTACTS  = 64;
static constexpr int SLOP_MAX_CHANNELS  = 8;
#define OUT_PATH_UNKNOWN  0xFF

struct SlopContact {
    ::mesh::Identity id;
    uint8_t  secret[PUB_KEY_SIZE];
    char     name[32];
    uint32_t last_seen;    // RTC timestamp of last advert
    int      last_rssi;    // RSSI of last received packet (dBm)
    uint8_t  out_path_len; // OUT_PATH_UNKNOWN if no known direct path
    uint8_t  out_path[MAX_PATH_SIZE];
};

// Mirrors MeshCore's ChannelDetails for group channel storage
struct SlopChannel {
    ::mesh::GroupChannel channel;
    char name[32];
};

class SlopMesh : public ::mesh::Mesh {
    SlopContact _contacts[SLOP_MAX_CONTACTS];
    int _nContacts = 0;

    SlopChannel _channels[SLOP_MAX_CHANNELS];
    int _nChannels = 0;

    // Match cache for searchPeersByHash/getPeerSharedSecret back-to-back calls
    int  _matchIdxs[SLOP_MAX_CONTACTS];
    int  _nMatches = 0;

    slopos::NodePrefs _prefs;
    void (*_onMessage)(const char* sender, const char* channel, const char* text);

    // Trace result storage
    bool     _has_trace_result = false;
    uint32_t _last_trace_tag = 0;
    uint8_t  _last_trace_len = 0;
    uint8_t  _last_trace_snrs[MAX_PATH_SIZE];
    uint8_t  _last_trace_hashes[MAX_PATH_SIZE];

protected:
    // ── Peer DB ──────────────────────────────────────
    int searchPeersByHash(const uint8_t* hash) override {
        _nMatches = 0;
        for (int i = 0; i < _nContacts; i++) {
            if (_contacts[i].id.isHashMatch(hash))
                _matchIdxs[_nMatches++] = i;
        }
        return _nMatches;
    }

    void getPeerSharedSecret(uint8_t* dest, int peer_idx) override {
        if (peer_idx >= 0 && peer_idx < _nMatches)
            memcpy(dest, _contacts[_matchIdxs[peer_idx]].secret, PUB_KEY_SIZE);
    }

    // ── Incoming text ────────────────────────────────
    void onPeerDataRecv(::mesh::Packet* pkt, uint8_t type, int sender_idx,
                        const uint8_t* secret, uint8_t* data, size_t len) override
    {
        if (type != PAYLOAD_TYPE_TXT_MSG || _nMatches == 0 || !_onMessage) return;
        // Data layout: [4-byte LE timestamp][null-terminated text]
        // Safety: force null-termination — packet data may lack a terminator
        if (len > 4) data[len - 1] = '\0';
        const char* text = (len > 4) ? (const char*)(data + 4) : "";
        int idx = _matchIdxs[sender_idx];
        const char* sender = _contacts[idx].name;
        if (sender[0]) {
            _onMessage(sender, "", text);
        }
    }

    // ── Contact discovery ─────────────────────────────
    void onAdvertRecv(::mesh::Packet*, const ::mesh::Identity& id, uint32_t timestamp,
                      const uint8_t* app_data, size_t app_data_len) override
    {
        // Parse advert using MeshCore's standard parser
        AdvertDataParser parser(app_data, (uint8_t)app_data_len);
        if (!parser.isValid()) return;

        const char* name = parser.getName();
        if (!name || !name[0]) {
            // No name — generate a fallback from pub_key
            static char fallback[16];
            snprintf(fallback, sizeof(fallback), "node_%02x", id.pub_key[0]);
            name = fallback;
        }

        // Deduplicate
        for (int i = 0; i < _nContacts; i++)
            if (_contacts[i].id.matches(id)) {
                _contacts[i].last_seen = timestamp;
                // Update name if changed (e.g. user renamed device)
                strncpy(_contacts[i].name, name, sizeof(_contacts[i].name) - 1);
                _contacts[i].name[sizeof(_contacts[i].name) - 1] = '\0';
                return;
            }

        if (_nContacts >= SLOP_MAX_CONTACTS) return;

        SlopContact& c = _contacts[_nContacts++];
        c.id = id;
        c.last_seen = timestamp;
        c.last_rssi = 0;
        self_id.calcSharedSecret(c.secret, id);

        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
    }

    // ── Group text ────────────────────────────────────
    int searchChannelsByHash(const uint8_t* hash, ::mesh::GroupChannel out[], int max) override {
        int n = 0;
        for (int i = 0; i < _nChannels && n < max; i++) {
            if (_channels[i].channel.hash[0] == hash[0]) {   // first byte match
                out[n++] = _channels[i].channel;
            }
        }
        return n;
    }

    void onGroupDataRecv(::mesh::Packet*, uint8_t type, const ::mesh::GroupChannel& ch,
                          uint8_t* data, size_t len) override
    {
        if (type != PAYLOAD_TYPE_GRP_TXT || !_onMessage) return;
        if (len > 5) data[len - 1] = '\0';
        const char* text = (len > 5) ? (const char*)(data + 5) : "";

        const char* chname = nullptr;
        for (int i = 0; i < _nChannels; i++) {
            if (memcmp(_channels[i].channel.hash, ch.hash, sizeof(ch.hash)) == 0) {
                chname = _channels[i].name;
                break;
            }
        }
        const char* channel = chname ? chname : "[group]";
        _onMessage(channel, channel, text);
    }

public:
    SlopMesh(::mesh::Radio& r, ::mesh::MillisecondClock& ms, ::mesh::RNG& rng,
             ::mesh::RTCClock& rtc, ::mesh::PacketManager& mgr, ::mesh::MeshTables& tbl)
        : ::mesh::Mesh(r, ms, rng, rtc, mgr, tbl),
          _onMessage(nullptr)
    {
        _prefs.set_defaults();
        for (int i = 0; i < SLOP_MAX_CONTACTS; i++) {
            _contacts[i].out_path_len = OUT_PATH_UNKNOWN;
        }
    }

    void setMessageCallback(void (*cb)(const char* sender, const char* channel, const char* text)) { _onMessage = cb; }

    // ── Path learning ────────────────────────────────
    void onPathRecv(::mesh::Packet* pkt, ::mesh::Identity& sender,
                    uint8_t* path, uint8_t path_len,
                    uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override
    {
        // Find contact by matching identity
        for (int i = 0; i < _nContacts; i++) {
            if (_contacts[i].id.matches(sender)) {
                // Use MeshCore's validated path copy — handles encoded path_len
                // correctly and rejects invalid/oversized paths
                if (!::mesh::Packet::isValidPathLen(path_len)) {
                    _contacts[i].out_path_len = OUT_PATH_UNKNOWN;
                    return;
                }
                _contacts[i].out_path_len =
                    ::mesh::Packet::copyPath(_contacts[i].out_path, path, path_len);
                return;
            }
        }
    }

    // ── Trace route ──────────────────────────────────
    bool sendTrace(int contact_idx, uint32_t tag) {
        if (contact_idx < 0 || contact_idx >= _nContacts) return false;
        SlopContact& c = _contacts[contact_idx];
        if (c.out_path_len == OUT_PATH_UNKNOWN) return false;

    ::mesh::Packet* pkt = createTrace(tag, 0, 0);  // auth_code=0 for now
    if (!pkt) return false;
    // sendDirect() for TRACE packets treats path_len as raw bytes
    // (memcpy's path_len bytes into payload). out_path_len is encoded
    // (hash_count | hash_size_shift), so decode to raw byte count.
    uint8_t hash_count = c.out_path_len & 63;
    uint8_t hash_size = (c.out_path_len >> 6) + 1;
    uint8_t raw_len = hash_count * hash_size;
    if (raw_len > MAX_PATH_SIZE) raw_len = MAX_PATH_SIZE;
    sendDirect(pkt, c.out_path, raw_len);
        return true;
    }

    // Called when a trace reaches its destination (us) and we have the full path
    void onTraceRecv(::mesh::Packet*, uint32_t tag, uint32_t auth_code, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) override
    {
        // Store the trace result for the UI to display
        _last_trace_tag = tag;
        // Clamp BEFORE storing — getTracePathLen() returns _last_trace_len,
        // and the UI buffer size must not be exceeded
        if (path_len > MAX_PATH_SIZE) path_len = MAX_PATH_SIZE;
        _last_trace_len = path_len;
        memcpy(_last_trace_snrs, path_snrs, path_len);
        memcpy(_last_trace_hashes, path_hashes, path_len);
        _has_trace_result = true;
    }

    bool hasTraceResult() const { return _has_trace_result; }
    uint32_t getTraceTag() const { return _last_trace_tag; }
    uint8_t  getTracePathLen() const { return _last_trace_len; }
    void getTracePath(uint8_t* snrs_out, uint8_t* hashes_out) const {
        memcpy(snrs_out, _last_trace_snrs, _last_trace_len);
        memcpy(hashes_out, _last_trace_hashes, _last_trace_len);
    }
    void clearTraceResult() { _has_trace_result = false; }

    // ── Send helpers ──────────────────────────────────
    bool sendTextTo(const char* dest_name, const char* text) {
        for (int i = 0; i < _nContacts; i++) {
            if (strcmp(_contacts[i].name, dest_name) != 0) continue;
            // Payload: 4-byte LE timestamp + null-terminated text, max 150 chars text
            // MeshCore caps encrypted payload at MAX_PACKET_PAYLOAD - 16 = 168 bytes.
            // With 4-byte ts + 1-byte null, 150 chars text → 155 bytes plaintext fits.
            static constexpr size_t MAX_PAYLOAD = 150;
            uint8_t buf[4 + MAX_PAYLOAD];
            uint32_t ts = getRTCClock()->getCurrentTime();
            memcpy(buf, &ts, 4);
            size_t text_len = strnlen(text, MAX_PAYLOAD - 1);
            memcpy(buf + 4, text, text_len);
            buf[4 + text_len] = '\0';
            size_t len = 4 + text_len + 1;
            ::mesh::Packet* pkt = createDatagram(PAYLOAD_TYPE_TXT_MSG,
                                                  _contacts[i].id,
                                                  _contacts[i].secret,
                                                  buf, len);
            if (!pkt) return false;
            if (_contacts[i].out_path_len != OUT_PATH_UNKNOWN) {
                sendDirect(pkt, _contacts[i].out_path, _contacts[i].out_path_len);
            } else {
                sendFlood(pkt);
            }
            return true;
        }
        return false;
    }

    void broadcastAdvert(const char* name) {
        AdvertDataBuilder builder(ADV_TYPE_CHAT, name);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt);
    }

    void broadcastAdvert(const char* name, double lat, double lon) {
        AdvertDataBuilder builder(ADV_TYPE_CHAT, name, lat, lon);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt);
    }

    // ── Contact export ────────────────────────────────
    int getContactCount() const { return _nContacts; }
    const SlopContact* getContact(int i) const {
        return (i >= 0 && i < _nContacts) ? &_contacts[i] : nullptr;
    }

    // ── Channel management ────────────────────────────
    // Minimal base64 decode (avoids external dependency — MeshCore uses base64.hpp)
    static int decode_b64(const char* in, size_t in_len, uint8_t* out, size_t out_cap) {
        static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int o = 0;
        uint32_t buf = 0;
        int bits = 0;
        for (size_t i = 0; i < in_len && in[i] != '='; i++) {
            const char* p = strchr(T, in[i]);
            if (!p) continue;
            buf = (buf << 6) | (uint32_t)(p - T);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (o < (int)out_cap) out[o++] = (uint8_t)(buf >> bits);
                buf &= (1U << bits) - 1;
            }
        }
        return o;
    }

    bool addChannel(const char* name, const char* psk_base64) {
        if (_nChannels >= SLOP_MAX_CHANNELS) return false;
        for (int j = 0; j < _nChannels; j++) {
            if (strcmp(_channels[j].name, name) == 0) return true;
        }
        SlopChannel& ch = _channels[_nChannels];
        memset(ch.channel.secret, 0, sizeof(ch.channel.secret));
        int len = decode_b64(psk_base64, strlen(psk_base64), ch.channel.secret,
                             sizeof(ch.channel.secret));
        if (len != 32 && len != 16) return false;
        ::mesh::Utils::sha256(ch.channel.hash, sizeof(ch.channel.hash),
                              ch.channel.secret, len);
        strncpy(ch.name, name, sizeof(ch.name) - 1);
        ch.name[sizeof(ch.name) - 1] = '\0';
        _nChannels++;
        return true;
    }

    bool addHashtagChannel(const char* name) {
        if (_nChannels >= SLOP_MAX_CHANNELS || !name) return false;

        char normalized[32];
        size_t src = 0;
        while (name[src] == ' ' || name[src] == '\t') src++;

        size_t out = 0;
        if (name[src] != '#') normalized[out++] = '#';
        while (name[src] && name[src] != ' ' && name[src] != '\t' &&
               name[src] != '\r' && name[src] != '\n' && out < sizeof(normalized) - 1) {
            normalized[out++] = name[src++];
        }
        if (name[src] && name[src] != ' ' && name[src] != '\t' &&
            name[src] != '\r' && name[src] != '\n') {
            return false;
        }
        normalized[out] = '\0';
        if (out <= 1) return false;

        for (int j = 0; j < _nChannels; j++) {
            if (strcmp(_channels[j].name, normalized) == 0) return true;
        }

        SlopChannel& ch = _channels[_nChannels];
        memset(&ch, 0, sizeof(ch));
        ::mesh::Utils::sha256(ch.channel.secret, CIPHER_KEY_SIZE,
                              (const uint8_t*)normalized, strlen(normalized));
        ::mesh::Utils::sha256(ch.channel.hash, sizeof(ch.channel.hash),
                              ch.channel.secret, CIPHER_KEY_SIZE);
        strncpy(ch.name, normalized, sizeof(ch.name) - 1);
        ch.name[sizeof(ch.name) - 1] = '\0';
        _nChannels++;
        return true;
    }

    int getChannelCount() const { return _nChannels; }
    const SlopChannel* getChannel(int i) const {
        return (i >= 0 && i < _nChannels) ? &_channels[i] : nullptr;
    }

    bool sendGroupText(int channel_idx, const char* text) {
        if (channel_idx < 0 || channel_idx >= _nChannels) return false;
        if (!text || !text[0]) return false;

        // Group text payload: [4-byte LE ts][txt_type=0][null-terminated text]
        // MeshCore caps encrypted payload at MAX_PACKET_PAYLOAD - 16 = 168 bytes.
        // With 4+1+1 overhead, 150 chars text → 156 bytes plaintext fits.
        static constexpr size_t MAX_GRP_PAYLOAD = 150;
        uint8_t buf[5 + MAX_GRP_PAYLOAD];
        uint32_t ts = getRTCClock()->getCurrentTime();
        memcpy(buf, &ts, 4);
        buf[4] = 0;   // text_type = 0 (plain text)
        size_t text_len = strnlen(text, MAX_GRP_PAYLOAD - 1);
        memcpy(buf + 5, text, text_len);
        buf[5 + text_len] = '\0';   // null-terminate the payload
        size_t total = 5 + text_len + 1;  // include null terminator in sent length
        if (total > sizeof(buf)) total = sizeof(buf);  // safety clamp

        ::mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT,
                                                   _channels[channel_idx].channel,
                                                   buf, total);
        if (!pkt) return false;
        sendFlood(pkt);
        return true;
    }

    // ── Prefs ─────────────────────────────────────────
    slopos::NodePrefs& prefs() { return _prefs; }
};

} // namespace mesh
} // namespace slopos
