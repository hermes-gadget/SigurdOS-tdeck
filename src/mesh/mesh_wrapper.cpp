// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// MeshCore protocol integration using SigurdMesh (minimal Mesh subclass).
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#include "mesh_wrapper.h"
#include "channel_validation.h"
#include "public_channel.h"
#include "message_store.h"
#include "comms/companion_bridge.h"
#include "comms/observed_ble_interface.h"
#include "hal/tdeck_board.h"
#include "hal/tdeck_pins.h"
#include "hal/gps.h"
#include "hal/prefs.h"
#include "sigurd_mesh_v2.h"
#include "regions.h"
#include "../diagnostics/debug_cfg.h"
#include <helpers/sensors/LPPDataHelpers.h>

// REQ_TYPE constants not defined in core BaseChatMesh.h (only in examples)
#ifndef REQ_TYPE_GET_TELEMETRY_DATA
#define REQ_TYPE_GET_TELEMETRY_DATA  0x03
#endif

#include <SPIFFS.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <time.h>
#include <Mesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>

using sigurdos::mesh::MeshMessage;

// ════════════════════════════════════════════════════
// Global objects
// ════════════════════════════════════════════════════

static sigurdos::TDeckBoard        board;
static SPIClass                  lora_spi(FSPI);
static Module*                   lora_mod = nullptr;
static CustomSX1262*             radio_module = nullptr;
static CustomSX1262Wrapper*      radio_driver = nullptr;
static bool                      radio_inited = false;
static ESP32RTCClock             fallback_clock;
static AutoDiscoverRTCClock      rtc_clock(fallback_clock);
static StdRNG                    fast_rng;
static SimpleMeshTables          tables;
static ArduinoMillis             millis_clock;
static StaticPoolPacketManager   pkt_mgr(16);
static TransportKeyStore        g_region_store;
using sigurdos::mesh::SigurdMeshV2;
using mesh_impl_t = sigurdos::mesh::SigurdMeshV2;
static mesh_impl_t*   g_mesh = nullptr;

static bool initialized = false;
static char own_name[32] = "SigurdOS";
static uint32_t last_advert_time = 0;
static bool     last_advert_success = false;
static bool     last_advert_used_gps = false;

static void formatDmConversation(char* out, size_t out_size, const char* name)
{
    if (!out || out_size == 0) return;

    static constexpr char prefix[] = "DM: ";
    size_t pos = 0;
    for (const char* p = prefix; *p && pos + 1 < out_size; ++p) {
        out[pos++] = *p;
    }

    const char* src = name ? name : "";
    while (*src && pos + 1 < out_size) {
        out[pos++] = *src++;
    }
    out[pos] = '\0';
}

// ════════════════════════════════════════════════════
// Message queue
// ════════════════════════════════════════════════════

static constexpr int MAX_QUEUED = 64;
static MeshMessage   msg_buf[MAX_QUEUED];
static int           msg_head = 0, msg_tail = 0, msg_count = 0;

// Drop counter — incremented when the queue is full and a message is lost.
// Check this to detect silent packet loss (the worst kind of regression).
static uint32_t      msg_drop_count = 0;
// Unread message count — incremented on every incoming (non-self) message,
// reset to 0 when the chat screen is opened. Used by the home screen badge.
static int           unread_count = 0;

#include "companion_adapter.inc"

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE && \
    defined(SIGURDOS_COMPANION_BLE_VALIDATION) && SIGURDOS_COMPANION_BLE_VALIDATION
static constexpr const char* BLE_VALIDATION_LOG_PATH = "/ble_hw.txt";
static uint32_t ble_validation_last_log_ms = 0;

