// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// SlopMeshV2 — BaseChatMesh subclass for SlopOS-TDeck.
// Built alongside the original SlopMesh during Phase 0 migration.
// Once parity is proven, this replaces SlopMesh entirely.
//
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#pragma once
#include <string.h>
#include <stdlib.h>
#include <helpers/BaseChatMesh.h>
#include <SPIFFS.h>
#include "mesh_wrapper.h"
#include "hal/prefs.h"
#include "hal/tdeck_board.h"
#include "../diagnostics/debug_cfg.h"

namespace slopos {
namespace mesh {

// RSSI/SNR side-channel — BaseChatMesh::ContactInfo doesn't carry signal data
struct SignalSample {
    uint8_t key[2];
    int     rssi;
    float   snr;
    uint32_t updated_at;
};

// NOTE: PingResult is declared in mesh_wrapper.h (slopos::mesh::PingResult).

class SlopMeshV2 : public ::BaseChatMesh {
public:
    SlopMeshV2(::mesh::Radio& radio, ::mesh::MillisecondClock& clock, ::mesh::RNG& rng,
               ::mesh::RTCClock& rtc, ::mesh::PacketManager& pm, ::mesh::MeshTables& mt)
        : BaseChatMesh(radio, clock, rng, rtc, pm, mt) {}
    ~SlopMeshV2() {}

    // ── Identity & name ─────────────────────────
    char _own_name[32] = "SlopOS";

    void setOwnName(const char* name) {
        if (name) {
            strncpy(_own_name, name, sizeof(_own_name) - 1);
            _own_name[sizeof(_own_name) - 1] = '\0';
        }
    }
    const char* getOwnName() const { return _own_name; }

    // Stores the wrapper's message callback (auto-reply, queue push, etc.)
    // Called from onChannelMessageRecv after pushing to the UI queue.
    void (*_message_cb)(const char*, const char*, const char*) = nullptr;
    void setMessageCallback(void (*cb)(const char*, const char*, const char*)) {
        _message_cb = cb;
    }

    // ── RSSI/SNR side-channel ───────────────────
    static constexpr int SIGNAL_SAMPLES_MAX = 64;
    SignalSample _signal_samples[SIGNAL_SAMPLES_MAX];
    int _n_signal_samples = 0;

    void updateSignalSample(const uint8_t* pub_key, int rssi, float snr) {
        if (!pub_key) return;
        uint32_t now = getRTCClock()->getCurrentTime();
        for (int i = 0; i < _n_signal_samples; i++) {
            if (_signal_samples[i].key[0] == pub_key[0] &&
                _signal_samples[i].key[1] == pub_key[1]) {
                _signal_samples[i].rssi = rssi;
                _signal_samples[i].snr = snr;
                _signal_samples[i].updated_at = now;
                return;
            }
        }
        if (_n_signal_samples < SIGNAL_SAMPLES_MAX) {
            _signal_samples[_n_signal_samples].key[0] = pub_key[0];
            _signal_samples[_n_signal_samples].key[1] = pub_key[1];
            _signal_samples[_n_signal_samples].rssi = rssi;
            _signal_samples[_n_signal_samples].snr = snr;
            _signal_samples[_n_signal_samples].updated_at = now;
            _n_signal_samples++;
        }
    }
    int getContactRSSI(const uint8_t* pub_key) const {
        if (!pub_key) return 0;
        for (int i = 0; i < _n_signal_samples; i++)
            if (_signal_samples[i].key[0] == pub_key[0] &&
                _signal_samples[i].key[1] == pub_key[1])
                return _signal_samples[i].rssi;
        return 0;
    }
    float getContactSNR(const uint8_t* pub_key) const {
        if (!pub_key) return 0.0f;
        for (int i = 0; i < _n_signal_samples; i++)
            if (_signal_samples[i].key[0] == pub_key[0] &&
                _signal_samples[i].key[1] == pub_key[1])
                return _signal_samples[i].snr;
        return 0.0f;
    }

