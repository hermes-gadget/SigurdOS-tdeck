// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Minimal MeshCore Mesh subclass for SlopOS-TDeck.
// Based on advice from Claude Code analysis of MeshCore internals.
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#pragma once
#include <math.h>
#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/AdvertDataHelpers.h>
#include <hal/prefs.h>
#include "utils/utf8_util.h"
#include "../diagnostics/debug_cfg.h"

namespace slopos {
namespace mesh {

// Forward declaration for packet logging (defined in mesh_wrapper.cpp)
void pushPacketLog(const char* source, int rssi, float snr, const char* type);

static constexpr int SLOP_MAX_CONTACTS  = 64;
static constexpr int SLOP_MAX_CHANNELS  = 8;
#define OUT_PATH_UNKNOWN  0xFF

struct SlopContact {
    ::mesh::Identity id;
    uint8_t  secret[PUB_KEY_SIZE];
    char     name[32];
    uint8_t  type;          // ADV_TYPE_* from the advert (CHAT, REPEATER, ROOM, etc.)
    bool     has_location;  // true when the advert included GPS coordinates
    float    latitude;
    float    longitude;
    uint32_t last_seen;    // RTC timestamp of last advert
    int      last_rssi;    // RSSI of last received packet (dBm)
    float    last_snr;     // SNR of last received packet (dB)
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

    char _own_name[32];

    // Trace result storage
    bool     _has_trace_result = false;
    uint32_t _last_trace_tag = 0;
    uint8_t  _last_trace_len = 0;
    uint8_t  _last_trace_snrs[MAX_PATH_SIZE];
    uint8_t  _last_trace_hashes[MAX_PATH_SIZE];

    // ── Ping Nearby ────────────────────────────────
    static constexpr int PING_RESULTS_MAX = 32;
    static constexpr uint32_t PING_COOLDOWN_MS = 30000;
    static constexpr uint32_t PING_WINDOW_MS = 3000;

    uint32_t _ping_tag = 0;
    uint32_t _ping_sent_at = 0;
    uint32_t _ping_last_at = 0;
    int      _ping_n_results = 0;
    struct PingResult {
        char name[32];
        int rssi;
    };
    PingResult _ping_results[PING_RESULTS_MAX];

    // ── Duty cycle ──────────────────────────────────
    uint8_t _duty_cycle = 0;  // 0 = disabled, 1-100 = percent of window

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

    // ── Incoming peer data (DM / REQ / RESPONSE) ────
    void onPeerDataRecv(::mesh::Packet*, uint8_t type, int sender_idx,
                        const uint8_t* secret, uint8_t* data, size_t len) override
    {
        // Accept TXT_MSG, REQ, and RESPONSE payloads as incoming messages
        if (type != PAYLOAD_TYPE_TXT_MSG && type != PAYLOAD_TYPE_REQ &&
            type != PAYLOAD_TYPE_RESPONSE) return;
        if (_nMatches == 0 || !_onMessage) return;
        // TXT_MSG layout: [4-byte LE timestamp][1-byte text flags][null-terminated text]
        // REQ/RESPONSE: raw text (no timestamp/flags header).
        const char* text;
        if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {
            data[len - 1] = '\0';
            text = (const char*)(data + 5);
        } else if (len > 0) {
            data[len - 1] = '\0';
            text = (const char*)data;
        } else {
            return;
        }
        int idx = _matchIdxs[sender_idx];
        const char* sender = _contacts[idx].name;
        if (sender[0]) {
            _onMessage(sender, "", text);
        }
    }

