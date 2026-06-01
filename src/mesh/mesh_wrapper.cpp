// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// MeshCore protocol integration using SigurdMesh (minimal Mesh subclass).
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#include "mesh_wrapper.h"
#include "hal/tdeck_board.h"
#include "hal/tdeck_pins.h"
#include "hal/gps.h"
#include "hal/prefs.h"
#include "sigurd_mesh_v2.h"
#include "sigurd_mesh.h"
#include "../diagnostics/debug_cfg.h"
#include <helpers/sensors/LPPDataHelpers.h>

// REQ_TYPE constants not defined in core BaseChatMesh.h (only in examples)
#ifndef REQ_TYPE_GET_TELEMETRY_DATA
#define REQ_TYPE_GET_TELEMETRY_DATA  0x03
#endif

#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>
#include <Mesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ESP32Board.h>
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
using sigurdos::mesh::SigurdMeshV2;
using mesh_impl_t = sigurdos::mesh::SigurdMeshV2;
static mesh_impl_t*   g_mesh = nullptr;

static bool initialized = false;
static char own_name[32] = "SigurdOS";
static uint32_t last_advert_time = 0;
static bool     last_advert_success = false;
static bool     last_advert_used_gps = false;

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

// Non-static overload for SigurdMeshV2 — takes RSSI/SNR from caller context
// (SigurdMeshV2 has packet context when calling, while the static queue_push
//  reads from radio_driver which may not reflect the correct packet.)
// Defined with its qualified name to match the declaration in mesh_wrapper.h
// (sigurdos::mesh) — SigurdMeshV2 calls it as sigurdos::mesh::mesh_v2_queue_push().
void sigurdos::mesh::mesh_v2_queue_push(const char* sender, const char* channel,
                         const char* text, int rssi, float snr) {
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
#if SIGURDOS_DEBUG_MESH
    SIGURDOS_RUNTIME_FEAT(mesh) {
    Serial.printf("[mesh] MSG from %s%s%s: %s  (RSSI:%ddBm SNR:%.1fdB)\n",
                  sender, channel && channel[0] ? " in " : "",
                  channel && channel[0] ? channel : "", text, rssi, snr);
    }
#endif
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
        Serial.println("[mesh] ERROR: SigurdMesh allocation failed");
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

    // Safety net: if no channels are loaded, auto-join the Public channel.
    // This handles fresh flashes where NVS was erased, ensuring the device
    // can at least receive Public channel messages even without completing
    // the onboarding wizard's channel setup.
    if (g_mesh->getChannelCount() == 0) {
        Serial.println("[mesh] No channels found — auto-joining Public channel");
        g_mesh->addChannelBool("Public", "izOH6cXN6mrJ5e26oRXNcg==");
        // Also auto-join chat channels discovered via incoming messages so
        // the device can reply on the same channel it received from.
        // Persist immediately so the channel survives reboot.
        saveChannels();
    }

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
    Serial.println("[mesh] SigurdMesh initialized");
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

uint32_t sendMessage(const char* dest, const char* text) {
    if (!g_mesh) return 0;
    uint32_t ts = getCurrentTime();
    if (ts == 0) ts = 1;  // 0 means failure; use 1 as fallback so ACK matching still works
    // sendTextTo now takes a fixed timestamp so the UI and mesh layer agree
    // (see slop_mesh_v2.h sendTextTo overload)
    bool ok = g_mesh->sendTextTo(dest, text, ts);
    if (ok) pushPacketLog(own_name, 0, 0.0f, "TX_DM");
    return ok ? ts : 0;
}

bool sendChannelMessage(const char* channel_name, const char* text) {
    if (!g_mesh) return false;
    for (int i = 0; i < g_mesh->getChannelCount(); i++) {
        auto* ch = g_mesh->getChannel(i);
        if (ch && strcmp(ch->name, channel_name) == 0) {
            bool ok = g_mesh->sendGroupText(i, text);
            if (ok) pushPacketLog(own_name, 0, 0.0f, "TX_CHAN");
            return ok;
        }
    }
    return false;
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
    if (!g_mesh) return 0;
    int n = 0;
    for (int i = 0; i < g_mesh->getContactCount() && n < max; i++) {
        auto* c = g_mesh->getContact(i);
        if (c) { strncpy(names[n], c->name, 31); names[n][31] = '\0'; n++; }
    }
    return n;
}

// ContactInfo is declared in mesh_wrapper.h — exportContactsFull uses it

int exportContactsFull(ContactInfo* out, int max) {
    if (!g_mesh) return 0;
    int n = 0;
    for (int i = 0; i < g_mesh->getContactCount() && n < max; i++) {
        auto* c = g_mesh->getContact(i);
        if (c && c->name[0]) {
            strncpy(out[n].name, c->name, 31);
            out[n].name[31] = '\0';
            out[n].type = c->type;
            // MeshCore's ContactInfo stores GPS as int32 (1e6 fixed-point) and
            // carries no per-contact RSSI/SNR — pull signal from the side-channel.
            out[n].has_location = (c->gps_lat != 0 || c->gps_lon != 0);
            out[n].latitude  = (float)c->gps_lat / 1000000.0f;
            out[n].longitude = (float)c->gps_lon / 1000000.0f;
            out[n].rssi = g_mesh->getContactRSSI(c->id.pub_key);
            out[n].snr  = g_mesh->getContactSNR(c->id.pub_key);
            out[n].last_seen = c->last_advert_timestamp;
            n++;
        }
    }
    return n;
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
        auto* c = const_cast<::ContactInfo*>(g_mesh->getContact(i));
        if (c && strcmp(c->name, name) == 0) {
            if (favourite) c->flags |= 0x01;
            else           c->flags &= ~0x01;
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
    // BaseChatMesh::addChannel returns ChannelDetails* — use the bool wrapper.
    return g_mesh ? g_mesh->addChannelBool(name, psk) : false;
}

bool addHashtagChannel(const char* name) {
    return g_mesh ? g_mesh->addHashtagChannel(name) : false;
}

bool joinPublicChannel() {
    return addChannel("Public", "izOH6cXN6mrJ5e26oRXNcg==");
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

    bool has_fix = sigurdos_gps_has_fix();
    last_advert_time = getCurrentTime();
    last_advert_used_gps = has_fix;

    if (!g_mesh) {
        last_advert_success = false;
        return false;
    }

    if (has_fix && sigurdos::prefs_get().share_location) {
        g_mesh->broadcastAdvert(own_name,
            sigurdos_gps_latitude(), sigurdos_gps_longitude(),
            sigurdos::prefs_get().advert_type);
    } else {
        g_mesh->broadcastAdvert(own_name, sigurdos::prefs_get().advert_type);
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
        ::ContactInfo c;
        memset(&c, 0, sizeof(c));
        if (f.read(c.id.pub_key, PUB_KEY_SIZE) != PUB_KEY_SIZE) break;
        if (f.read((uint8_t*)c.name, 32) != 32) break;
        if (f.read(&c.type, 1) != 1) break;
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

    // ── Channel management extensions ────────────
    bool removeChannel(int idx) {
        if (!g_mesh) return false;
        bool ok = g_mesh->removeChannel(idx);
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
    bool addTestRepeater(const char* name) {
        if (!g_mesh || !name || !name[0]) return false;
        ::ContactInfo c;
        memset(&c, 0, sizeof(c));
        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        c.type = ADV_TYPE_REPEATER;
        return g_mesh->addContact(c);
    }

    bool addTestRoomServer(const char* name) {
        if (!g_mesh || !name || !name[0]) return false;
        ::ContactInfo c;
        memset(&c, 0, sizeof(c));
        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        c.type = ADV_TYPE_ROOM;
        return g_mesh->addContact(c);
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

} // namespace mesh
} // namespace sigurdos