    // ── Trace route ─────────────────────────────
    bool     _has_trace_result = false;
    uint32_t _last_trace_tag = 0;
    uint8_t  _last_trace_len = 0;
    uint8_t  _last_trace_snrs[MAX_PATH_SIZE] = {0};
    uint8_t  _last_trace_hashes[MAX_PATH_SIZE] = {0};

    // Send a TRACE packet to a contact (by index). Requires a known direct path.
    bool sendTrace(int contact_idx, uint32_t tag) {
        ::ContactInfo c;
        if (!getContactByIdx((uint32_t)contact_idx, c)) return false;
        if (c.out_path_len == OUT_PATH_UNKNOWN) return false;
        _has_trace_result = false;
        ::mesh::Packet* pkt = createTrace(tag, 0, 0);
        if (!pkt) return false;
        sendDirect(pkt, c.out_path, c.out_path_len);
        return true;
    }

    void onTraceRecv(::mesh::Packet*, uint32_t tag, uint32_t auth_code, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes,
                     uint8_t path_len) override {
        _last_trace_tag = tag;
        if (path_len > MAX_PATH_SIZE) path_len = MAX_PATH_SIZE;
        _last_trace_len = path_len;
        memcpy(_last_trace_snrs, path_snrs, path_len);
        memcpy(_last_trace_hashes, path_hashes, path_len);
        _has_trace_result = true;
    }

    bool hasTraceResult() { return _has_trace_result; }
    uint8_t getTracePathLen() { return _last_trace_len; }
    void getTracePath(uint8_t* snrs_out, uint8_t* hashes_out) {
        memcpy(snrs_out, _last_trace_snrs, _last_trace_len);
        memcpy(hashes_out, _last_trace_hashes, _last_trace_len);
    }
    void clearTraceResult() { _has_trace_result = false; _last_trace_len = 0; }

    // ── Ping Nearby ─────────────────────────────
    static constexpr int PING_RESULTS_MAX = 32;
    static constexpr uint32_t PING_COOLDOWN_MS = 30000;
    static constexpr uint32_t PING_WINDOW_MS = 3000;

    uint32_t _ping_tag = 0;
    uint32_t _ping_sent_at = 0;
    uint32_t _ping_last_at = 0;
    int      _ping_n_results = 0;
    PingResult _ping_results[PING_RESULTS_MAX];

    // Send a zero-hop PING control packet to discover nearby nodes.
    bool sendPingNearby() {
        uint32_t now = _ms->getMillis();
        if (_ping_last_at != 0 && now - _ping_last_at < PING_COOLDOWN_MS) return false;
        _ping_tag = (uint32_t)(now ^ (uint32_t)(intptr_t)this);
        _ping_sent_at = now;
        _ping_last_at = now;
        _ping_n_results = 0;

        char ping[20];
        int n = snprintf(ping, sizeof(ping), "PING:%08lx", (unsigned long)_ping_tag);
        if (n <= 0 || (size_t)n > sizeof(ping)) return false;

        ::mesh::Packet* pkt = createRawData((uint8_t*)ping, (size_t)n);
        if (!pkt) return false;
        pkt->payload[0] |= 0x80;   // control-disco bit
        sendZeroHop(pkt);
        return true;
    }