static void bleValidationAppendLine(const char* line)
{
    if (!line) return;
    File f = SPIFFS.open(BLE_VALIDATION_LOG_PATH, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
}

static void bleValidationEmit(bool force)
{
    uint32_t now = millis();
    if (!force && (uint32_t)(now - ble_validation_last_log_ms) < 5000u) return;
    ble_validation_last_log_ms = now;

    const sigurdos::comms::BleSerialObserverStats s = g_ble_serial.stats();
    char line[320];
    snprintf(line, sizeof(line),
             "@ble_hw|ms=%lu|begun=%u|en=%u|conn=%u|adv=%u|authok=%lu|authfail=%lu|connect=%lu|disconnect=%lu|mtu=%u|rxw=%lu|rxd=%lu|rx=%lu|tx=%lu|txd=%lu|lrx=%u|ltx=%u",
             (unsigned long)now,
             s.begun ? 1u : 0u,
             s.enabled ? 1u : 0u,
             s.connected ? 1u : 0u,
             s.advertising_expected ? 1u : 0u,
             (unsigned long)s.auth_success_count,
             (unsigned long)s.auth_failure_count,
             (unsigned long)s.connect_count,
             (unsigned long)s.disconnect_count,
             (unsigned int)s.last_mtu,
             (unsigned long)s.ble_write_count,
             (unsigned long)s.ble_write_drop_count,
             (unsigned long)s.rx_frame_count,
             (unsigned long)s.tx_frame_count,
             (unsigned long)s.tx_drop_count,
             (unsigned int)s.last_rx_code,
             (unsigned int)s.last_tx_code);
    Serial.println(line);
    bleValidationAppendLine(line);
}

static void bleValidationStartLog()
{
    SPIFFS.remove(BLE_VALIDATION_LOG_PATH);
    bleValidationAppendLine("[ble-validation] log-start");
    bleValidationEmit(true);
}
#elif defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
static void bleValidationEmit(bool) {}
static void bleValidationStartLog() {}
#else
static void bleValidationEmit(bool) {}
#endif

// Non-static overload for SigurdMeshV2 — takes RSSI/SNR from caller context
// (SigurdMeshV2 has packet context when calling, while the static queue_push
//  reads from radio_driver which may not reflect the correct packet.)
// Defined with its qualified name to match the declaration in mesh_wrapper.h
// (sigurdos::mesh) — SigurdMeshV2 calls it as sigurdos::mesh::mesh_v2_queue_push().
void sigurdos::mesh::mesh_v2_queue_push(const char* sender, const char* channel,
                         const char* text, int rssi, float snr,
                         uint32_t sender_timestamp, uint8_t path_len) {
    if (!sender || !text) return;
    if (msg_count >= MAX_QUEUED) {
        msg_drop_count++;
#if SIGURDOS_DEBUG_MESH
        SIGURDOS_RUNTIME_FEAT(mesh) {
        Serial.printf("[mesh] WARN: message queue full — dropping msg from %s (%lu dropped so far)\n",
                      sender, (unsigned long)msg_drop_count);
        }
#endif
        return;
    }
    MeshMessage& m = msg_buf[msg_head];
    strncpy(m.sender, sender, sizeof(m.sender) - 1);
    m.sender[sizeof(m.sender) - 1] = '\0';
    strncpy(m.channel, channel ? channel : "", sizeof(m.channel) - 1);
    m.channel[sizeof(m.channel) - 1] = '\0';
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.timestamp = rtc_clock.getCurrentTime();
    m.is_self = false;
    if (strcmp(sender, own_name) != 0) unread_count++;
    msg_head = (msg_head + 1) % MAX_QUEUED;
    msg_count++;
    const char* ptype = (channel && channel[0]) ? "CHANNEL" : "DM";
    sigurdos::mesh::pushPacketLog(sender, rssi, snr, ptype);
    storeIncomingMessageForCompanion(sender, channel, text, rssi, snr,
                                     sender_timestamp, path_len);
#if SIGURDOS_DEBUG_MESH
    SIGURDOS_RUNTIME_FEAT(mesh) {
    Serial.printf("[mesh] MSG from %s%s%s: %s  (RSSI:%ddBm SNR:%.1fdB)\n",
                  sender, channel && channel[0] ? " in " : "",
                  channel && channel[0] ? channel : "", text, rssi, snr);
    }
#endif
}

void sigurdos::mesh::mesh_v2_notify_send_confirmed(uint32_t ack, uint32_t trip_time_ms) {
    // Only forward if the bridge already exists — never allocate it here just to
    // report an ACK (it is created lazily on the first incoming/companion path).
    if (g_companion_bridge_ptr) {
        g_companion_bridge_ptr->notifySendConfirmed(ack, trip_time_ms);
    }
}

void sigurdos::mesh::mesh_v2_group_data_push(uint8_t channel_index,
                              uint8_t path_len,
                              int8_t snr_quarters,
                              uint16_t data_type,
                              const uint8_t* data,
                              size_t data_len) {
    if (data_len > sigurdos::comms::SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) return;
    if (CompanionBridge* b = companionBridge()) {
        b->enqueueChannelData(channel_index, snr_quarters, path_len, data_type, data, data_len);
    }
}

static void queue_push(const char* sender, const char* channel, const char* text) {
    if (msg_count >= MAX_QUEUED) {
        msg_drop_count++;
#if SIGURDOS_DEBUG_MESH
        SIGURDOS_RUNTIME_FEAT(mesh) {
        Serial.printf("[mesh] WARN: message queue full — dropping msg from %s (%lu dropped so far)\n",
                      sender ? sender : "?", (unsigned long)msg_drop_count);
        }
#endif
        return;
    }
    MeshMessage& m = msg_buf[msg_head];
    if (!sender) sender = "";
    strncpy(m.sender, sender, sizeof(m.sender) - 1);
    m.sender[sizeof(m.sender) - 1] = '\0';
    strncpy(m.channel, channel ? channel : "", sizeof(m.channel) - 1);
    m.channel[sizeof(m.channel) - 1] = '\0';
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.timestamp = rtc_clock.getCurrentTime();
    m.is_self = false;
    // Increment unread count for incoming messages (reset when chat is opened)
    if (strcmp(sender, own_name) != 0) unread_count++;
    msg_head = (msg_head + 1) % MAX_QUEUED;
    msg_count++;
    // Log as packet entry (accessible via Packets screen)
    if (sender && sender[0] && g_mesh) {
        int rssi = (int)radio_driver->getLastRSSI();
        float snr = radio_driver->getLastSNR();
        const char* ptype = (channel && channel[0]) ? "CHANNEL" : "DM";
        sigurdos::mesh::pushPacketLog(sender, rssi, snr, ptype);
    }
}

static bool queue_pop(MeshMessage* out) {
    if (msg_count == 0) return false;
    *out = msg_buf[msg_tail];
    msg_tail = (msg_tail + 1) % MAX_QUEUED;
    msg_count--;
    return true;
}

// Forward declarations
namespace sigurdos { namespace mesh { bool sendChannelMessage(const char* channel_name, const char* text); }}

static void onMeshMessage(const char* sender, const char* channel, const char* text) {
    queue_push(sender, channel, text);
#if SIGURDOS_DEBUG
    // Auto-reply in debug mode to test full duplex
    if (channel && channel[0]) {
        char reply[160];
        snprintf(reply, sizeof(reply), "%s: Roger that (%s)", own_name, text);
        sigurdos::mesh::sendChannelMessage(channel, reply);
    }
#endif
#if SIGURDOS_DEBUG_MESH
    SIGURDOS_RUNTIME_FEAT(mesh) {
    int rssi = (int)radio_driver->getLastRSSI();
    float snr = radio_driver->getLastSNR();
    Serial.printf("[mesh] MSG from %s%s%s: %s  (RSSI:%ddBm SNR:%.1fdB)\n",
                  sender, channel && channel[0] ? " in " : "",
                  channel && channel[0] ? channel : "", text, rssi, snr);
    }
#endif
}

// ════════════════════════════════════════════════════
// Identity persistence
// ════════════════════════════════════════════════════

static bool loadIdentity(::mesh::LocalIdentity& id) {
    if (!SPIFFS.exists("/mesh_id")) return false;
    File f = SPIFFS.open("/mesh_id", "r");
    if (!f) return false;
    uint8_t buf[128];
    int len = f.read(buf, sizeof(buf));
    f.close();
    if (len != PRV_KEY_SIZE && len != (PRV_KEY_SIZE + PUB_KEY_SIZE)) {
        // Corrupt or partial file — delete it and regenerate
        SPIFFS.remove("/mesh_id");
        return false;
    }
    id.readFrom(buf, len);
    // validatePrivateKey expects raw 64-byte prv_key — MeshCore serializes prv_key first
    return ::mesh::LocalIdentity::validatePrivateKey(buf);
}

static void saveIdentity(::mesh::LocalIdentity& id) {
    uint8_t buf[128];
    size_t len = id.writeTo(buf, sizeof(buf));
    File f = SPIFFS.open("/mesh_id", "w");
    if (!f) return;
    size_t written = f.write(buf, len);
    f.close();
    if (written != len) {
        // Partial write — file may be corrupted, remove it
        SPIFFS.remove("/mesh_id");
    }
}

// ════════════════════════════════════════════════════
// ACK tracking bridge
// ════════════════════════════════════════════════════
static constexpr int MAX_ACKED = 32;
struct AckedMsg {
    char dest[32];
    uint32_t timestamp;
};
static AckedMsg _acked_msgs[MAX_ACKED];
static int _acked_head = 0;
static int _acked_count = 0;
static int _ack_counter = 0;   // bumped on each registerAckedMessage() — used by UI to detect new ACKs

static uint32_t _last_telemetry_tag = 0;
static sigurdos::mesh::TelemetryResult _cached_telemetry;
static bool _has_cached_telemetry = false;

// ── Status request tracking (Phase 4.2) ───────
static uint32_t _last_status_tag = 0;
static sigurdos::mesh::NodeStatus _cached_status;
static bool _has_cached_status = false;

// Parse a 56-byte RepeaterStats blob into NodeStatus struct
static void parse_status_blob(const uint8_t* data, uint8_t len, sigurdos::mesh::NodeStatus* out) {
    if (!data || !out) return;
    memset(out, 0, sizeof(*out));
    uint8_t avail = len < NODE_STATUS_RESPONSE_SIZE ? len : NODE_STATUS_RESPONSE_SIZE;
    // data[0..3] = tag, skip that; status blob starts at data[4]
    const uint8_t* blob = data + 4;
    uint8_t blen = avail > 4 ? avail - 4 : 0;
    if (blen < 2) return;  // need at least batt_milli_volts
    unsigned ofs = 0;
    auto r16 = [&](int16_t* dst) { if (ofs + 2 <= blen) { memcpy(dst, blob + ofs, 2); ofs += 2; } };
    auto ru16 = [&](uint16_t* dst) { if (ofs + 2 <= blen) { memcpy(dst, blob + ofs, 2); ofs += 2; } };
    auto ru32 = [&](uint32_t* dst) { if (ofs + 4 <= blen) { memcpy(dst, blob + ofs, 4); ofs += 4; } };
    ru16(&out->batt_milli_volts);
    ru16(&out->curr_tx_queue_len);
    r16(&out->noise_floor);
    r16(&out->last_rssi);
    ru32(&out->n_packets_recv);
    ru32(&out->n_packets_sent);
    ru32(&out->total_air_time_secs);
    ru32(&out->total_up_time_secs);
    ru32(&out->n_sent_flood);
    ru32(&out->n_sent_direct);
    ru32(&out->n_recv_flood);
    ru32(&out->n_recv_direct);
    ru16(&out->err_events);
    r16(&out->last_snr);
    ru16(&out->n_direct_dups);
    ru16(&out->n_flood_dups);
    ru32(&out->total_rx_air_time_secs);
    ru32(&out->n_recv_errors);
}

// ════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════

namespace sigurdos {
namespace mesh {

namespace {

bool hasPublicChannel()
{
    if (!g_mesh) return false;
    for (int i = 0; i < g_mesh->getChannelCount(); i++) {
        const ChannelDetails* ch = g_mesh->getChannel(i);
        if (ch && isPublicChannelName(ch->name)) return true;
    }
    return false;
}

bool ensurePublicChannelPresent(bool persist)
{
    if (!g_mesh) return false;
    if (hasPublicChannel()) return true;

    bool ok = g_mesh->addChannelBool(PUBLIC_CHANNEL_NAME, PUBLIC_CHANNEL_PSK_BASE64);
    if (ok) {
#if SIGURDOS_DEBUG_MESH
        Serial.println("[mesh] Added missing Public channel");
#endif
        if (persist) saveChannels();
    } else {
        Serial.println("[mesh] WARNING: Public channel missing and channel list is full");
    }
    return ok;
}

} // namespace

// ── Packet log ────────────────────────────────────
static constexpr int MAX_PACKET_LOG = 50;
static PacketLogEntry pkt_log[MAX_PACKET_LOG];
static int pkt_log_head = 0;
static int pkt_log_count = 0;

void pushPacketLog(const char* source, int rssi, float snr, const char* type) {
    if (!source || !type) return;
    PacketLogEntry& e = pkt_log[pkt_log_head];
    e.timestamp = getCurrentTime();
    strncpy(e.source, source, sizeof(e.source) - 1);
    e.source[sizeof(e.source) - 1] = '\0';
    e.rssi = rssi;
    e.snr = snr;
    strncpy(e.type, type, sizeof(e.type) - 1);
    e.type[sizeof(e.type) - 1] = '\0';
    pkt_log_head = (pkt_log_head + 1) % MAX_PACKET_LOG;
    if (pkt_log_count < MAX_PACKET_LOG) pkt_log_count++;
}

// ── ACK tracking bridge ──────────────────────
void registerAckedMessage(const char* dest, uint32_t ts) {
    if (!dest) return;
    AckedMsg& a = _acked_msgs[_acked_head];
    strncpy(a.dest, dest, sizeof(a.dest) - 1);
    a.dest[sizeof(a.dest) - 1] = '\0';
    a.timestamp = ts;
    _acked_head = (_acked_head + 1) % MAX_ACKED;
    if (_acked_count < MAX_ACKED) _acked_count++;
    _ack_counter++;
    char conversation[sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN];
    formatDmConversation(conversation, sizeof(conversation), dest);
    sigurdos::mesh::messageStoreMarkAcked(conversation, ts);
#if SIGURDOS_DEBUG_MESH
    Serial.printf("[mesh] ACK for %s (ts=%lu) — %d total tracked\n", dest, (unsigned long)ts, _acked_count);
#endif
}

bool isMessageAcked(const char* dest, uint32_t ts) {
    if (!dest) return false;
    int i = _acked_head;
    for (int c = 0; c < _acked_count; c++) {
        i--;
        if (i < 0) i = MAX_ACKED - 1;
        if (_acked_msgs[i].timestamp == ts && strcmp(_acked_msgs[i].dest, dest) == 0) return true;
    }
    return false;
}

int getAckCounter() {
    return _ack_counter;
}

// ── REQ/RESPONSE framework (Phase 4.1) ────────
bool sendRequest(const char* dest_name, uint8_t req_type) {
    if (!g_mesh || !dest_name) return false;
    return g_mesh->sendRequest(dest_name, req_type);
}

bool sendRequestWithData(const char* dest_name, const uint8_t* data, uint8_t len) {
    if (!g_mesh || !dest_name || !data) return false;
    return g_mesh->sendRequestWithData(dest_name, data, len);
}

int getResponseCount() {
    return g_mesh ? g_mesh->getResponseCount() : 0;
}

bool getResponse(int idx, uint32_t* out_tag, uint8_t* out_data, uint8_t* out_len, char* out_contact_name) {
    if (!g_mesh) return false;
    auto* re = g_mesh->getResponse(idx);
    if (!re) return false;
    if (out_tag) *out_tag = re->tag;
    if (out_data && re->len > 0) memcpy(out_data, re->data, re->len);
    if (out_len) *out_len = re->len;
    if (out_contact_name) {
        strncpy(out_contact_name, re->contact_name, 31);
        out_contact_name[31] = '\0';
    }
    return true;
}

void clearResponses() {
    if (g_mesh) g_mesh->clearResponses();
}

// ── Room message fetch (Phase 4.6) ───────────────────
bool sendRoomMsgFetchRequest(const char* contact_name, const char* channel_name) {
    return g_mesh ? g_mesh->sendRoomMsgFetchRequest(contact_name, channel_name) : false;
}

int getRoomMsgFetchCount() {
    return g_mesh ? g_mesh->getRoomMsgFetchCount() : 0;
}

bool getRoomMsgFetchEntry(int index, char* sender_out, int sender_sz,
                          char* text_out, int text_sz,
                          char* channel_out, int channel_sz,
                          uint32_t* timestamp_out) {
    if (!g_mesh) return false;
    auto* e = g_mesh->getRoomMsgFetchEntry(index);
    if (!e || !e->valid) return false;
    if (sender_out && sender_sz > 0) {
        strncpy(sender_out, e->sender, sender_sz - 1);
        sender_out[sender_sz - 1] = '\0';
    }
    if (text_out && text_sz > 0) {
        strncpy(text_out, e->text, text_sz - 1);
        text_out[text_sz - 1] = '\0';
    }
    if (channel_out && channel_sz > 0) {
        strncpy(channel_out, e->channel, channel_sz - 1);
        channel_out[channel_sz - 1] = '\0';
    }
    if (timestamp_out) *timestamp_out = e->timestamp;
    return true;
}

void clearRoomMsgFetch() {
    if (g_mesh) g_mesh->clearRoomMsgFetch();
}

// ── Status request (Phase 4.2) ────────────────
bool requestStatus(const char* dest_name) {
    if (!g_mesh || !dest_name || !dest_name[0]) return false;
    bool ok = g_mesh->sendRequest(dest_name, REQ_TYPE_GET_STATUS);
    if (ok) {
        // Find the tag from the pending request table
        for (int i = 0; i < SigurdMeshV2::MAX_PENDING_REQUESTS; i++) {
            if (g_mesh->_pending_reqs[i].in_use &&
                strcmp(g_mesh->_pending_reqs[i].dest_name, dest_name) == 0) {
                _last_status_tag = g_mesh->_pending_reqs[i].tag;
                break;
            }
        }
    }
    return ok;
}

bool hasStatusResponse() {
    if (!g_mesh || _last_status_tag == 0) return false;
    int n = g_mesh->getResponseCount();
    for (int i = 0; i < n; i++) {
        auto* re = g_mesh->getResponse(i);
        if (re && re->tag == _last_status_tag) {
            parse_status_blob(re->data, re->len, &_cached_status);
            _has_cached_status = true;
            return true;
        }
    }
    return false;
}

bool getStatusResult(NodeStatus* out) {
    if (!out || !_has_cached_status) return false;
    memcpy(out, &_cached_status, sizeof(*out));
    return true;
}

// ── Telemetry queries (Phase 4.3) ────────────
bool requestTelemetry(const char* dest_name) {
    if (!g_mesh || !dest_name || !dest_name[0]) return false;
    bool ok = g_mesh->sendRequest(dest_name, REQ_TYPE_GET_TELEMETRY_DATA);
    if (ok) {
        for (int i = 0; i < SigurdMeshV2::MAX_PENDING_REQUESTS; i++) {
            if (g_mesh->_pending_reqs[i].in_use &&
                strcmp(g_mesh->_pending_reqs[i].dest_name, dest_name) == 0) {
                _last_telemetry_tag = g_mesh->_pending_reqs[i].tag;
                break;
            }
        }
    }
    return ok;
}

bool hasTelemetryResponse() {
    if (!g_mesh || _last_telemetry_tag == 0) return false;
    int n = g_mesh->getResponseCount();
    for (int i = 0; i < n; i++) {
        auto* re = g_mesh->getResponse(i);
        if (re && re->tag == _last_telemetry_tag) {
            // Parse CayenneLPP from response body (skip 4-byte tag)
            TelemetryResult result;
            memset(&result, 0, sizeof(result));
            if (re->len > 4) {
                LPPReader reader(re->data + 4, re->len - 4);
                uint8_t ch, type;
                int idx = 0;
                while (reader.readHeader(ch, type) && idx < MAX_TELEMETRY_ITEMS) {
                    TelemetryItem& item = result.items[result.n_items++];
                    item.channel = ch;
                    item.type = type;
                    switch (type) {
                        case LPP_VOLTAGE: {
                            float v;
                            reader.readVoltage(v);
                            item.value_float = v;
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "%.2fV", v);
                            break;
                        }
                        case LPP_TEMPERATURE: {
                            float t;
                            reader.readTemperature(t);
                            item.value_float = t;
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "%.1fC", t);
                            break;
                        }
                        case LPP_RELATIVE_HUMIDITY: {
                            float h;
                            reader.readRelativeHumidity(h);
                            item.value_float = h;
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "%.0f%%", h);
                            break;
                        }
                        case LPP_BAROMETRIC_PRESSURE: {
                            float p;
                            reader.readPressure(p);
                            item.value_float = p;
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "%.1f hPa", p);
                            break;
                        }
                        case LPP_GPS: {
                            float lat, lon, alt;
                            reader.readGPS(lat, lon, alt);
                            item.value_float = lat;
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "%.4f, %.4f %.0fm", lat, lon, alt);
                            break;
                        }
                        case LPP_CURRENT: {
                            float a;
                            reader.readCurrent(a);
                            item.value_float = a;
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "%.3fA", a);
                            break;
                        }
                        default:
                            reader.skipData(type);
                            snprintf(item.value_str, sizeof(item.value_str),
                                     "type=%d", type);
                            break;
                    }
                }
            }
            _cached_telemetry = result;
            _has_cached_telemetry = true;
            return true;
        }
    }
    return false;
}

