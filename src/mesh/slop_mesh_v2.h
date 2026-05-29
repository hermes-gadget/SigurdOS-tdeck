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

// Ping result (matches slop_mesh.h layout)
struct PingResult {
    char name[32];
    int rssi;
};

class SlopMeshV2 : public ::BaseChatMesh {
public:
    SlopMeshV2(::mesh::Radio& radio, ::mesh::Clock& clock, ::mesh::RNG& rng,
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
    void setMessageCallback(void (*)(const char*, const char*, const char*)) {}

    // ── RSSI/SNR side-channel ───────────────────
    static constexpr int SIGNAL_SAMPLES_MAX = 64;
    SignalSample _signal_samples[SIGNAL_SAMPLES_MAX];
    int _n_signal_samples = 0;

    void updateSignalSample(const uint8_t* pub_key, int rssi, float snr) {
        if (!pub_key) return;
        for (int i = 0; i < _n_signal_samples; i++) {
            if (_signal_samples[i].key[0] == pub_key[0] &&
                _signal_samples[i].key[1] == pub_key[1]) {
                _signal_samples[i].rssi = rssi;
                _signal_samples[i].snr = snr;
                _signal_samples[i].updated_at = getCurrentTime();
                return;
            }
        }
        if (_n_signal_samples < SIGNAL_SAMPLES_MAX) {
            _signal_samples[_n_signal_samples].key[0] = pub_key[0];
            _signal_samples[_n_signal_samples].key[1] = pub_key[1];
            _signal_samples[_n_signal_samples].rssi = rssi;
            _signal_samples[_n_signal_samples].snr = snr;
            _signal_samples[_n_signal_samples].updated_at = getCurrentTime();
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
    uint8_t  _last_trace_snrs[64] = {0};
    uint8_t  _last_trace_hashes[64] = {0};

    bool sendTrace(int contact_idx, uint32_t* out_tag) {
        if (contact_idx < 0 || contact_idx >= (int)_n_contacts) return false;
        _has_trace_result = false;
        uint32_t tag = getRNG()->nextValue();
        if (out_tag) *out_tag = tag;
        return sendRequest(contacts[contact_idx], 0x04, &tag, sizeof(tag));
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

    bool sendPingNearby() {
        uint32_t now = millis();
        if (now - _ping_last_at < PING_COOLDOWN_MS) return false;
        _ping_tag = getRNG()->nextValue();
        _ping_sent_at = now;
        _ping_n_results = 0;
        return sendRequest(nullptr, 0x01, (uint8_t*)&_ping_tag, sizeof(_ping_tag));
    }
    bool pingIsActive() {
        return _ping_sent_at > 0 && (millis() - _ping_sent_at) < PING_WINDOW_MS;
    }
    bool pingOnCooldown() {
        return _ping_last_at > 0 && (millis() - _ping_last_at) < PING_COOLDOWN_MS;
    }
    uint32_t pingCooldownRemaining() {
        if (!pingOnCooldown()) return 0;
        return PING_COOLDOWN_MS - (millis() - _ping_last_at);
    }
    uint32_t activePingRemaining() {
        if (!pingIsActive()) return 0;
        return PING_WINDOW_MS - (millis() - _ping_sent_at);
    }
    int getPingResultCount() { return _ping_n_results; }
    const PingResult* getPingResult(int i) {
        if (i < 0 || i >= _ping_n_results) return nullptr;
        return &_ping_results[i];
    }

    // ── OnResponse handler for trace/ping ───────
    void onContactResponse(const ::ContactInfo& contact, const uint8_t* data,
                            uint8_t len) override
    {
        // Check for trace result
        if (len >= 1 && data[0] == 0x04 && len >= 2) {
            _has_trace_result = true;
            _last_trace_len = len - 1;
            if (_last_trace_len > 64) _last_trace_len = 64;
            for (int i = 0; i < (int)_last_trace_len; i++) {
                _last_trace_snrs[i] = data[i];
            }
            return;
        }
        // Check for ping response
        if (len >= 4) {
            uint32_t tag;
            memcpy(&tag, data, sizeof(tag));
            if (tag == _ping_tag && _ping_n_results < PING_RESULTS_MAX) {
                _ping_last_at = millis();
                PingResult& pr = _ping_results[_ping_n_results++];
                strncpy(pr.name, contact.name, sizeof(pr.name) - 1);
                pr.name[sizeof(pr.name) - 1] = '\0';
                pr.rssi = getContactRSSI(contact.id.pub_key);
            }
        }
        char buf[288];
        snprintf(buf, sizeof(buf), "[RESP] %s: %u bytes", contact.name, len);
        slopos::mesh::mesh_v2_queue_push(contact.name, "", buf, 0, 0.0f);
    }

    // ── OnRequest handler for ping ──────────────
    uint8_t onContactRequest(const ::ContactInfo& contact, uint32_t sender_timestamp,
                              const uint8_t* data, uint8_t len,
                              uint8_t* reply) override
    {
        if (len >= 4) {
            // Check for ping request (type 0x01)
            if (data[0] == 0x01 && len >= 5) {
                memcpy(reply, data + 1, 4);
                return 4;
            }
            // Check for trace request (type 0x04)
            if (data[0] == 0x04 && len >= 5) {
                memcpy(reply, data + 1, len - 1);
                return len - 1;
            }
        }
        // Telemetry request
        if (len > 0 && data[0] == 0x03) {
            slopos::NodePrefs p = slopos::prefs_get();
            if ((p.telemetry_mode & 0x01) == 0) return 0;
            float vbat = slopos::board::getBatteryVoltage();
            uint16_t mv = (uint16_t)(vbat * 1000.0f);
            reply[0] = 0x01; reply[1] = 0x76;
            reply[2] = (mv >> 8) & 0xFF; reply[3] = mv & 0xFF;
            return 4;
        }
        return 0;
    }

    // ════════════════════════════════════════════════════
    //  BaseChatMesh pure virtual handlers
    // ════════════════════════════════════════════════════

    void onDiscoveredContact(::ContactInfo& contact, bool is_new,
                             uint8_t path_len, const uint8_t* path) override
    {
        if (_radio) {
            updateSignalSample(contact.id.pub_key,
                               (int)_radio->getLastRSSI(),
                               getPacketSNR());
        }
        char log_name[32];
        strncpy(log_name, contact.name, sizeof(log_name) - 1);
        log_name[sizeof(log_name) - 1] = '\0';
        slopos::mesh::pushPacketLog(
            log_name,
            getContactRSSI(contact.id.pub_key),
            getContactSNR(contact.id.pub_key),
            is_new ? "ADVERT" : "ADVERT(UPDATE)");
#if SLOPOS_DEBUG_MESH
        Serial.printf("[mesh] %s contact: %s (type=%d)\n",
                      is_new ? "new" : "updated", contact.name, contact.type);
#endif
    }

    ::ContactInfo* processAck(const uint8_t* data) override {
        uint32_t ack_val;
        memcpy(&ack_val, data, sizeof(ack_val));
        for (int i = 0; i < 8; i++) {
            if (expected_ack_table[i].is_pending &&
                expected_ack_table[i].ack == ack_val) {
                expected_ack_table[i].is_pending = false;
                int contact_idx = expected_ack_table[i].contact_idx;
                if (contact_idx >= 0 && contact_idx < MAX_CONTACTS) {
                    if (contacts[contact_idx].id.pub_key[0] != 0) {
                        slopos::mesh::pushPacketLog(
                            contacts[contact_idx].name, 0, 0.0f, "ACK");
                        return &contacts[contact_idx];
                    }
                }
            }
        }
        return nullptr;
    }

    void onMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                       uint32_t sender_timestamp, const char* text) override
    {
        int rssi = 0;
        float snr = 0.0f;
        if (pkt) {
            rssi = (int)pkt->getRSSI();
            snr = pkt->getSNR();
            updateSignalSample(contact.id.pub_key, rssi, snr);
        } else {
            rssi = getContactRSSI(contact.id.pub_key);
            snr = getContactSNR(contact.id.pub_key);
        }
        slopos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr);
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
        int rssi = pkt ? (int)pkt->getRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        slopos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr);
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
        int rssi = pkt ? (int)pkt->getRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;

        const char* chname = nullptr;
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
            if (channels[i].channel.hash[0] == channel.hash[0]) {
                chname = channels[i].name;
                break;
            }
        }

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
        slopos::mesh::mesh_v2_queue_push(sender_name,
                                  chname ? chname : "[group]",
                                  msg_text, rssi, snr);
    }

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
    bool allowPacketForward(const ::mesh::Packet* pkt) const override { return false; }