    void onControlDataRecv(::mesh::Packet* pkt) override {
        if (!pkt || pkt->payload_len < 5) return;
        if ((pkt->payload[0] & 0x80) == 0) return;

        uint8_t clean[32];
        size_t clen = pkt->payload_len;
        if (clen > sizeof(clean)) clen = sizeof(clean);
        memcpy(clean, pkt->payload, clen);
        clean[0] &= 0x7F;

        // PING received — reply with PONG (our name + RSSI)
        if (memcmp(clean, "PING:", 5) == 0) {
            int rssi = (int)_radio->getLastRSSI();
            size_t tag_start = 5;
            size_t tag_len = pkt->payload_len - tag_start;
            if (tag_len > 16) tag_len = 16;

            char pong[128];
            int n = snprintf(pong, sizeof(pong), "PONG:%.*s:%s:%d",
                             (int)tag_len, (const char*)(pkt->payload + tag_start),
                             _own_name[0] ? _own_name : "unknown", rssi);
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

            const char* tag_end = (const char*)memchr(pkt->payload + 5, ':',
                                                      pkt->payload_len - 5);
            if (!tag_end) return;
            size_t tag_len = (size_t)(tag_end - (const char*)pkt->payload - 5);
            if (tag_len == 0) return;

            char recv_tag[20];
            if (tag_len > sizeof(recv_tag) - 1) tag_len = sizeof(recv_tag) - 1;
            memcpy(recv_tag, pkt->payload + 5, tag_len);
            recv_tag[tag_len] = '\0';

            char our_tag[20];
            snprintf(our_tag, sizeof(our_tag), "%08lx", (unsigned long)_ping_tag);
            if (strcmp(recv_tag, our_tag) != 0) return;

            const char* remaining = tag_end + 1;
            size_t rem_len = pkt->payload_len -
                             (size_t)(remaining - (const char*)pkt->payload);
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

    bool pingIsActive() {
        return _ping_sent_at > 0 && (_ms->getMillis() - _ping_sent_at) < PING_WINDOW_MS;
    }
    bool pingOnCooldown() {
        return _ping_last_at > 0 && (_ms->getMillis() - _ping_last_at) < PING_COOLDOWN_MS;
    }
    uint32_t pingCooldownRemaining() {
        if (!pingOnCooldown()) return 0;
        return PING_COOLDOWN_MS - (_ms->getMillis() - _ping_last_at);
    }
    uint32_t activePingRemaining() {
        if (!pingIsActive()) return 0;
        return PING_WINDOW_MS - (_ms->getMillis() - _ping_sent_at);
    }
    int getPingResultCount() { return _ping_n_results; }
    const PingResult* getPingResult(int i) {
        if (i < 0 || i >= _ping_n_results) return nullptr;
        return &_ping_results[i];
    }

    // ════════════════════════════════════════════════════
    //  BaseChatMesh pure virtual handlers
    // ════════════════════════════════════════════════════

    void onDiscoveredContact(::ContactInfo& contact, bool is_new,
                             uint8_t path_len, const uint8_t* path) override
    {
        int rssi = (int)_radio->getLastRSSI();
        float snr = _radio->getLastSNR();
        updateSignalSample(contact.id.pub_key, rssi, snr);
        slopos::mesh::pushPacketLog(contact.name, rssi, snr,
                                    is_new ? "ADVERT" : "ADVERT(UPDATE)");
#if SLOPOS_DEBUG_MESH
        Serial.printf("[mesh] %s contact: %s (type=%d)\n",
                      is_new ? "new" : "updated", contact.name, contact.type);
#endif
    }

    // ACK tracking is not maintained in this skeleton (parity with SlopMesh,
    // whose onAckRecv() was a no-op). Returning nullptr means "not ours".
    ::ContactInfo* processAck(const uint8_t* data) override {
        return nullptr;
    }

    void onMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                       uint32_t sender_timestamp, const char* text) override
    {
        int rssi = (int)_radio->getLastRSSI();
        float snr = pkt ? pkt->getSNR() : _radio->getLastSNR();
        updateSignalSample(contact.id.pub_key, rssi, snr);
        slopos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr);
        if (_message_cb) {
            _message_cb(contact.name, "", text);
        }
    }

    void onCommandDataRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                           uint32_t sender_timestamp, const char* text) override
    {
        char buf[288];
        snprintf(buf, sizeof(buf), "[CMD] %s: %s", contact.name, text);
        slopos::mesh::mesh_v2_queue_push(contact.name, "", buf, 0, 0.0f);
    }