bool getTelemetryResult(TelemetryResult* out) {
    if (!out || !_has_cached_telemetry) return false;
    memcpy(out, &_cached_telemetry, sizeof(*out));
    return true;
}

// ── Path discovery (Phase 4.4) ────────────────
uint32_t discoverPath(const char* dest_name) {
    if (!g_mesh || !dest_name || !dest_name[0]) return 0;
    return g_mesh->sendPathDiscovery(dest_name);
}

bool hasPathTo(const char* dest_name) {
    if (!g_mesh || !dest_name) return false;
    return g_mesh->getPathLen(dest_name) != OUT_PATH_UNKNOWN;
}

uint8_t getContactPathLen(const char* dest_name) {
    if (!g_mesh || !dest_name) return OUT_PATH_UNKNOWN;
    return g_mesh->getPathLen(dest_name);
}

// Inject a simulated message into the queue (for remote test mode — no radio)
// All functions in this file that access the radio check for g_mesh == nullptr,
// so this is safe to call without radio initialisation.
void injectMessage(const char* sender, const char* channel, const char* text)
{
    if (!sender || !text) return;
    queue_push(sender, channel, text);
    if (!channel || channel[0] == '\0') {
        pushPacketLog(sender, -50, 8.0f, "DM");
    } else {
        pushPacketLog(sender, -50, 8.0f, "CHANNEL");
    }
#if SIGURDOS_DEBUG_MESH
    SIGURDOS_RUNTIME_FEAT(mesh) {
    Serial.printf("[test] injected msg from %s%s%s: %s\n",
                  sender, channel && channel[0] ? " in " : "",
                  channel && channel[0] ? channel : "", text);
    }
#endif
}