    void onChannelDataRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* pkt,
                            uint8_t* data, size_t len) override {}

    // ── Compatibility API (matches SlopMesh) ──────

    void setDutyCycle(uint8_t percent) {
        slopos::NodePrefs p = slopos::prefs_get();
        p.duty_cycle = percent;
        slopos::prefs_set(p);
    }
    int getContactCount() { return (int)_n_contacts; }
    int getChannelCount() { return (int)_n_channels; }

    const ChannelDetails* getChannel(int idx) {
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return nullptr;
        if (channels[idx].name[0] == '\0') return nullptr;
        return &channels[idx];
    }

    bool getContact(int idx, ContactInfo& out) {
        if (idx < 0 || idx >= (int)_n_contacts) return false;
        out = contacts[idx];
        return true;
    }

    bool removeContact(int idx) {
        if (idx < 0 || idx >= (int)_n_contacts) return false;
        removeContact(contacts[idx].id.pub_key);
        return true;
    }

    bool resetPathTo(int idx) {
        if (idx < 0 || idx >= (int)_n_contacts) return false;
        return resetPathTo(contacts[idx]);
    }

    bool removeChannel(int idx) {
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;
        memset(&channels[idx], 0, sizeof(ChannelDetails));
        return true;
    }

    const char* getChannelName(int idx) {
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return "";
        return channels[idx].name;
    }

    bool loadChannel(const uint8_t* secret, size_t secret_len,
                     const uint8_t* hash, const char* name) {
        if (!name || !name[0]) return false;
        int slot = -1;
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
            if (channels[i].name[0] == '\0') { slot = i; break; }
        }
        if (slot < 0) return false;
        ChannelDetails& cd = channels[slot];
        strncpy(cd.name, name, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        memcpy(cd.channel.hash, hash, sizeof(cd.channel.hash));
        if (secret && secret_len > 0) {
            size_t cpy = secret_len < sizeof(cd.channel.secret) ? secret_len : sizeof(cd.channel.secret);
            memcpy(cd.channel.secret, secret, cpy);
        }
        _n_channels++;
        return true;
    }

    // Stats passthrough
    unsigned long getTotalAirTime() { return _radio ? _radio->getTotalAirTime() : 0; }
    unsigned long getReceiveAirTime() { return _radio ? _radio->getReceiveAirTime() : 0; }
    void resetStats() { if (_radio) _radio->resetStats(); }
    uint32_t getNumSentFlood() { return _radio ? _radio->getNumSentFlood() : 0; }
    uint32_t getNumSentDirect() { return _radio ? _radio->getNumSentDirect() : 0; }
    uint32_t getNumRecvFlood() { return _radio ? _radio->getNumRecvFlood() : 0; }
    uint32_t getNumRecvDirect() { return _radio ? _radio->getNumRecvDirect() : 0; }

    // Single-arg getContact (matches SlopMesh API)
    const ContactInfo* getContact(int idx) {
        if (idx < 0 || idx >= (int)_n_contacts) return nullptr;
        if (contacts[idx].id.pub_key[0] == 0) return nullptr;
        return &contacts[idx];
    }

    // sendTrace by value (wrapper passes tag, not ptr)
    bool sendTrace(int contact_idx, uint32_t tag) {
        return sendTrace(contact_idx, &tag);
    }

    // bool-returning addChannel for wrapper compatibility
    bool addChannelBool(const char* name, const char* psk_base64) {
        return BaseChatMesh::addChannel(name, psk_base64) != nullptr;
    }

    // Hashtag channel (no PSK)
    bool addHashtagChannel(const char* name) {
        if (!name || !name[0]) return false;
        ChannelDetails* cd = BaseChatMesh::addChannel(name, "");
        return cd != nullptr;
    }

    // Flood advert
    void broadcastAdvert(const char* name, uint8_t type) {
        ::mesh::Packet* pkt = createSelfAdvert(name);
        if (pkt) sendFloodScoped(pkt);
        delete pkt;
    }
    void broadcastAdvert(const char* name, double lat, double lon, uint8_t type) {
        ::mesh::Packet* pkt = createSelfAdvert(name, lat, lon);
        if (pkt) sendFloodScoped(pkt);
        delete pkt;
    }

    // Remaining TX budget (duty cycle)
    uint32_t getRemainingTxBudget() {
        if (!_radio) return 0;
        return _radio->getRemainingTxBudget(getAirtimeBudgetFactor());
    }

    float getPacketSNR() const {
        if (_radio) return _radio->getSNR();
        return 0.0f;
    }
};

} // namespace mesh
} // namespace slopos