    void onSignedMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                             uint32_t sender_timestamp, const uint8_t* sender_prefix,
                             const char* text) override
    {
        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        slopos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr);
        if (_message_cb) {
            _message_cb(contact.name, "", text);
        }
    }

    uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
        return 500 + (uint32_t)(16.0f * pkt_airtime_millis);
    }

    uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis,
                                        uint8_t path_len) const override {
        return 500 + (uint32_t)((pkt_airtime_millis * 6.0f + 250.0f) *
                                (float)((path_len & 63) + 1));
    }

    void onSendTimeout() override {}

    void onChannelMessageRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* pkt,
                              uint32_t timestamp, const char* text) override
    {
        int rssi = (int)_radio->getLastRSSI();
        float snr = pkt ? pkt->getSNR() : _radio->getLastSNR();

        // Resolve the channel name from the matched GroupChannel.
        char chname[32] = "[group]";
        int cidx = findChannelIdx(channel);
        if (cidx >= 0) {
            ChannelDetails cd;
            if (BaseChatMesh::getChannel(cidx, cd) && cd.name[0]) {
                strncpy(chname, cd.name, sizeof(chname) - 1);
                chname[sizeof(chname) - 1] = '\0';
            }
        }

        // text arrives as "<sender_name>: <message>" (BaseChatMesh wire format)
        const char* sender_name = text;
        const char* msg_text = "";
        const char* colon = strstr(text, ": ");
        if (colon && colon > text) {
            size_t nlen = colon - text;
            if (nlen > 31) nlen = 31;
            static char sender_buf[32];
            memcpy(sender_buf, text, nlen);
            sender_buf[nlen] = '\0';
            sender_name = sender_buf;
            msg_text = colon + 2;
        }
        slopos::mesh::mesh_v2_queue_push(sender_name, chname, msg_text, rssi, snr);
        if (_message_cb) {
            _message_cb(sender_name, chname, msg_text);
        }
    }

    uint8_t onContactRequest(const ::ContactInfo& contact, uint32_t sender_timestamp,
                             const uint8_t* data, uint8_t len, uint8_t* reply) override
    {
        return 0;  // no custom request handling in this skeleton
    }

    void onContactResponse(const ::ContactInfo& contact, const uint8_t* data,
                           uint8_t len) override {}

    void onContactPathUpdated(const ::ContactInfo& contact) override {
#if SLOPOS_DEBUG_MESH
        Serial.printf("[mesh] Path updated for %s (len=%d)\n",
                      contact.name, contact.out_path_len);
#endif
    }

    // ── SPIFFS blob persistence ─────────────────

    int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override {
        char path[48];
        snprintf(path, sizeof(path), "/blob_%02x%02x",
                 key_len > 0 ? key[0] : 0, key_len > 1 ? key[1] : 0);
        if (!SPIFFS.exists(path)) return 0;
        File f = SPIFFS.open(path, "r");
        if (!f) return 0;
        int len = f.read(dest_buf, 4096);
        f.close();
        return len;
    }

    bool putBlobByKey(const uint8_t key[], int key_len,
                       const uint8_t src_buf[], int len) override {
        char path[48];
        snprintf(path, sizeof(path), "/blob_%02x%02x",
                 key_len > 0 ? key[0] : 0, key_len > 1 ? key[1] : 0);
        if (SPIFFS.exists(path)) SPIFFS.remove(path);
        File f = SPIFFS.open(path, "w");
        if (!f) return false;
        size_t written = f.write(src_buf, len);
        f.close();
        return written == (size_t)len;
    }

    // ── Behavior overrides ──────────────────────

    bool isAutoAddEnabled() const override { return true; }
    bool shouldAutoAddContactType(uint8_t type) const override {
        return type == ADV_TYPE_CHAT || type == ADV_TYPE_ROOM || type == ADV_TYPE_NONE;
    }
    bool shouldOverwriteWhenFull() const override { return true; }
    uint8_t getAutoAddMaxHops() const override {
        return slopos::prefs_get().flood_max_hops;
    }
    void onContactsFull() override {}
    float getAirtimeBudgetFactor() const override {
        slopos::NodePrefs p = slopos::prefs_get();
        if (p.duty_cycle == 0) return -1.0f;
        return (float)p.duty_cycle / 100.0f;
    }

    // ── Compatibility API (matches SlopMesh) ──────

    void setDutyCycle(uint8_t percent) {
        slopos::NodePrefs p = slopos::prefs_get();
        p.duty_cycle = percent;
        slopos::prefs_set(p);
    }

    int getContactCount() { return getNumContacts(); }

    // Number of populated channels. BaseChatMesh keeps them contiguous from
    // slot 0; an empty name marks the end.
    int getChannelCount() {
        int n = 0;
        ChannelDetails tmp;
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
            if (!BaseChatMesh::getChannel(i, tmp)) break;
            if (tmp.name[0] == '\0') break;
            n++;
        }
        return n;
    }

    // Transient pointer accessors backed by per-instance caches. Callers use
    // the returned pointer before the next call (matching SlopMesh usage).
    ChannelDetails _ch_cache;
    const ChannelDetails* getChannel(int idx) {
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return nullptr;
        if (!BaseChatMesh::getChannel(idx, _ch_cache)) return nullptr;
        if (_ch_cache.name[0] == '\0') return nullptr;
        return &_ch_cache;
    }

    const char* getChannelName(int idx) {
        const ChannelDetails* c = getChannel(idx);
        return c ? c->name : "";
    }

    ::ContactInfo _contact_cache;
    const ::ContactInfo* getContact(int idx) {
        if (idx < 0 || idx >= getNumContacts()) return nullptr;
        if (!getContactByIdx((uint32_t)idx, _contact_cache)) return nullptr;
        return &_contact_cache;
    }

    bool removeContact(int idx) {
        ::ContactInfo tmp;
        if (!getContactByIdx((uint32_t)idx, tmp)) return false;
        return BaseChatMesh::removeContact(tmp);
    }

    bool resetPathTo(int idx) {
        ::ContactInfo tmp;
        if (!getContactByIdx((uint32_t)idx, tmp)) return false;
        // resetPathTo() mutates the passed reference, so operate on the live
        // stored contact (returned by lookupContactByPubKey), not a copy.
        ::ContactInfo* live = lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE);
        if (!live) return false;
        BaseChatMesh::resetPathTo(*live);
        return true;
    }

    bool removeChannel(int idx) {
        int n = getChannelCount();
        if (idx < 0 || idx >= n) return false;
        ChannelDetails tmp;
        for (int i = idx; i < n - 1; i++) {
            if (BaseChatMesh::getChannel(i + 1, tmp)) BaseChatMesh::setChannel(i, tmp);
        }
        ChannelDetails empty;
        memset(&empty, 0, sizeof(empty));
        BaseChatMesh::setChannel(n - 1, empty);
        return true;
    }

    // Base64 PSK decode (self-contained, matches MeshCore's alphabet).
    static int decode_b64(const char* in, size_t in_len, uint8_t* out, size_t out_cap) {
        static const char T[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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

    // bool-returning addChannel for wrapper compatibility. Inserts via
    // setChannel() at the next free slot (keeps the channel array contiguous
    // and consistent with getChannelCount()).
    bool addChannelBool(const char* name, const char* psk_base64) {
        if (!name || !name[0]) return false;
        int idx = getChannelCount();
        if (idx >= MAX_GROUP_CHANNELS) return false;
        for (int i = 0; i < idx; i++) {
            ChannelDetails t;
            if (BaseChatMesh::getChannel(i, t) && strcmp(t.name, name) == 0) return true;
        }
        ChannelDetails cd;
        memset(&cd, 0, sizeof(cd));
        int len = decode_b64(psk_base64, strlen(psk_base64),
                             cd.channel.secret, sizeof(cd.channel.secret));
        if (len != 32 && len != 16) return false;
        strncpy(cd.name, name, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        return BaseChatMesh::setChannel(idx, cd);  // setChannel recomputes hash
    }

    // Hashtag channel (no PSK): secret = sha256(name), hash = sha256(secret).
    bool addHashtagChannel(const char* name) {
        if (!name || !name[0]) return false;

        char normalized[32];
        size_t src = 0;
        while (name[src] == ' ' || name[src] == '\t') src++;
        size_t out = 0;
        if (name[src] != '#') normalized[out++] = '#';
        while (name[src] && name[src] != ' ' && name[src] != '\t' &&
               name[src] != '\r' && name[src] != '\n' && out < sizeof(normalized) - 1) {
            normalized[out++] = name[src++];
        }
        normalized[out] = '\0';
        if (out <= 1) return false;

        int idx = getChannelCount();
        if (idx >= MAX_GROUP_CHANNELS) return false;
        for (int i = 0; i < idx; i++) {
            ChannelDetails t;
            if (BaseChatMesh::getChannel(i, t) && strcmp(t.name, normalized) == 0)
                return true;
        }
        ChannelDetails cd;
        memset(&cd, 0, sizeof(cd));
        ::mesh::Utils::sha256(cd.channel.secret, CIPHER_KEY_SIZE,
                              (const uint8_t*)normalized, strlen(normalized));
        strncpy(cd.name, normalized, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        return BaseChatMesh::setChannel(idx, cd);  // setChannel recomputes hash
    }

    bool loadChannel(const uint8_t* secret, size_t secret_len,
                     const uint8_t* hash, const char* name) {
        if (!name || !name[0]) return false;
        int idx = getChannelCount();
        if (idx >= MAX_GROUP_CHANNELS) return false;
        ChannelDetails cd;
        memset(&cd, 0, sizeof(cd));
        size_t cpy = secret_len < sizeof(cd.channel.secret) ? secret_len
                                                            : sizeof(cd.channel.secret);
        memcpy(cd.channel.secret, secret, cpy);
        strncpy(cd.name, name, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        // setChannel() recomputes the hash from the secret (same derivation
        // used when the channel was created), so the stored hash is reproduced.
        return BaseChatMesh::setChannel(idx, cd);
    }

    // ── Send helpers ────────────────────────────

    bool sendTextTo(const char* name, const char* text) {
        if (!name || !text) return false;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t expected_ack = 0, est_timeout = 0;
                uint32_t ts = getRTCClock()->getCurrentTime();
                int r = BaseChatMesh::sendMessage(tmp, ts, 0, text,
                                                  expected_ack, est_timeout);
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool sendGroupText(int idx, const char* text) {
        if (idx < 0 || idx >= getChannelCount() || !text || !text[0]) return false;
        ChannelDetails cd;
        if (!BaseChatMesh::getChannel(idx, cd)) return false;
        uint32_t ts = getRTCClock()->getCurrentTime();
        return BaseChatMesh::sendGroupMessage(ts, cd.channel, _own_name, text,
                                              (int)strlen(text));
    }

    // ── Flood advert ────────────────────────────
    void broadcastAdvert(const char* name, uint8_t adv_type = ADV_TYPE_CHAT) {
        AdvertDataBuilder builder(adv_type, name);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt);
    }
    void broadcastAdvert(const char* name, double lat, double lon,
                         uint8_t adv_type = ADV_TYPE_CHAT) {
        AdvertDataBuilder builder(adv_type, name, lat, lon);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt);
    }

    float getPacketSNR() const {
        return _radio ? _radio->getLastSNR() : 0.0f;
    }

    // NOTE: airtime/packet-count stats (getTotalAirTime, getReceiveAirTime,
    // resetStats, getNumSent/RecvFlood/Direct) and getRemainingTxBudget are
    // inherited directly from mesh::Dispatcher — no overrides needed.
};

} // namespace mesh
} // namespace slopos