bool init(bool spiffs_ok)
{
    // Initialize the mesh-layer board instance. The main.cpp board runs
    // begin() earlier in boot for peripheral power/I2C, but this instance
    // is the one used by the radio driver (via radio_driver → RadioLibWrapper
    // → _board->getStartupReason()). Without begin(), the deep-sleep-wake
    // from DIO1 path is never triggered, causing LoRa packets received
    // during deep sleep to be silently dropped.
    board.begin();

    // Always initialise clock even in remote-test mode
    fallback_clock.begin();
    rtc_clock.begin(Wire);

#if !defined(SIGURDOS_REMOTE_TEST) || defined(SIGURDOS_REMOTE_TEST_RADIO)
    // Delayed allocation of RadioLib Module and radio driver objects.
    // These were previously allocated at file scope (static init time),
    // before PSRAM was available. On ESP32 Arduino builds, a failed
    // `new` returns nullptr (no exceptions). Delaying to init() time
    // and checking null avoids a silent crash when the radio starts.
    lora_mod = new Module(P_LORA_NSS, P_LORA_DIO_1,
                          P_LORA_RESET, P_LORA_BUSY, lora_spi);
    if (!lora_mod) {
        Serial.println("[mesh] FATAL: Radio Module allocation failed (OOM)");
        return false;
    }
    radio_module = new CustomSX1262(lora_mod);
    if (!radio_module) {
        Serial.println("[mesh] FATAL: CustomSX1262 allocation failed (OOM)");
        return false;
    }
    radio_driver = new CustomSX1262Wrapper(*radio_module, board);
    if (!radio_driver) {
        Serial.println("[mesh] FATAL: Radio driver allocation failed (OOM)");
        return false;
    }

    // Mark clock as initialized so getCurrentTime() and other time APIs
    // work even when the radio hasn't been configured yet.
    initialized = true;

    // ── Radio configuration: use compile-time defaults if not configured ──
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    float   freq     = p.configured ? p.freq  : LORA_FREQ;
    float   bw       = p.configured ? p.bw    : LORA_BW;
    int     sf       = p.configured ? p.sf    : LORA_SF;
    int     cr       = p.configured ? p.cr    : LORA_CR;
    int     tx_power = p.configured ? p.tx_power_dbm : LORA_TX_PWR;

#if SIGURDOS_DEBUG
    // Debug builds: override NVS with compile-time radio defaults.
    // This ensures consistent behavior regardless of stale NVS values
    // from previous firmware versions or manual configuration.
    freq = LORA_FREQ;
    bw   = LORA_BW;
    sf   = LORA_SF;
    cr   = LORA_CR;
    tx_power = LORA_TX_PWR;
    Serial.println("[mesh] DEBUG — forcing compile-time radio params");
#endif

    if (!p.configured) {
#if SIGURDOS_DEBUG_MESH
        Serial.println("[mesh] Using compile-time defaults — open Settings to customize");
#endif
    }

    // If still not configured (non-debug builds), keep SX1262 off.
    // In debug builds, the SIGURDOS_DEBUG block below saves configured=true
    // to NVS — but it can only do that if we don't early-return here.
    // Debug builds always init the radio with compile-time defaults.
#if !SIGURDOS_DEBUG
    {
        const auto& cp = sigurdos::prefs_get();
        if (!cp.configured) {
            Serial.println("[mesh] Radio not configured — holding SX1262 in reset");
            pinMode(P_LORA_RESET, OUTPUT);
            digitalWrite(P_LORA_RESET, LOW);
            return true;
        }
    }
#endif

    // ── SX1262 hard reset: radio may retain state across ESP32 reboots.
    //     If BUSY pin is stuck HIGH from a previous crash, std_init() hangs
    //     in waitForBusyPin() → watchdog reset → infinite bootloop.
    //     Solution: assert RST LOW for 100µs, release, wait 10ms for TCXO.
#if SIGURDOS_DEBUG_MESH
    Serial.println("[mesh] hard-resetting SX1262 via RST pin...");
#endif
    pinMode(P_LORA_RESET, OUTPUT);
    digitalWrite(P_LORA_RESET, LOW);
    delayMicroseconds(100);
    digitalWrite(P_LORA_RESET, HIGH);
    delay(10);  // TCXO stabilization + radio calibration

#if SIGURDOS_DEBUG_MESH
    Serial.println("[mesh] initializing LoRa SPI bus...");
#endif
    lora_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#if SIGURDOS_DEBUG_MESH
    Serial.println("[mesh] calling radio_module->std_init()...");
#endif
    if (!radio_module->std_init(&lora_spi)) {
        Serial.println("[mesh] ERROR: Radio init failed");
        return false;
    }
    radio_inited = true;

    radio_module->setFrequency(freq);
    radio_module->setBandwidth(bw);
    radio_module->setSpreadingFactor(sf);
    radio_module->setCodingRate(cr);   // denominator (5–8); RadioLib rejects the SX126X enum constants
    radio_module->setOutputPower(tx_power);
    
    // Apply RX boosted gain mode if configured
    if (p.rx_boosted_gain) {
        radio_driver->setRxBoostedGainMode(true);
    }
#if SIGURDOS_DEBUG_MESH
    Serial.printf("[mesh] Radio: %.3f MHz / %.1f kHz / SF%d / CR4/%d / %d dBm\n",
                  freq, bw, sf, cr, tx_power);
#endif

    fast_rng.begin(radio_module->random(0x7FFFFFFF));

    g_mesh = new mesh_impl_t(*radio_driver, millis_clock, fast_rng, rtc_clock, pkt_mgr, tables);
    if (!g_mesh) {
        Serial.println("[mesh] ERROR: SigurdMeshV2 allocation failed");
        return false;
    }
    g_mesh->setMessageCallback(onMeshMessage);
    g_mesh->setOwnName(own_name);

    // Generate or load identity
    if (!loadIdentity(g_mesh->self_id)) {
        g_mesh->self_id = ::mesh::LocalIdentity(&fast_rng);
        if (spiffs_ok) {
            saveIdentity(g_mesh->self_id);
        } else {
            Serial.println("[mesh] WARNING: SPIFFS unavailable — identity is ephemeral");
        }
    }

    g_mesh->begin();

    // Apply duty cycle from prefs
    g_mesh->setDutyCycle(p.duty_cycle);

    // Restore persisted channels from NVS
    loadChannels();

    // Restore persisted contacts from SPIFFS
    loadContacts();

    // Safety net: Public is the built-in MeshCore channel and must be present
    // even if persisted NVS contains other channels from older firmware.
    ensurePublicChannelPresent(true);

    // Debug builds: auto-join the #testingsigurdos test channel for RF testing on
    // 869.525/SF10/BW250/CR5. addChannelBool() is a no-op if already present.
#if SIGURDOS_DEBUG
    g_mesh->addChannelBool("testingsigurdos", "Si/tjXzmnwmPBA43Fw4b3Q==");
    saveChannels();
    // is fully operational without requiring Settings → Radio Setup.
    {
        sigurdos::NodePrefs dp = sigurdos::prefs_get();
        if (!dp.configured) {
            dp.configured = true;
            dp.freq = LORA_FREQ;
            dp.bw = LORA_BW;
            dp.sf = LORA_SF;
            dp.cr = LORA_CR;
            dp.tx_power_dbm = LORA_TX_PWR;
            sigurdos::prefs_set(dp);
            Serial.println("[mesh] DEBUG: forced configured=true for testing");
        }
    }
#endif

    // Auto-sync #channel names as flood-scope regions.
    // Must run after all channels are loaded so regions are seeded from NVS.
    regionsInit(g_region_store);
    regionsLoad();
    syncRegionsFromChannels();

    // Restore the active flood-scope region after reboot so outgoing floods
    // continue to be stamped with the correct transport code.
    {
        const sigurdos::NodePrefs& np = sigurdos::prefs_get();
        if (np.active_region[0] && g_mesh) {
            RegionEntry* r = sigurdos::mesh::findRegion(np.active_region);
            if (r) {
                TransportKey keys[1];
                int nk = sigurdos::mesh::getRegionMap() ? sigurdos::mesh::getRegionMap()->getTransportKeysFor(*r, keys, 1) : 0;
                if (nk > 0) g_mesh->setActiveScope(keys[0].key);
            }
        }
    }

    sigurdos::mesh::messageStoreBegin();

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    {
        char ble_name[32];
        strncpy(ble_name, own_name, sizeof(ble_name) - 1);
        ble_name[sizeof(ble_name) - 1] = '\0';
        g_ble_serial.begin("MeshCore-", ble_name, g_companion_host.blePin());
        if (CompanionBridge* b = companionBridge()) {
            b->begin(&g_ble_serial, &g_companion_host);
            if (sigurdos::prefs_get().ble_enabled) {
                bool enabled = b->setEnabled(true);
#if defined(SIGURDOS_DEBUG) || \
    (defined(SIGURDOS_COMPANION_BLE_VALIDATION) && SIGURDOS_COMPANION_BLE_VALIDATION)
                Serial.printf("[mesh] Companion BLE advertising %s as MeshCore-%s\n",
                              enabled ? "enabled" : "failed", ble_name);
#endif
                (void)enabled;
            } else {
#if defined(SIGURDOS_DEBUG) || \
    (defined(SIGURDOS_COMPANION_BLE_VALIDATION) && SIGURDOS_COMPANION_BLE_VALIDATION)
                Serial.println("[mesh] Companion BLE advertising disabled by prefs");
#endif
            }
            bleValidationStartLog();
        }
    }
#else
    if (CompanionBridge* b = companionBridge()) b->begin(nullptr, &g_companion_host);
#endif

    // Only broadcast advert if user has explicitly configured radio params.
    // Compile-time defaults may be illegal in some regions — transmit gating
    // prevents first-boot broadcasts until user opens Settings → Radio Setup.
#if SIGURDOS_DEBUG
    g_mesh->broadcastAdvert(own_name, sigurdos::prefs_get().advert_type);
#else
    if (p.configured) {
        g_mesh->broadcastAdvert(own_name, sigurdos::prefs_get().advert_type);
    }
#endif

    initialized = true;
#if SIGURDOS_DEBUG_MESH
    Serial.println("[mesh] SigurdMeshV2 initialized");
#endif
    // Test entry to verify packet log works
    pushPacketLog("SYSTEM", 0, 0.0f, "BOOT");
    return true;
#else
    // Remote test without SIGURDOS_REMOTE_TEST_RADIO: init SPI bus for SD card only, no LoRa radio
    lora_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    initialized = true;
    return true;
#endif
}

// ── LVGL timer forward declaration ───────────────────────
// Called periodically during mesh loop to prevent UI stuttering.
// Declared here rather than including lvgl.h to keep dependency light.
extern "C" uint32_t lv_timer_handler(void);

void loop()
{
    if (!initialized) return;
    if (g_mesh) {
        g_mesh->loop();  // Dispatcher::loop() — fast, non-blocking
        if (g_companion_bridge_ptr) g_companion_bridge_ptr->loop();
        bleValidationEmit(false);
    }
    rtc_clock.tick();

    // ── Periodic auto-advert ──────────────────────────────
    {
        static uint32_t last_auto_adv = 0;
        uint8_t interval = sigurdos::prefs_get().advert_interval;
        if (interval > 0) {
            uint32_t now = millis();
            uint32_t interval_ms = (uint32_t)interval * 30000u; // half-minutes
            if (now - last_auto_adv >= interval_ms) {
                last_auto_adv = now;
                sendAdvert();
            }
        }
    }

    // Service LVGL timers so UI remains responsive even during
    // sustained mesh activity (periodic adverts, packet bursts).
    // Called at ~50 Hz to keep animations and input feedback smooth
    // without adding meaningful overhead.
    static uint32_t last_lvgl = 0;
    uint32_t now = millis();
    if (now - last_lvgl > 20) {
        last_lvgl = now;
        lv_timer_handler();
    }
}

// ── Send ────────────────────────────────────────

class ScopedFloodScope {
public:
    ScopedFloodScope(mesh_impl_t* mesh, const uint8_t* key16) : _mesh(mesh) {
        if (!_mesh) return;
        _had_prev = _mesh->copyActiveScope(_prev);
        if (key16) _mesh->setActiveScope(key16);
        else       _mesh->clearActiveScope();
    }

    ~ScopedFloodScope() {
        if (!_mesh) return;
        if (_had_prev) _mesh->setActiveScope(_prev);
        else           _mesh->clearActiveScope();
    }

private:
    mesh_impl_t* _mesh = nullptr;
    uint8_t _prev[16]{};
    bool _had_prev = false;
};

uint32_t sendMessage(const char* dest, const char* text) {
    if (!g_mesh) return 0;
    uint32_t ts = getCurrentTime();
    if (ts == 0) ts = 1;  // 0 means failure; use 1 as fallback so ACK matching still works
    // sendTextTo now takes a fixed timestamp so the UI and mesh layer agree
    // (see slop_mesh_v2.h sendTextTo overload)
    bool ok = g_mesh->sendTextTo(dest, text, ts);
    if (ok) {
        char conversation[sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN];
        formatDmConversation(conversation, sizeof(conversation), dest);
        storeOutgoingMessageForCompanion(conversation, text, ts, false);
        pushPacketLog(own_name, 0, 0.0f, "TX_DM");
    }
    return ok ? ts : 0;
}

bool sendChannelMessage(const char* channel_name, const char* text) {
    if (!g_mesh) return false;
    for (int i = 0; i < g_mesh->getChannelCount(); i++) {
        auto* ch = g_mesh->getChannel(i);
        if (ch && strcmp(ch->name, channel_name) == 0) {
            bool ok = g_mesh->sendGroupText(i, text);
            if (ok) {
                storeOutgoingMessageForCompanion(channel_name, text, getCurrentTime(), true);
                pushPacketLog(own_name, 0, 0.0f, "TX_CHAN");
            }
            return ok;
        }
    }
    return false;
}

uint32_t sendMessageWithScopeKey(const char* dest_name, const char* text, const uint8_t* key16) {
    ScopedFloodScope scope(g_mesh, key16);
    return sendMessage(dest_name, text);
}

bool sendChannelMessageWithScopeKey(const char* channel_name, const char* text, const uint8_t* key16) {
    ScopedFloodScope scope(g_mesh, key16);
    return sendChannelMessage(channel_name, text);
}

// ── Message queue ───────────────────────────────

int pollMessages(MeshMessage* out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
    while (n < max && queue_pop(&out[n])) n++;
    return n;
}

int pendingMessageCount() { return msg_count; }
uint32_t getQueueDropCount() { return msg_drop_count; }

int getUnreadMessageCount() { return unread_count; }
void resetUnreadMessageCount() { unread_count = 0; }

// ── Contacts ────────────────────────────────────

int getContactCount() { return g_mesh ? g_mesh->getContactCount() : 0; }

int exportContacts(char names[][32], int max) {
    if (!g_mesh || !names || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < g_mesh->getContactCount() && n < max; i++) {
        auto* c = g_mesh->getContact(i);
        if (c) { strncpy(names[n], c->name, 31); names[n][31] = '\0'; n++; }
    }
    return n;
}