    // ── Contact discovery ─────────────────────────────
    void onAdvertRecv(::mesh::Packet* pkt, const ::mesh::Identity& id, uint32_t timestamp,
                      const uint8_t* app_data, size_t app_data_len) override
    {
        // Parse advert using MeshCore's standard parser
        AdvertDataParser parser(app_data, (uint8_t)app_data_len);
        if (!parser.isValid()) return;

        const char* name = parser.getName();
        if (!name || !name[0]) {
            // No name — generate a fallback from pub_key
            char fallback[16];
            snprintf(fallback, sizeof(fallback), "node_%02x", id.pub_key[0]);
            name = fallback;
        }

        auto update_location = [](SlopContact& c, const AdvertDataParser& p) {
            c.has_location = p.hasLatLon();
            if (c.has_location) {
                c.latitude = (float)p.getLat();
                c.longitude = (float)p.getLon();
            } else {
                c.latitude = 0.0f;
                c.longitude = 0.0f;
            }
        };

        // Deduplicate
        for (int i = 0; i < _nContacts; i++)
            if (_contacts[i].id.matches(id)) {
                _contacts[i].last_seen = timestamp;
                _contacts[i].last_rssi = (int)_radio->getLastRSSI();
                _contacts[i].last_snr = pkt->getSNR();
                _contacts[i].type = parser.getType();
                update_location(_contacts[i], parser);
                strncpy(_contacts[i].name, name, sizeof(_contacts[i].name) - 1);
                _contacts[i].name[sizeof(_contacts[i].name) - 1] = '\0';
                pushPacketLog(name, _contacts[i].last_rssi, pkt->getSNR(), "ADVERT");
                return;
            }

        // Flood max hops check: skip auto-adding contacts beyond the configured limit
        {
            uint8_t max_hops = slopos::prefs_get().flood_max_hops;
            if (max_hops > 0 && pkt->getPathHashCount() >= max_hops) {
                pushPacketLog(name, (int)_radio->getLastRSSI(), pkt->getSNR(), "ADVERT(SKIPPED)");
                return;
            }
        }

        if (_nContacts >= SLOP_MAX_CONTACTS) {
            // List full — evict the contact with the oldest last_seen (LRU)
            int oldest = 0;
            for (int i = 1; i < SLOP_MAX_CONTACTS; i++) {
                if (_contacts[i].last_seen < _contacts[oldest].last_seen)
                    oldest = i;
            }
            SlopContact& c = _contacts[oldest];
            c.id = id;
            c.last_seen = timestamp;
            c.last_rssi = (int)_radio->getLastRSSI();
            c.last_snr = pkt->getSNR();
            c.type = parser.getType();
            update_location(c, parser);
            c.out_path_len = OUT_PATH_UNKNOWN;
            self_id.calcSharedSecret(c.secret, id);
            strncpy(c.name, name, sizeof(c.name) - 1);
            c.name[sizeof(c.name) - 1] = '\0';
            pushPacketLog(name, c.last_rssi, pkt->getSNR(), "ADVERT");
            return;
        }

        SlopContact& c = _contacts[_nContacts++];
        c.id = id;
        c.last_seen = timestamp;
        c.last_rssi = (int)_radio->getLastRSSI();
        c.last_snr = pkt->getSNR();
        c.type = parser.getType();
        update_location(c, parser);
        self_id.calcSharedSecret(c.secret, id);

        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        pushPacketLog(name, c.last_rssi, pkt->getSNR(), "ADVERT");
    }

    // ── Group text ────────────────────────────────────
    int searchChannelsByHash(const uint8_t* hash, ::mesh::GroupChannel out[], int max) override {
        int n = 0;
        for (int i = 0; i < _nChannels && n < max; i++) {
            // MeshCore sends only PATH_HASH_SIZE (1) byte of the channel hash
            // in the packet header — compare just the first byte, matching
            // BaseChatMesh's implementation.
            if (_channels[i].channel.hash[0] == hash[0]) {
                out[n++] = _channels[i].channel;
            }
        }
        return n;
    }

    // Parse sender name from formatted group text ("sender_name: message")
    static void parse_group_sender(const char* raw_text, char* sender_out, size_t sender_cap,
                                   const char** text_out)
    {
        const char* colon = strstr(raw_text, ": ");
        if (colon && colon > raw_text) {
            size_t name_len = colon - raw_text;
            if (name_len >= sender_cap) name_len = sender_cap - 1;
            memcpy(sender_out, raw_text, name_len);
            sender_out[name_len] = '\0';
            *text_out = colon + 2;
        } else {
            strncpy(sender_out, raw_text, sender_cap - 1);
            sender_out[sender_cap - 1] = '\0';
            *text_out = "";
        }
    }