// ContactInfo is declared in mesh_wrapper.h — exportContactsFull uses it
static void fillContactInfo(ContactInfo& dest, const ::ContactInfo& src) {
    strncpy(dest.name, src.name, sizeof(dest.name) - 1);
    dest.name[sizeof(dest.name) - 1] = '\0';
    dest.type = src.type;
    // Extract perm from MeshCore ContactInfo::flags bits 1-2
    dest.perm = (src.flags >> 1) & 0x03;
    // MeshCore's ContactInfo stores GPS as int32 (1e6 fixed-point) and
    // carries no per-contact RSSI/SNR — pull signal from the side-channel.
    dest.has_location = (src.gps_lat != 0 || src.gps_lon != 0);
    dest.latitude  = (float)src.gps_lat / 1000000.0f;
    dest.longitude = (float)src.gps_lon / 1000000.0f;
    dest.rssi = g_mesh->getContactRSSI(src.id.pub_key);
    dest.snr  = g_mesh->getContactSNR(src.id.pub_key);
    dest.last_seen = src.last_advert_timestamp;
}

int exportContactsFull(ContactInfo* out, int max) {
    if (!g_mesh || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < g_mesh->getContactCount() && n < max; i++) {
        auto* c = g_mesh->getContact(i);
        if (c && c->name[0]) {
            fillContactInfo(out[n], *c);
            n++;
        }
    }
    return n;
}

bool getContactByName(const char* name, ContactInfo* out) {
    if (!g_mesh || !name || !name[0] || !out) return false;
    for (int i = 0; i < g_mesh->getContactCount(); i++) {
        auto* c = g_mesh->getContact(i);
        if (c && c->name[0] && strcmp(c->name, name) == 0) {
            fillContactInfo(*out, *c);
            return true;
        }
    }
    return false;
}

// ── Favourite contacts ──────────────────────────

bool isContactFavourite(const char* name) {
    if (!g_mesh || !name) return false;
    for (int i = 0; i < g_mesh->getContactCount(); i++) {
        auto* c = g_mesh->getContact(i);
        if (c && strcmp(c->name, name) == 0) {
            return (c->flags & 0x01) != 0;
        }
    }
    return false;
}

void setContactFavourite(const char* name, bool favourite) {
    if (!g_mesh || !name) return;
    for (int i = 0; i < g_mesh->getContactCount(); i++) {
        auto* c = g_mesh->getContact(i);
        if (c && strcmp(c->name, name) == 0) {
            ::ContactInfo updated = *c;
            if (favourite) updated.flags |= 0x01;
            else           updated.flags &= ~0x01;
            // Bump lastmod + persist so a companion app's incremental
            // CMD_GET_CONTACTS(since=…) picks up the favourite change (R3).
            updated.lastmod = getCurrentTime();
            if (g_mesh->removeContact(i) && g_mesh->addContact(updated)) {
                saveContacts();
            }
            return;
        }
    }
}

// ── Channels ────────────────────────────────────

int getChannelCount() { return g_mesh ? g_mesh->getChannelCount() : 0; }

int exportChannels(char names[][32], int max) {
    if (!g_mesh) return 0;
    int n = 0;
    for (int i = 0; i < g_mesh->getChannelCount() && n < max; i++) {
        auto* ch = g_mesh->getChannel(i);
        if (ch) { strncpy(names[n], ch->name, 31); names[n][31] = '\0'; n++; }
    }
    return n;
}

bool addChannel(const char* name, const char* psk) {
    // Validate channel name
    if (!channel_name_valid(name)) return false;
    // BaseChatMesh::addChannel returns ChannelDetails* — use the bool wrapper.
    bool ok = g_mesh ? g_mesh->addChannelBool(name, psk) : false;
    if (ok) syncRegionsFromChannels();
    return ok;
}

bool addHashtagChannel(const char* name) {
    // Validate channel name
    if (!channel_name_valid(name)) return false;
    bool ok = g_mesh ? g_mesh->addHashtagChannel(name) : false;
    if (ok) syncRegionsFromChannels();
    return ok;
}

bool joinPublicChannel() {
    bool ok = addChannel(PUBLIC_CHANNEL_NAME, PUBLIC_CHANNEL_PSK_BASE64);
    if (ok) saveChannels();
    return ok;
}

// ── Region sync from channels ────────────────────
// Auto-create #regions from #channels so the user doesn't need to
// manually add each channel as a flood-scope region.

void syncRegionsFromChannels() {
    if (!g_mesh) return;
    RegionMap* map = sigurdos::mesh::getRegionMap();
    if (!map) return;

    int ch_count = g_mesh->getChannelCount();
    bool changed = false;

    for (int i = 0; i < ch_count; i++) {
        auto* ch = g_mesh->getChannel(i);
        if (!ch || !ch->name || ch->name[0] == '\0') continue;

        // Only auto-sync # public channels
        if (ch->name[0] != '#') continue;

        // Skip if this channel already has a matching region
        if (map->findByName(ch->name)) continue;

        // Create region from channel name via RegionMap
        RegionEntry* r = map->putRegion(ch->name, 0);
        if (!r) continue;

        r->flags = 0;  // allow flood by default
        changed = true;
    }

    if (changed) {
        sigurdos::mesh::regionsSave();
    }
}

// ── Identity ────────────────────────────────────

void setOwnName(const char* name) {
    if (!name) return;
    strncpy(own_name, name, sizeof(own_name) - 1);
    own_name[sizeof(own_name) - 1] = '\0';
    if (g_mesh) g_mesh->setOwnName(own_name);
}

const char* getOwnName() { return own_name; }

// ── Radio stats ─────────────────────────────────

int getNoiseFloor()   { return g_mesh ? (int)radio_driver->getNoiseFloor() : -120; }
int getLastRSSI()     { return g_mesh ? (int)radio_driver->getLastRSSI() : 0; }
float getLastSNR()    { return g_mesh ? radio_driver->getLastSNR() : 0.0f; }
unsigned long getTotalTxAirtimeMs() { return g_mesh ? g_mesh->getTotalAirTime() : 0; }
unsigned long getTotalRxAirtimeMs() { return g_mesh ? g_mesh->getReceiveAirTime() : 0; }
uint32_t getNumSentFlood()   { return g_mesh ? g_mesh->getNumSentFlood() : 0; }
uint32_t getNumSentDirect()  { return g_mesh ? g_mesh->getNumSentDirect() : 0; }
uint32_t getNumRecvFlood()   { return g_mesh ? g_mesh->getNumRecvFlood() : 0; }
uint32_t getNumRecvDirect()  { return g_mesh ? g_mesh->getNumRecvDirect() : 0; }
void resetPacketStats()      { if (g_mesh) g_mesh->resetStats(); }

// ── Signal history for sparkline ─────────────
int getSignalHistoryCount() {
    return g_mesh ? g_mesh->getSignalHistoryCount() : 0;
}
int getSignalHistoryRSSI(int idx) {
    return g_mesh ? g_mesh->getSignalHistoryRSSI(idx) : 0;
}
float getSignalHistorySNR(int idx) {
    return g_mesh ? g_mesh->getSignalHistorySNR(idx) : 0;
}

bool sendAdvert() {
    // Rate limit: reject calls within 10 seconds of the last successful advert.
    // The UI also enforces this via button cooldown, but programmatic
    // callers (e.g. Terminal's `advert` command) bypass that layer.
    static uint32_t last_advert_ms = 0;
    uint32_t now_ms = millis();
    if (last_advert_ms != 0 && now_ms - last_advert_ms < 10000) {
        return false;
    }

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    bool has_fix = sigurdos_gps_has_fix();
    bool use_live_location = has_fix && p.share_location;
    bool use_manual_location = !use_live_location && p.share_location && p.advert_location_valid;
    last_advert_time = getCurrentTime();
    last_advert_used_gps = use_live_location;

    if (!g_mesh) {
        last_advert_success = false;
        return false;
    }

    if (use_live_location) {
        g_mesh->broadcastAdvert(own_name,
            sigurdos_gps_latitude(), sigurdos_gps_longitude(),
            p.advert_type);
    } else if (use_manual_location) {
        g_mesh->broadcastAdvert(own_name,
            (float)p.advert_lat / 1000000.0f,
            (float)p.advert_lon / 1000000.0f,
            p.advert_type);
    } else {
        g_mesh->broadcastAdvert(own_name, p.advert_type);
    }

    last_advert_success = true;
    pushPacketLog(own_name, 0, 0.0f, "TX_ADV");
    last_advert_ms = now_ms;
    return true;
}

uint32_t getLastAdvertTime() {
    return last_advert_time;
}

bool getLastAdvertSuccess() {
    return last_advert_success;
}

bool getLastAdvertUsedGps() {
    return last_advert_used_gps;
}

uint32_t getCurrentTime() {
    return initialized ? rtc_clock.getCurrentTime() : 0;
}

bool setSystemTime(uint32_t epoch_seconds) {
    if (!initialized) return false;
    rtc_clock.setCurrentTime(epoch_seconds);
    fallback_clock.setCurrentTime(epoch_seconds);  // always keep soft RTC in sync too
    return true;
}

void getCurrentLocalDateTime(int* year, int* month, int* day, int* hour, int* minute) {
    if (!initialized || !year || !month || !day || !hour || !minute) {
        if (year) *year = 2024;
        if (month) *month = 1;
        if (day) *day = 1;
        if (hour) *hour = 0;
        if (minute) *minute = 0;
        return;
    }
    uint32_t epoch = rtc_clock.getCurrentTime();
    time_t t = epoch;
    struct tm* tm_info = gmtime(&t);
    *year   = tm_info->tm_year + 1900;
    *month  = tm_info->tm_mon + 1;
    *day    = tm_info->tm_mday;
    *hour   = tm_info->tm_hour;
    *minute = tm_info->tm_min;
}

uint32_t makeEpoch(int year, int month, int day, int hour, int minute) {
    // Compute UTC epoch directly — no dependency on TZ environment or
    // platform-specific timegm(). Uses Howard Hinnant's date algorithm,
    // correct for all dates in the 32-bit epoch range (1970–2106).
    int y = year;
    unsigned m = (unsigned)(month);
    if (m < 3) { m += 12; y -= 1; }
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m - 3u) + 2u) / 5u + (unsigned)(day - 1);
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    int days = (int)(era * 146097) + (int)doe - 719468;
    return (uint32_t)days * 86400u + (uint32_t)hour * 3600u + (uint32_t)minute * 60u;
}

static uint32_t trace_tag_counter = 0;

int findContactIndex(const char* name) {
    if (!g_mesh || !name || !name[0]) return -1;
    for (int i = 0; i < g_mesh->getContactCount(); i++) {
        auto* c = g_mesh->getContact(i);
        if (c && strcmp(c->name, name) == 0) return i;
    }
    return -1;
}

bool sendTrace(int contact_idx, uint32_t* out_tag) {
    if (!g_mesh) return false;
    uint32_t tag = ++trace_tag_counter;
    if (out_tag) *out_tag = tag;
    return g_mesh->sendTrace(contact_idx, tag);
}

bool hasTraceResult()   { return g_mesh ? g_mesh->hasTraceResult() : false; }
uint8_t getTracePathLen() { return g_mesh ? g_mesh->getTracePathLen() : 0; }
void getTracePath(uint8_t* snrs, uint8_t* hashes) {
    if (g_mesh) g_mesh->getTracePath(snrs, hashes);
}
void clearTraceResult() { if (g_mesh) g_mesh->clearTraceResult(); }

bool contactHasPath(int idx) {
    if (!g_mesh) return false;
    auto* c = g_mesh->getContact(idx);
    return c && c->out_path_len != OUT_PATH_UNKNOWN;
}

// ── Ping Nearby ────────────────────────────────
bool sendPingNearby() {
    return g_mesh ? g_mesh->sendPingNearby() : false;
}

bool pingIsActive() {
    return g_mesh ? g_mesh->pingIsActive() : false;
}

bool pingOnCooldown() {
    return g_mesh ? g_mesh->pingOnCooldown() : false;
}

uint32_t pingCooldownRemaining() {
    return g_mesh ? g_mesh->pingCooldownRemaining() : 0;
}

uint32_t activePingRemaining() {
    return g_mesh ? g_mesh->activePingRemaining() : 0;
}

int getPingResultCount() {
    return g_mesh ? g_mesh->getPingResultCount() : 0;
}

const PingResult* getPingResult(int i) {
    if (!g_mesh) return nullptr;
    auto* r = g_mesh->getPingResult(i);
    if (!r) return nullptr;
    // Return as public PingResult (same layout, but from different namespace)
    return reinterpret_cast<const PingResult*>(r);
}

void saveChannels() {
    if (!g_mesh) return;
    Preferences nvs;
    if (!nvs.begin("sigurdos", false)) return;
    int n = g_mesh->getChannelCount();
    nvs.putUChar("ch_cnt", (uint8_t)n);
    for (int i = 0; i < n; i++) {
        auto* ch = g_mesh->getChannel(i);
        if (!ch) continue;
        char key[16];
        snprintf(key, sizeof(key), "ch_%d_name", i);
        nvs.putString(key, ch->name);
        snprintf(key, sizeof(key), "ch_%d_sec", i);
        nvs.putBytes(key, ch->channel.secret, sizeof(ch->channel.secret));
        snprintf(key, sizeof(key), "ch_%d_hash", i);
        nvs.putBytes(key, ch->channel.hash, sizeof(ch->channel.hash));
    }
    nvs.end();
}

void loadChannels() {
    if (!g_mesh) return;
    Preferences nvs;
    if (!nvs.begin("sigurdos", true)) return;
    int n = nvs.getUChar("ch_cnt", 0);
    for (int i = 0; i < n; i++) {
        char key[16];
        char name[32] = {0};
        uint8_t secret[32] = {0};
        uint8_t hash[32] = {0};
        snprintf(key, sizeof(key), "ch_%d_name", i);
        if (nvs.getString(key, name, sizeof(name)) <= 0) continue;
        snprintf(key, sizeof(key), "ch_%d_sec", i);
        if (nvs.getBytes(key, secret, sizeof(secret)) <= 0) continue;
        snprintf(key, sizeof(key), "ch_%d_hash", i);
        if (nvs.getBytes(key, hash, sizeof(hash)) <= 0) continue;
        if (name[0]) g_mesh->loadChannel(secret, sizeof(secret), hash, name);
    }
    nvs.end();
}

void saveState() {
    if (g_mesh) saveIdentity(g_mesh->self_id);
}

// ── Contact persistence ─────────────────────────
static const char* CONTACTS_FILE = "/contacts";

void saveContacts() {
    if (!g_mesh) return;
    if (!SPIFFS.begin(false)) return;
    int n = g_mesh->getNumContacts();
    if (n <= 0) { SPIFFS.remove(CONTACTS_FILE); return; }

    File f = SPIFFS.open(CONTACTS_FILE, "w");
    if (!f) return;

    // Write contact count
    f.write((uint8_t*)&n, sizeof(n));

    for (int i = 0; i < n; i++) {
        ::ContactInfo c;
        if (!g_mesh->getContactByIdx((uint32_t)i, c)) continue;
        f.write(c.id.pub_key, PUB_KEY_SIZE);  // 32 bytes
        f.write((uint8_t*)c.name, 32);         // 32 bytes
        f.write(&c.type, 1);                    // 1 byte
        uint8_t perm = (c.flags >> 1) & 0x03;  // extract from flags bits 1-2
        f.write(&perm, 1);                      // perm byte
    }
    f.close();
}

void loadContacts() {
    if (!g_mesh) return;
    if (!SPIFFS.begin(false)) return;
    if (!SPIFFS.exists(CONTACTS_FILE)) return;

    File f = SPIFFS.open(CONTACTS_FILE, "r");
    if (!f) return;

    int n = 0;
    if (f.read((uint8_t*)&n, sizeof(n)) != sizeof(n) || n <= 0) { f.close(); return; }

    for (int i = 0; i < n; i++) {
        ::ContactInfo c{};
        if (f.read(c.id.pub_key, PUB_KEY_SIZE) != PUB_KEY_SIZE) break;
        if (f.read((uint8_t*)c.name, 32) != 32) break;
        if (f.read(&c.type, 1) != 1) break;
        // Read perm byte (format: [count:4][pub_key:32][name:32][type:1][perm:1])
        uint8_t perm_byte = 0;
        if (f.read(&perm_byte, 1) != 1) break;
        // Pack perm into flags bits 1-2, preserving bit 0 (favourite)
        c.flags = (c.flags & 0x01) | ((perm_byte & 0x03) << 1);
        c.name[31] = '\0';
        c.out_path_len = OUT_PATH_UNKNOWN;
        c.shared_secret_valid = false;
        g_mesh->addContact(c);
    }
    f.close();
}

void shutdown()
{
    if (!initialized) return;
    // Save all persistent state to NVS/SPIFFS before shutting down
    saveChannels();
    saveState();
    saveContacts();
    // Give flash writes time to complete before power cut
    delay(150);
    // Enter deep sleep indefinitely. User must press power button
    // (long press) or reset to wake. This is functionally equivalent
    // to power-off for the T-Deck.
    board.sleep(0);
}

void factoryReset()
{
    // Save identity in case we need it for rollback, then wipe everything
    if (g_mesh) saveIdentity(g_mesh->self_id);
    saveChannels();
    saveContacts();

    // Close SPIFFS before reformatting
    SPIFFS.end();

    // Erase known NVS namespaces (prefs + channels, repeater passwords)
    {
        Preferences nvs;
        if (nvs.begin("sigurdos", false)) {
            nvs.clear();
            nvs.end();
        }
    }
    {
        Preferences nvs;
        if (nvs.begin("sigurdos_pw", false)) {
            nvs.clear();
            nvs.end();
        }
    }

    // Reformat SPIFFS to wipe identity, contacts, and any other files
    SPIFFS.format();

    // Give flash writes time to complete before restart
    delay(200);

    // Reboot — on next boot, init() will find no prefs and no identity,
    // so it will use defaults and generate a fresh identity
    ESP.restart();
}

int getPacketLogCount() { return pkt_log_count; }

bool getPacketLogEntry(int index, PacketLogEntry* out) {
    if (index < 0 || index >= pkt_log_count || !out) return false;
    int idx = (pkt_log_head - pkt_log_count + index + MAX_PACKET_LOG) % MAX_PACKET_LOG;
    *out = pkt_log[idx];
    return true;
}

// ── Live radio config (no NVS write) ──────────
bool applyRadioParams(float freq, float bw, int sf, int cr, int tx_power, bool rx_gain) {
    if (!radio_module || !radio_inited) return false;
    radio_module->setFrequency(freq);
    radio_module->setBandwidth(bw);
    radio_module->setSpreadingFactor(sf);
    radio_module->setCodingRate(cr);
    radio_module->setOutputPower(tx_power);
    if (radio_driver) {
        radio_driver->setRxBoostedGainMode(rx_gain);
    }
    return true;
}

bool revertRadioParams() {
    if (!radio_module || !radio_inited) return false;
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    float freq = p.configured ? p.freq : LORA_FREQ;
    float bw   = p.configured ? p.bw   : LORA_BW;
    int   sf   = p.configured ? p.sf   : LORA_SF;
    int   cr   = p.configured ? p.cr   : LORA_CR;
    int   pwr  = p.configured ? p.tx_power_dbm : LORA_TX_PWR;
    radio_module->setFrequency(freq);
    radio_module->setBandwidth(bw);
    radio_module->setSpreadingFactor(sf);
    radio_module->setCodingRate(cr);
    radio_module->setOutputPower(pwr);
    if (radio_driver) {
        radio_driver->setRxBoostedGainMode(p.rx_boosted_gain);
    }
    return true;
}

// ── Duty cycle ────────────────────────────────
unsigned long getRemainingTxBudget() {
    return g_mesh ? g_mesh->getRemainingTxBudget() : 0;
}

void setDutyCycle(uint8_t percent) {
    if (!g_mesh) return;
    g_mesh->setDutyCycle(percent);
}

bool companionBleAvailable() {
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    return true;
#else
    return false;
#endif
}

bool companionBleSetEnabled(bool enabled) {
    sigurdos::NodePrefs p = sigurdos::prefs_get();
    p.ble_enabled = enabled;
    sigurdos::prefs_set(p);
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    CompanionBridge* b = companionBridge();
    return b && b->setEnabled(enabled);
#else
    return false;
#endif
}

bool companionBleEnabled() { CompanionBridge* b = companionBridge(); return b && b->isEnabled(); }
bool companionBleConnected() { CompanionBridge* b = companionBridge(); return b && b->isConnected(); }
uint32_t companionBleLastSyncTime() { CompanionBridge* b = companionBridge(); return b ? b->lastSyncTime() : 0; }
uint32_t companionBlePin() { return g_companion_host.blePin(); }