    void onGroupDataRecv(::mesh::Packet*, uint8_t type, const ::mesh::GroupChannel& ch,
                          uint8_t* data, size_t len) override
    {
        if (type != PAYLOAD_TYPE_GRP_TXT && type != PAYLOAD_TYPE_GRP_DATA) return;
        if (!_onMessage || len <= 5) return;

        // data[0..3] = LE timestamp
        // data[4]    = text_type (0 = plain text)
        // data[5..]  = "<sender_name>: <message>\0"  (BaseChatMesh-compatible format)
        if (len > 5) data[len - 1] = '\0';

        const char* raw_text = (const char*)(data + 5);

        const char* chname = nullptr;
        for (int i = 0; i < _nChannels; i++) {
            if (memcmp(_channels[i].channel.hash, ch.hash, sizeof(ch.hash)) == 0) {
                chname = _channels[i].name;
                break;
            }
        }
        const char* channel = chname ? chname : "[group]";

        // Parse "<sender_name>: <message>" — this is the format used by
        // MeshCore's BaseChatMesh (and now by SlopOS sendGroupText).
        // Falls back to raw_text as sender if no colon separator found.
        char sender_buf[32];
        const char* message;
        parse_group_sender(raw_text, sender_buf, sizeof(sender_buf), &message);

        _onMessage(sender_buf, channel, message);
    }

    // ── Anonymous data ────────────────────────────────
    void onAnonDataRecv(::mesh::Packet*, const uint8_t* secret,
                        const ::mesh::Identity& sender,
                        uint8_t* data, size_t len) override
    {
        if (!_onMessage || len <= 4) return;
        // Data layout: [4-byte LE timestamp][null-terminated text]
        if (len > 4) data[len - 1] = '\0';
        const char* text = (const char*)(data + 4);

        // Generate a fallback name from the sender's public key
        char fallback[16];
        snprintf(fallback, sizeof(fallback), "anon_%02x", sender.pub_key[0]);
        _onMessage(fallback, "", text);
    }

    // ── Path learning (from peer-path callbacks) ──────
    bool onPeerPathRecv(::mesh::Packet* pkt, int sender_idx, const uint8_t* secret,
                        uint8_t* path, uint8_t path_len, uint8_t extra_type,
                        uint8_t* extra, uint8_t extra_len) override
    {
        if (sender_idx >= _nMatches) return false;
        int idx = _matchIdxs[sender_idx];
        if (idx < 0 || idx >= _nContacts) return false;
        if (!::mesh::Packet::isValidPathLen(path_len)) return false;
        _contacts[idx].out_path_len =
            ::mesh::Packet::copyPath(_contacts[idx].out_path, path, path_len);
        return true;  // accept path — Mesh will send a reciprocal return path
    }