// ── Contact management extensions ────────────
    bool removeContact(const char* name) {
        if (!g_mesh || !name) return false;
        for (int i = 0; i < g_mesh->getContactCount(); i++) {
            auto* c = g_mesh->getContact(i);
            if (c && strcmp(c->name, name) == 0) {
                return g_mesh->removeContact(i);
            }
        }
        return false;
    }

    bool resetPathTo(const char* name) {
        if (!g_mesh || !name) return false;
        int idx = findContactIndex(name);
        if (idx < 0) return false;
        return g_mesh->resetPathTo(idx);
    }

    bool setContactPerm(const char* name, uint8_t perm) {
        if (!g_mesh || !name) return false;
        ::ContactInfo tmp;
        for (int i = 0; i < g_mesh->getNumContacts(); i++) {
            if (g_mesh->getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                // Get writable pointer to the actual MeshCore ContactInfo
                ::ContactInfo* live = g_mesh->lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE);
                if (!live) return false;
                // Pack perm into flags bits 1-2, preserving bit 0 (favourite)
                live->flags = (live->flags & 0x01) | ((perm & 0x03) << 1);
                saveContacts();
                return true;
            }
        }
        return false;
    }

    int getContactPerm(const char* name) {
        if (!g_mesh || !name) return -1;
        ::ContactInfo tmp;
        for (int i = 0; i < g_mesh->getNumContacts(); i++) {
            if (g_mesh->getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                return (tmp.flags >> 1) & 0x03;
            }
        }
        return -1;
    }

    // ── Channel management extensions ────────────
    bool removeChannel(int idx) {
        if (!g_mesh) return false;
        const ChannelDetails* ch = g_mesh->getChannel(idx);
        if (ch && isPublicChannelName(ch->name)) return false;
        bool ok = g_mesh->removeChannel(idx);
        if (ok) saveChannels();
        return ok;
    }

    // ── Repeater/room login (Phase 4.5) ──────────────
    bool sendLogin(const char* name, const char* password) {
        if (!g_mesh || !name || !password) return false;
        for (int i = 0; i < g_mesh->getContactCount(); i++) {
            auto* c = g_mesh->getContact(i);
            if (c && strcmp(c->name, name) == 0) {
                g_mesh->sendLoginTo(*c, password);
                return true;
            }
        }
        return false;
    }

    void sendLogout(const char* name) {
        if (!g_mesh || !name) return;
        for (int i = 0; i < g_mesh->getContactCount(); i++) {
            auto* c = g_mesh->getContact(i);
            if (c && strcmp(c->name, name) == 0) {
                g_mesh->sendLogoutTo(*c);
                return;
            }
        }
    }

    bool sendCommand(const char* name, const char* text) {
        if (!g_mesh || !name || !text) return false;
        for (int i = 0; i < g_mesh->getContactCount(); i++) {
            auto* c = g_mesh->getContact(i);
            if (c && strcmp(c->name, name) == 0) {
                return g_mesh->sendCommandDataTo(*c, text);
            }
        }
        return false;
    }

    bool isLoggedIn(const char* name) {
        return g_mesh ? g_mesh->isLoggedIn(name) : false;
    }

    // Force login state for a contact (test/override only)
    void forceLoginState(const char* name, uint8_t status, uint8_t permission) {
        if (!g_mesh || !name) return;
        int idx = g_mesh->findLoginEntry(name);
        if (idx < 0) {
            idx = g_mesh->addLoginEntry(name);
            if (idx < 0) return;
        }
        g_mesh->_login_entries[idx].status = status;
        g_mesh->_login_entries[idx].permission = permission;
    }

    uint8_t getLoginPermission(const char* name) {
        return g_mesh ? g_mesh->getLoginPermission(name) : 0;
    }

    uint8_t getLoginStatus(const char* name) {
        return g_mesh ? g_mesh->getLoginStatus(name) : LOGIN_STATUS_NONE;
    }

    // ── Anonymous requests (Phase 4.7) ────────────────
    bool sendAnonMessage(const char* pubkey_hex, const char* text) {
        if (!g_mesh || !pubkey_hex || !text) return false;
        uint8_t pub_key[PUB_KEY_SIZE];
        int n = SigurdMeshV2::hexToBytes(pubkey_hex, pub_key, sizeof(pub_key));
        if (n != PUB_KEY_SIZE) return false;
        return g_mesh->sendAnonMessage(pub_key, text);
    }

    // ── Group data datagrams (Phase 4.8) ─────────────
    bool sendGroupDataToChannel(int channel_idx, uint16_t data_type,
                                const uint8_t* data, int data_len) {
        return g_mesh ? g_mesh->sendGroupDataToChannel(channel_idx, data_type, data, data_len) : false;
    }

    int getGroupDataRecvCount() {
        return g_mesh ? g_mesh->getGroupDataCount() : 0;
    }

    bool getGroupDataRecvEntry(int index, uint16_t* data_type_out,
                               uint8_t* data_out, int data_out_max, int* data_len_out,
                               char* channel_out, int channel_sz,
                               uint32_t* timestamp_out) {
        if (!g_mesh) return false;
        const ::sigurdos::mesh::SigurdMeshV2::GroupDataEntry* e = g_mesh->getGroupDataEntry(index);
        if (!e || !e->valid) return false;
        if (data_type_out) *data_type_out = e->data_type;
        if (data_len_out) *data_len_out = e->data_len;
        if (data_out && data_out_max > 0 && e->data_len > 0) {
            int cp = (e->data_len < data_out_max) ? e->data_len : data_out_max;
            memcpy(data_out, e->data, cp);
        }
        if (channel_out && channel_sz > 0) {
            strncpy(channel_out, e->channel_name, channel_sz - 1);
            channel_out[channel_sz - 1] = '\0';
        }
        if (timestamp_out) *timestamp_out = e->timestamp;
        return true;
    }

    void clearGroupDataRecv() {
        if (g_mesh) g_mesh->clearGroupData();
    }

#if defined(SIGURDOS_REMOTE_TEST)
    // ── Test repeater helper ──────────────────────────
    static void fillTestContact(::ContactInfo& c, const char* name, uint8_t type) {
        c = ::ContactInfo{};
        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        c.type = type;
        c.out_path_len = OUT_PATH_UNKNOWN;
        c.last_advert_timestamp = millis() / 1000;
        c.lastmod = c.last_advert_timestamp;

        uint32_t h = 2166136261u;
        for (const char* p = name; *p; p++) {
            h ^= static_cast<uint8_t>(*p);
            h *= 16777619u;
        }
        for (int i = 0; i < PUB_KEY_SIZE; i++) {
            h ^= static_cast<uint8_t>(i * 37);
            h *= 16777619u;
            c.id.pub_key[i] = static_cast<uint8_t>(h >> ((i & 3) * 8));
        }
        if (c.id.pub_key[0] == 0x00 || c.id.pub_key[0] == 0xFF) {
            c.id.pub_key[0] = 0x42;
        }
    }

    static bool addTestContact(const char* name, uint8_t type) {
        if (!g_mesh || !name || !name[0]) return false;
        ::ContactInfo c{};
        fillTestContact(c, name, type);

        for (int i = 0; i < g_mesh->getContactCount(); i++) {
            auto* live = g_mesh->getContact(i);
            if (live && strcmp(live->name, c.name) == 0) {
                if (!g_mesh->removeContact(i)) return false;
                return g_mesh->addContact(c);
            }
        }

        if (g_mesh->getContactCount() >= MAX_CONTACTS && type == ADV_TYPE_REPEATER) {
            for (int i = 0; i < g_mesh->getContactCount(); i++) {
                auto* live = g_mesh->getContact(i);
                if (live && live->type != ADV_TYPE_REPEATER) {
                    if (!g_mesh->removeContact(i)) return false;
                    return g_mesh->addContact(c);
                }
            }
        }

        return g_mesh->addContact(c);
    }

    bool addTestRepeater(const char* name) {
        return addTestContact(name, ADV_TYPE_REPEATER);
    }

    bool addTestRoomServer(const char* name) {
        return addTestContact(name, ADV_TYPE_ROOM);
    }
#endif


// ── Command response ring buffer ────────────────
struct CmdResponse {
    char name[32];
    char text[160];
};
CmdResponse _cmd_responses[MAX_CMD_RESPONSES];
int _cmd_resp_head = 0;
int _cmd_resp_count = 0;

void pushCmdResponse(const char* name, const char* text) {
    if (!name || !text) return;
    if (_cmd_resp_count >= MAX_CMD_RESPONSES) return;  // drop if full
    int idx = (_cmd_resp_head + _cmd_resp_count) % MAX_CMD_RESPONSES;
    strncpy(_cmd_responses[idx].name, name, sizeof(_cmd_responses[idx].name) - 1);
    _cmd_responses[idx].name[sizeof(_cmd_responses[idx].name) - 1] = '\0';
    strncpy(_cmd_responses[idx].text, text, sizeof(_cmd_responses[idx].text) - 1);
    _cmd_responses[idx].text[sizeof(_cmd_responses[idx].text) - 1] = '\0';
    _cmd_resp_count++;
}

bool pollCmdResponse(char* name_out, int name_sz, char* text_out, int text_sz) {
    if (_cmd_resp_count <= 0) return false;
    CmdResponse& r = _cmd_responses[_cmd_resp_head];
    if (name_out && name_sz > 0) {
        strncpy(name_out, r.name, name_sz - 1);
        name_out[name_sz - 1] = '\0';
    }
    if (text_out && text_sz > 0) {
        strncpy(text_out, r.text, text_sz - 1);
        text_out[text_sz - 1] = '\0';
    }
    _cmd_resp_head = (_cmd_resp_head + 1) % MAX_CMD_RESPONSES;
    _cmd_resp_count--;
    return true;
}

void clearCmdResponses() {
    _cmd_resp_head = 0;
    _cmd_resp_count = 0;
}


// ── Hex-to-bytes helper ─────────────────────────
int hexToBytes(const char* hex, uint8_t* out, int out_max) {
    return SigurdMeshV2::hexToBytes(hex, out, (size_t)out_max);
}

// ── Advert path (inbound) ─────────────────────
uint8_t getAdvertPathLen(const char* name) {
    if (!g_mesh || !name) return 0;
    return g_mesh->getAdvertPathLen(name);
}

int signMessage(const char* data, uint8_t* sig_out) {
    if (!g_mesh || !data || !sig_out) return 0;
    if (!data[0]) return 0;  // empty data — nothing to sign
    g_mesh->self_id.sign(sig_out, (const uint8_t*)data, strlen(data));
    return SIGNATURE_SIZE;
}

// ── Identity backup ─────────────────────────────
bool exportIdentity(char* hex_out, size_t hex_sz) {
    if (!g_mesh || !hex_out || hex_sz < (PRV_KEY_SIZE * 2 + 1)) return false;
    uint8_t buf[PRV_KEY_SIZE + PUB_KEY_SIZE];
    size_t len = g_mesh->self_id.writeTo(buf, sizeof(buf));
    if (len < PRV_KEY_SIZE) return false;
    // Hex-encode the private key portion (first PRV_KEY_SIZE bytes)
    for (size_t i = 0; i < PRV_KEY_SIZE; i++) {
        int p = snprintf(hex_out + i * 2, hex_sz - i * 2, "%02x", buf[i]);
        if (p != 2) return false;
    }
    hex_out[PRV_KEY_SIZE * 2] = '\0';
    return true;
}

bool importIdentity(const char* hex_privkey) {
    if (!g_mesh || !hex_privkey) return false;
    size_t hex_len = strlen(hex_privkey);
    if (hex_len != PRV_KEY_SIZE * 2) return false;  // must be exactly 128 hex chars
    uint8_t buf[PRV_KEY_SIZE];
    int n = SigurdMeshV2::hexToBytes(hex_privkey, buf, sizeof(buf));
    if (n != PRV_KEY_SIZE) return false;
    // Validate the private key using MeshCore's validation (ECDH check)
    if (!::mesh::LocalIdentity::validatePrivateKey(buf)) return false;
    // Re-key the node — readFrom with PRV_KEY_SIZE will derive pub_key from prv_key
    g_mesh->self_id.readFrom(buf, PRV_KEY_SIZE);
    // Persist the new identity to SPIFFS
    saveIdentity(g_mesh->self_id);
    return true;
}

// ── URI import helpers ────────────────────────

// Simple URL decoder (in-place). Handles '+' -> ' ' and '%XX' -> char.
static uint8_t hexDigitVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void urlDecode(char* str) {
    if (!str) return;
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%'
                && ::mesh::Utils::isHexChar(src[1])
                && ::mesh::Utils::isHexChar(src[2])) {
            *dst++ = (char)((hexDigitVal(src[1]) << 4)
                          |  hexDigitVal(src[2]));
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Minimal base64 encoder — returns output length.
// Caller must provide out buffer sized at least ((inLen + 2) / 3) * 4 + 1.
static int encodeBase64(const uint8_t* in, int inLen, char* out) {
    static const char ALPHABET[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < inLen; i += 3) {
        uint32_t word = (uint32_t)in[i] << 16;
        if (i + 1 < inLen) word |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < inLen) word |= (uint32_t)in[i + 2];
        out[o++] = ALPHABET[(word >> 18) & 0x3F];
        out[o++] = ALPHABET[(word >> 12) & 0x3F];
        if (i + 1 < inLen)
            out[o++] = ALPHABET[(word >> 6) & 0x3F];
        else
            out[o++] = '=';
        if (i + 2 < inLen)
            out[o++] = ALPHABET[word & 0x3F];
        else
            out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

bool importContactByUri(const char* uri) {
    if (!uri || !g_mesh) return false;

    // Must start with "meshcore://"
    if (strncmp(uri, "meshcore://", 11) != 0) return false;
    const char* p = uri + 11;

    // ── Query-parameter format: meshcore://contact/add?name=…&public_key=…&type=… ──
    if (strncmp(p, "contact/add?", 12) == 0) {
        p += 12;

        char name[32] = {0};
        char pubkey_hex[65] = {0};
        int type = 1;  // default to chat

        while (*p) {
            const char* key_start = p;
            while (*p && *p != '=' && *p != '&') p++;
            int key_len = (int)(p - key_start);
            if (*p == '=') {
                p++; // skip =
                const char* val_start = p;
                while (*p && *p != '&') p++;
                int val_len = (int)(p - val_start);

                if (key_len == 4 && strncmp(key_start, "name", 4) == 0) {
                    int copy = val_len < (int)(sizeof(name) - 1)
                        ? val_len : (int)(sizeof(name) - 1);
                    memcpy(name, val_start, copy);
                    name[copy] = '\0';
                    urlDecode(name);
                } else if (key_len == 10 && strncmp(key_start, "public_key", 10) == 0) {
                    int copy = val_len < 64 ? val_len : 64;
                    memcpy(pubkey_hex, val_start, copy);
                    pubkey_hex[copy] = '\0';
                } else if (key_len == 4 && strncmp(key_start, "type", 4) == 0) {
                    char tbuf[4] = {0};
                    int copy = val_len < 3 ? val_len : 3;
                    memcpy(tbuf, val_start, copy);
                    type = atoi(tbuf);
                }
            }
            if (*p == '&') p++;
        }

        if (!name[0] || !pubkey_hex[0]) return false;

        uint8_t pub_key[PUB_KEY_SIZE];
        int n = SigurdMeshV2::hexToBytes(pubkey_hex, pub_key, sizeof(pub_key));
        if (n != PUB_KEY_SIZE) return false;

        if (type < 1 || type > 4) type = 1; // default to chat

        ::ContactInfo c{};
        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        memcpy(c.id.pub_key, pub_key, PUB_KEY_SIZE);
        c.type = (uint8_t)type;
        c.out_path_len = OUT_PATH_UNKNOWN;
        return g_mesh->addContact(c);
    }

    // ── channel/add query-parameter format ──
    if (strncmp(p, "channel/add?", 12) == 0) {
        return addChannelByUri(uri);  // delegate
    }

    // ── Raw hex blob format: meshcore://<hex> (biz card) ──
    // Extract all hex chars and convert to binary for importContact()
    {
        char hex[512];
        int i = 0;
        while (*p && i < (int)(sizeof(hex) - 1)) {
            if (::mesh::Utils::isHexChar(*p)) {
                hex[i++] = *p;
            }
            p++;
        }
        hex[i] = '\0';

        if (i >= (int)(PUB_KEY_SIZE * 2)) { // at least a public key's worth
            uint8_t buf[256];
            int blen = SigurdMeshV2::hexToBytes(hex, buf, sizeof(buf));
            if (blen > 0) {
                return g_mesh->importContact(buf, (uint8_t)blen);
            }
        }
    }

    return false;
}

bool addChannelByUri(const char* uri) {
    if (!uri || !g_mesh) return false;

    // Must start with "meshcore://"
    if (strncmp(uri, "meshcore://", 11) != 0) return false;
    const char* p = uri + 11;

    // Must be channel/add?...
    if (strncmp(p, "channel/add?", 12) != 0) return false;
    p += 12;

    char name[32] = {0};
    char secret_hex[65] = {0};  // 32 hex chars = 16 bytes, but allow up to 64

    while (*p) {
        const char* key_start = p;
        while (*p && *p != '=' && *p != '&') p++;
        int key_len = (int)(p - key_start);
        if (*p == '=') {
            p++;
            const char* val_start = p;
            while (*p && *p != '&') p++;
            int val_len = (int)(p - val_start);

            if (key_len == 4 && strncmp(key_start, "name", 4) == 0) {
                int copy = val_len < (int)(sizeof(name) - 1)
                    ? val_len : (int)(sizeof(name) - 1);
                memcpy(name, val_start, copy);
                name[copy] = '\0';
                urlDecode(name);
            } else if (key_len == 6 && strncmp(key_start, "secret", 6) == 0) {
                int copy = val_len < (int)(sizeof(secret_hex) - 1)
                    ? val_len : (int)(sizeof(secret_hex) - 1);
                memcpy(secret_hex, val_start, copy);
                secret_hex[copy] = '\0';
            }
        }
        if (*p == '&') p++;
    }

    if (!name[0] || !secret_hex[0]) return false;

    // Decode secret hex → bytes → base64 (what addChannel expects)
    uint8_t raw_secret[32];  // up to 32 bytes
    int raw_len = SigurdMeshV2::hexToBytes(secret_hex, raw_secret, sizeof(raw_secret));
    if (raw_len <= 0) return false;

    // Base64-encode the raw secret
    // Output size: ((raw_len + 2) / 3) * 4 + 1 — max 45 for 32 bytes
    char b64[48];
    encodeBase64(raw_secret, raw_len, b64);

    // Use the wrapper's addChannel which accepts base64 PSK
    return g_mesh->addChannelBool(name, b64);
}

// ── QR code support ─────────────────────────────
bool getContactPubkeyHex(const char* name, char* hex_out, size_t hex_sz)
{
    if (!g_mesh || !name || !hex_out) return false;
    // Need 2*PUB_KEY_SIZE hex chars + null terminator
    if (hex_sz < (size_t)(PUB_KEY_SIZE * 2 + 1)) return false;
    int count = g_mesh->getNumContacts();
    for (int i = 0; i < count; i++) {
        ::ContactInfo c;
        if (g_mesh->getContactByIdx(i, c) && strcmp(c.name, name) == 0) {
            ::mesh::Utils::toHex(hex_out, c.id.pub_key, PUB_KEY_SIZE);
            return true;
        }
    }
    return false;
}

bool getChannelSecretHex(int channel_idx, char* hex_out, size_t hex_sz)
{
    if (!g_mesh || !hex_out) return false;
    if (hex_sz < (size_t)(PUB_KEY_SIZE * 2 + 1)) return false;
    const ChannelDetails* ch = g_mesh->getChannel(channel_idx);
    if (!ch) return false;
    ::mesh::Utils::toHex(hex_out, ch->channel.secret, PUB_KEY_SIZE);
    return true;
}

// ── Regions (flood scope) ──────────────────────────
// Core implementations are in regions.cpp.
// mesh_wrapper provides g_mesh-dependent extras.

bool setActiveRegion(const char* name) {
    // Update NodePrefs + cache (via regions module)
    sigurdos::mesh::setActiveRegionName(name);

    // Propagate the TransportKey to the mesh instance
    if (g_mesh) {
        if (name && name[0]) {
            ::RegionEntry* r = sigurdos::mesh::findRegion(name);
            if (r) {
                RegionMap* map = sigurdos::mesh::getRegionMap();
                if (map) {
                    TransportKey keys[1];
                    int nk = map->getTransportKeysFor(*r, keys, 1);
                    if (nk > 0) {
                        g_mesh->setActiveScope(keys[0].key);
                        return true;
                    }
                }
            }
            // Region name saved but key not in store — clear scope
            g_mesh->clearActiveScope();
        } else {
            g_mesh->clearActiveScope();
        }
    }
    return true;
}

void setSendUnscopedOnce(bool v) {
    if (g_mesh) {
        g_mesh->setSendUnscopedOnce(v);
    }
}

} // namespace mesh
} // namespace sigurdos