    // ── Path learning (from flood path callbacks) ─────
    void onPathRecv(::mesh::Packet* pkt, ::mesh::Identity& sender,
                    uint8_t* path, uint8_t path_len,
                    uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override
    {
        // Find contact by matching identity
        for (int i = 0; i < _nContacts; i++) {
            if (_contacts[i].id.matches(sender)) {
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

    // ── TX/RX delay tuning ──────────────────────────
    int calcRxDelay(float score, uint32_t air_time) const override {
        slopos::NodePrefs p = slopos::prefs_get();
        if (p.rx_delay_base <= 0.0f) return 0;
        return (int)((pow(p.rx_delay_base, 0.85f - score) - 1.0) * air_time);
    }

    uint32_t getRetransmitDelay(const ::mesh::Packet* packet) override {
        slopos::NodePrefs p = slopos::prefs_get();
        uint32_t t = (_radio->getEstAirtimeFor(packet->getRawLength()) * 52 / 50) / 2;
        return (uint32_t)(p.tx_delay_factor * getRNG()->nextInt(0, 5) * t);
    }

    uint32_t getDirectRetransmitDelay(const ::mesh::Packet* packet) override {
        slopos::NodePrefs p = slopos::prefs_get();
        if (p.direct_tx_delay_factor <= 0.0f) return 0;
        uint32_t t = (_radio->getEstAirtimeFor(packet->getRawLength()) * 52 / 50) / 2;
        return (uint32_t)(p.direct_tx_delay_factor * getRNG()->nextInt(0, 5) * t);
    }

public:
    // ── Trace route ──────────────────────────────────
    bool sendTrace(int contact_idx, uint32_t tag) {
        if (contact_idx < 0 || contact_idx >= _nContacts) return false;
        SlopContact& c = _contacts[contact_idx];
        if (c.out_path_len == OUT_PATH_UNKNOWN) return false;

        ::mesh::Packet* pkt = createTrace(tag, 0, 0);
        if (!pkt) return false;
        uint8_t hash_count = c.out_path_len & 63;
        uint8_t hash_size = (c.out_path_len >> 6) + 1;
        uint8_t raw_len = hash_count * hash_size;
        if (raw_len > MAX_PATH_SIZE) raw_len = MAX_PATH_SIZE;
        sendDirect(pkt, c.out_path, raw_len);
        return true;
    }

    void onTraceRecv(::mesh::Packet*, uint32_t tag, uint32_t auth_code, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) override
    {
        _last_trace_tag = tag;
        if (path_len > MAX_PATH_SIZE) path_len = MAX_PATH_SIZE;
        _last_trace_len = path_len;
        memcpy(_last_trace_snrs, path_snrs, path_len);
        memcpy(_last_trace_hashes, path_hashes, path_len);
        _has_trace_result = true;
    }

    // ── ACK ───────────────────────────────────────────
    void onAckRecv(::mesh::Packet* pkt, uint32_t ack_crc) override {
        // ACK received — handled internally by MeshCore for reliable delivery
    }

    // ── Ping Nearby ──────────────────────────────
    void onControlDataRecv(::mesh::Packet* pkt) override {
        if (!pkt || pkt->payload_len < 5) return;
        if ((pkt->payload[0] & 0x80) == 0) return;

        // Mask off the control-disco bit before comparing string payload
        uint8_t clean[32];
        size_t clen = pkt->payload_len;
        if (clen > sizeof(clean)) clen = sizeof(clean);
        memcpy(clean, pkt->payload, clen);
        clean[0] &= 0x7F;

        // PING received — respond with PONG containing our name and RSSI
        if (memcmp(clean, "PING:", 5) == 0) {
            int rssi = (int)_radio->getLastRSSI();
            // Tag is after "PING:" (hex string)
            size_t tag_start = 5;
            size_t tag_len = pkt->payload_len - tag_start;
            if (tag_len > 16) tag_len = 16;

            char pong[128];
            int n = snprintf(pong, sizeof(pong), "PONG:%.*s:%s:%d",
                             (int)tag_len, (const char*)(pkt->payload + tag_start),
                             _own_name[0] ? _own_name : "unknown",
                             rssi);
            if (n > 0 && (size_t)n <= sizeof(pong)) {
                ::mesh::Packet* resp = createRawData((uint8_t*)pong, (size_t)n);
                if (resp) {
                    resp->payload[0] |= 0x80;
                    sendZeroHop(resp);
                }
            }
            return;
        }

        // PONG received — collect if it matches our active ping
        if (memcmp(clean, "PONG:", 5) == 0 && _ping_sent_at != 0 && _ping_tag != 0) {
            if (_ping_n_results >= PING_RESULTS_MAX) return;
            uint32_t now_ms = _ms->getMillis();
            if (now_ms > _ping_sent_at + PING_WINDOW_MS) return;

            // Format: PONG:<tag>:<name>:<rssi>
            // Extract tag to verify it matches our ping
            const char* tag_end = (const char*)memchr(pkt->payload + 5, ':', pkt->payload_len - 5);
            if (!tag_end) return;

            size_t tag_len = (size_t)(tag_end - (const char*)pkt->payload - 5);
            if (tag_len == 0) return;

            // Check tag matches
            char recv_tag[20];
            if (tag_len > sizeof(recv_tag) - 1) tag_len = sizeof(recv_tag) - 1;
            memcpy(recv_tag, pkt->payload + 5, tag_len);
            recv_tag[tag_len] = '\0';

            char our_tag[20];
            snprintf(our_tag, sizeof(our_tag), "%08lx", (unsigned long)_ping_tag);
            if (strcmp(recv_tag, our_tag) != 0) return;

            // Parse name (between second ':' and third ':')
            const char* remaining = tag_end + 1;
            size_t rem_len = pkt->payload_len - (size_t)(remaining - (const char*)pkt->payload);
            const char* rssi_start = (const char*)memchr(remaining, ':', rem_len);
            if (!rssi_start) return;

            size_t name_len = (size_t)(rssi_start - remaining);
            if (name_len > 31) name_len = 31;

            PingResult& pr = _ping_results[_ping_n_results++];
            memcpy(pr.name, remaining, name_len);
            pr.name[name_len] = '\0';
            pr.rssi = atoi(rssi_start + 1);
            return;
        }
    }

    // ── Raw custom data ───────────────────────────────
    void onRawDataRecv(::mesh::Packet* pkt) override {
        // Future: handle application-specific raw data
    }

    // ── Packet-level RX logging ───────────────────────
    void logRx(::mesh::Packet* pkt, int, float) override {
        const char* tname;
        switch (pkt->getPayloadType()) {
            case PAYLOAD_TYPE_ADVERT:   tname = "ADVERT_RX"; break;
            case PAYLOAD_TYPE_ACK:      tname = "ACK";        break;
            case PAYLOAD_TYPE_TXT_MSG:  tname = "DM_RX";      break;
            case PAYLOAD_TYPE_GRP_TXT:
            case PAYLOAD_TYPE_GRP_DATA: tname = "GRP_RX";     break;
            case PAYLOAD_TYPE_ANON_REQ: tname = "ANON_RX";    break;
            case PAYLOAD_TYPE_TRACE:    tname = "TRACE";      break;
            default:                    tname = "PKT_RX";     break;
        }
        int rssi = (int)_radio->getLastRSSI();
        float snr = pkt->getSNR();
        pushPacketLog("RADIO", rssi, snr, tname);
#if SLOPOS_DEBUG_MESH
        SLOPOS_RUNTIME_FEAT(mesh) {
        Serial.printf("[rx] %s  RSSI:%ddBm SNR:%.1fdB\n",
                      tname, rssi, snr);
        }
#endif
    }

    SlopMesh(::mesh::Radio& r, ::mesh::MillisecondClock& ms, ::mesh::RNG& rng,
             ::mesh::RTCClock& rtc, ::mesh::PacketManager& mgr, ::mesh::MeshTables& tbl)
        : ::mesh::Mesh(r, ms, rng, rtc, mgr, tbl),
          _onMessage(nullptr)
    {
        _own_name[0] = '\0';
        _prefs.set_defaults();
        for (int i = 0; i < SLOP_MAX_CONTACTS; i++) {
            _contacts[i].out_path_len = OUT_PATH_UNKNOWN;
        }
    }

    bool hasTraceResult() const { return _has_trace_result; }
    uint32_t getTraceTag() const { return _last_trace_tag; }
    uint8_t  getTracePathLen() const { return _last_trace_len; }
    void getTracePath(uint8_t* snrs_out, uint8_t* hashes_out) const {
        memcpy(snrs_out, _last_trace_snrs, _last_trace_len);
        memcpy(hashes_out, _last_trace_hashes, _last_trace_len);
    }
    void clearTraceResult() { _has_trace_result = false; }

    void setMessageCallback(void (*cb)(const char* sender, const char* channel, const char* text)) { _onMessage = cb; }
    void setOwnName(const char* name) {
        if (!name) return;
        strncpy(_own_name, name, sizeof(_own_name) - 1);
        _own_name[sizeof(_own_name) - 1] = '\0';
    }

    // ── Send helpers ──────────────────────────────────
    bool sendTextTo(const char* dest_name, const char* text) {
        for (int i = 0; i < _nContacts; i++) {
            if (strcmp(_contacts[i].name, dest_name) != 0) continue;
            // Payload: [4-byte LE timestamp][1-byte flags][null-terminated text]
            // MeshCore caps encrypted payload at MAX_PACKET_PAYLOAD - 16 = 168 bytes.
            // With 5-byte header + 1-byte null, 150 chars text -> 156 bytes plaintext fits.
            static constexpr size_t MAX_PAYLOAD = 150;
            uint8_t buf[5 + MAX_PAYLOAD];
            uint32_t ts = getRTCClock()->getCurrentTime();
            memcpy(buf, &ts, 4);
            buf[4] = 0;   // TXT_TYPE_PLAIN, attempt 0
            size_t text_len = slopos::utf8_truncate_bytes(text, MAX_PAYLOAD - 1);
            memcpy(buf + 5, text, text_len);
            buf[5 + text_len] = '\0';
            size_t len = 5 + text_len + 1;
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

    void broadcastAdvert(const char* name, uint8_t adv_type = ADV_TYPE_CHAT) {
        AdvertDataBuilder builder(adv_type, name);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt);
    }

    void broadcastAdvert(const char* name, double lat, double lon, uint8_t adv_type = ADV_TYPE_CHAT) {
        AdvertDataBuilder builder(adv_type, name, lat, lon);
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

    // ── Remove contact at index ────────────────────────────
    // Compacts the array so there are no gaps. Contacts are re-learned from adverts.
    bool removeContact(int idx) {
        if (idx < 0 || idx >= _nContacts) return false;
        _nContacts--;
        for (int i = idx; i < _nContacts; i++) {
            _contacts[i] = _contacts[i + 1];
        }
        memset(&_contacts[_nContacts], 0, sizeof(SlopContact));
        return true;
    }

    // ── Channel management ────────────────────────────
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

    bool loadChannel(const uint8_t* secret, size_t secret_len, const uint8_t* hash, const char* name) {
        if (_nChannels >= SLOP_MAX_CHANNELS) return false;
        for (int i = 0; i < _nChannels; i++) {
            if (strcmp(_channels[i].name, name) == 0) return true;
        }
        SlopChannel& ch = _channels[_nChannels];
        memset(&ch, 0, sizeof(ch));
        size_t cp = secret_len < sizeof(ch.channel.secret) ? secret_len : sizeof(ch.channel.secret);
        memcpy(ch.channel.secret, secret, cp);
        memcpy(ch.channel.hash, hash, sizeof(ch.channel.hash));
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

    // ── Remove channel at index ────────────────────────────
    // Compacts the array so there are no gaps. Caller must persist after.
    bool removeChannel(int idx) {
        if (idx < 0 || idx >= _nChannels) return false;
        _nChannels--;
        for (int i = idx; i < _nChannels; i++) {
            _channels[i] = _channels[i + 1];
        }
        memset(&_channels[_nChannels], 0, sizeof(SlopChannel));
        return true;
    }

    bool sendGroupText(int channel_idx, const char* text) {
        if (channel_idx < 0 || channel_idx >= _nChannels) return false;
        if (!text || !text[0]) return false;

        // Group text payload: [4-byte LE ts][txt_type=0]["<sender_name>: <text>\0"]
        // Matches BaseChatMesh format for full interoperability.
        // MeshCore caps encrypted payload at MAX_PACKET_PAYLOAD - CIPHER_BLOCK_SIZE - 3 = 165 bytes.
        // With 5-byte header + name overhead, we leave generous room.
        static constexpr size_t MAX_GRP_PAYLOAD = 150;
        uint8_t buf[5 + MAX_GRP_PAYLOAD];
        uint32_t ts = getRTCClock()->getCurrentTime();
        memcpy(buf, &ts, 4);
        buf[4] = 0;   // text_type = 0 (plain text)

        // Build "<sender_name>: <text>" — matching BaseChatMesh wire format
        char* text_start = (char*)(buf + 5);
        size_t remaining = MAX_GRP_PAYLOAD - 1;
        int prefix_len = 0;
        if (_own_name[0]) {
            prefix_len = snprintf(text_start, remaining, "%s: ", _own_name);
            if (prefix_len < 0) prefix_len = 0;
            if ((size_t)prefix_len >= remaining) prefix_len = remaining - 1;
        }
        size_t text_len = slopos::utf8_truncate_bytes(text, remaining - (size_t)prefix_len);
        if (text_len > remaining - (size_t)prefix_len)
            text_len = remaining - (size_t)prefix_len - 1;
        memcpy(text_start + prefix_len, text, text_len);
        text_start[prefix_len + text_len] = '\0';

        size_t total = 5 + (size_t)prefix_len + text_len + 1;
        if (total > sizeof(buf)) total = sizeof(buf);

        ::mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT,
                                                   _channels[channel_idx].channel,
                                                   buf, total);
        if (!pkt) return false;
        sendFlood(pkt);
        return true;
    }

    // ── Prefs ─────────────────────────────────────────
    slopos::NodePrefs& prefs() { return _prefs; }

    // ── Ping Nearby ─────────────────────────────────
    // Send a zero-hop PING to discover nearby nodes
    bool sendPingNearby() {
        uint32_t now = _ms->getMillis();
        if (now - _ping_last_at < PING_COOLDOWN_MS) return false;

        // Generate a unique tag for this ping
        _ping_tag = (uint32_t)(now ^ (uint32_t)(intptr_t)this);
        _ping_sent_at = now;
        _ping_last_at = now;
        _ping_n_results = 0;

        char ping[20];
        int n = snprintf(ping, sizeof(ping), "PING:%08lx", (unsigned long)_ping_tag);
        if (n <= 0 || (size_t)n > sizeof(ping)) return false;

        ::mesh::Packet* pkt = createRawData((uint8_t*)ping, (size_t)n);
        if (!pkt) return false;
        pkt->payload[0] |= 0x80; // ensure control-disco bit
        sendZeroHop(pkt);
        return true;
    }

    bool pingIsActive() const {
        if (_ping_sent_at == 0) return false;
        return _ms->getMillis() < _ping_sent_at + PING_WINDOW_MS;
    }

    bool pingOnCooldown() const {
        if (_ping_last_at == 0) return false;
        return _ms->getMillis() < _ping_last_at + PING_COOLDOWN_MS;
    }

    uint32_t pingCooldownRemaining() const {
        if (_ping_last_at == 0) return 0;
        uint32_t now = _ms->getMillis();
        if (now >= _ping_last_at + PING_COOLDOWN_MS) return 0;
        return (_ping_last_at + PING_COOLDOWN_MS) - now;
    }

    // Returns ms remaining in the active 3-second ping listening window.
    // Used by the Finder screen to show countdown during active ping.
    // Returns 0 if no ping is active or the window has expired.
    uint32_t activePingRemaining() const {
        if (_ping_sent_at == 0) return 0;
        uint32_t now = _ms->getMillis();
        if (now >= _ping_sent_at + PING_WINDOW_MS) return 0;
        return (_ping_sent_at + PING_WINDOW_MS) - now;
    }

    int getPingResultCount() const { return _ping_n_results; }
    const PingResult* getPingResult(int i) const {
        return (i >= 0 && i < _ping_n_results) ? &_ping_results[i] : nullptr;
    }

    void clearPingState() {
        _ping_tag = 0;
        _ping_sent_at = 0;
        _ping_n_results = 0;
    }

    // ── Duty cycle override ──────────────────────────
    float getAirtimeBudgetFactor() const override {
        if (_duty_cycle == 0) return 1.0f;  // no limit
        // Scale: 1% → 0.01, 100% → 1.0
        return _duty_cycle / 100.0f;
    }

    void setDutyCycle(uint8_t percent) {
        _duty_cycle = percent;
    }
};

} // namespace mesh
} // namespace slopos
