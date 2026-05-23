// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// MeshCore protocol integration using SlopMesh (minimal Mesh subclass).
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#include "mesh_wrapper.h"
#include "hal/tdeck_board.h"
#include "hal/tdeck_pins.h"
#include "hal/gps.h"
#include "hal/prefs.h"
#include "slop_mesh.h"

#include <SPIFFS.h>
#include <time.h>
#include <Mesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ESP32Board.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>

using slopos::mesh::MeshMessage;

// ════════════════════════════════════════════════════
// Global objects
// ════════════════════════════════════════════════════

static slopos::TDeckBoard        board;
static SPIClass                  lora_spi;
static Module*                   lora_mod = new Module(P_LORA_NSS, P_LORA_DIO_1,
                                                       P_LORA_RESET, P_LORA_BUSY, lora_spi);
static CustomSX1262              radio_module(lora_mod);
static CustomSX1262Wrapper       radio_driver(radio_module, board);
static ESP32RTCClock             fallback_clock;
static AutoDiscoverRTCClock      rtc_clock(fallback_clock);
static StdRNG                    fast_rng;
static SimpleMeshTables          tables;
static ArduinoMillis             millis_clock;
static StaticPoolPacketManager   pkt_mgr(16);
static slopos::mesh::SlopMesh*   g_mesh = nullptr;

static bool initialized = false;
static char own_name[32] = "SlopOS";

// ════════════════════════════════════════════════════
// Message queue
// ════════════════════════════════════════════════════

static constexpr int MAX_QUEUED = 64;
static MeshMessage   msg_buf[MAX_QUEUED];
static int           msg_head = 0, msg_tail = 0, msg_count = 0;

static void queue_push(const char* sender, const char* channel, const char* text) {
    if (msg_count >= MAX_QUEUED) return;
    MeshMessage& m = msg_buf[msg_head];
    strncpy(m.sender, sender, sizeof(m.sender) - 1);
    m.sender[sizeof(m.sender) - 1] = '\0';
    strncpy(m.channel, channel ? channel : "", sizeof(m.channel) - 1);
    m.channel[sizeof(m.channel) - 1] = '\0';
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.timestamp = rtc_clock.getCurrentTime();
    m.is_self = false;
    msg_head = (msg_head + 1) % MAX_QUEUED;
    msg_count++;
}

static bool queue_pop(MeshMessage* out) {
    if (msg_count == 0) return false;
    *out = msg_buf[msg_tail];
    msg_tail = (msg_tail + 1) % MAX_QUEUED;
    msg_count--;
    return true;
}

static void onMeshMessage(const char* sender, const char* channel, const char* text) {
    queue_push(sender, channel, text);
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    Serial.printf("[mesh] MSG from %s%s%s: %s\n",
                  sender, channel && channel[0] ? " in " : "",
                  channel && channel[0] ? channel : "", text);
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
// Public API
// ════════════════════════════════════════════════════

namespace slopos {
namespace mesh {

bool init(bool spiffs_ok)
{
    fallback_clock.begin();
    rtc_clock.begin(Wire);

    // ── Radio configuration: use compile-time defaults if not configured ──
    const slopos::NodePrefs& p = slopos::prefs_get();
    float   freq     = p.configured ? p.freq  : LORA_FREQ;
    float   bw       = p.configured ? p.bw    : LORA_BW;
    int     sf       = p.configured ? p.sf    : LORA_SF;
    int     cr       = p.configured ? p.cr    : LORA_CR;
    int     tx_power = p.configured ? p.tx_power_dbm : LORA_TX_PWR;

    if (!p.configured) {
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
        Serial.println("[mesh] Using compile-time defaults — open Settings to customize");
#endif
    }

    // ── SX1262 hard reset: radio may retain state across ESP32 reboots.
    //     If BUSY pin is stuck HIGH from a previous crash, std_init() hangs
    //     in waitForBusyPin() → watchdog reset → infinite bootloop.
    //     Solution: assert RST LOW for 100µs, release, wait 10ms for TCXO.
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    Serial.println("[mesh] hard-resetting SX1262 via RST pin...");
#endif
    pinMode(P_LORA_RESET, OUTPUT);
    digitalWrite(P_LORA_RESET, LOW);
    delayMicroseconds(100);
    digitalWrite(P_LORA_RESET, HIGH);
    delay(10);  // TCXO stabilization + radio calibration

#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    Serial.println("[mesh] initializing LoRa SPI bus...");
#endif
    lora_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    Serial.println("[mesh] calling radio_module.std_init()...");
#endif
    if (!radio_module.std_init(&lora_spi)) {
        Serial.println("[mesh] ERROR: Radio init failed");
        return false;
    }

    int16_t cr_enum = (cr == 5) ? RADIOLIB_SX126X_LORA_CR_4_5 :
                      (cr == 6) ? RADIOLIB_SX126X_LORA_CR_4_6 :
                      (cr == 7) ? RADIOLIB_SX126X_LORA_CR_4_7 :
                                  RADIOLIB_SX126X_LORA_CR_4_8;
    radio_module.setFrequency(freq);
    radio_module.setBandwidth(bw);
    radio_module.setSpreadingFactor(sf);
    radio_module.setCodingRate(cr_enum);
    radio_module.setOutputPower(tx_power);
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    Serial.printf("[mesh] Radio: %.3f MHz / %.1f kHz / SF%d / CR4/%d / %d dBm\n",
                  freq, bw, sf, cr, tx_power);
#endif

    fast_rng.begin(radio_module.random(0x7FFFFFFF));

    g_mesh = new SlopMesh(radio_driver, millis_clock, fast_rng, rtc_clock, pkt_mgr, tables);
    if (!g_mesh) {
        Serial.println("[mesh] ERROR: SlopMesh allocation failed");
        return false;
    }
    g_mesh->setMessageCallback(onMeshMessage);

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

    // Only broadcast advert if user has explicitly configured radio params.
    // Compile-time defaults may be illegal in some regions — transmit gating
    // prevents first-boot broadcasts until user opens Settings → Radio Setup.
    if (p.configured) {
        g_mesh->broadcastAdvert(own_name);
    }

    initialized = true;
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    Serial.println("[mesh] SlopMesh initialized");
#endif
    return true;
}

void loop()
{
    if (!initialized || !g_mesh) return;
    g_mesh->loop();  // handles radio_driver.loop() internally
    rtc_clock.tick();
}

// ── Send ────────────────────────────────────────

bool sendMessage(const char* dest, const char* text) {
    return g_mesh ? g_mesh->sendTextTo(dest, text) : false;
}

bool sendChannelMessage(const char* channel_name, const char* text) {
    if (!g_mesh) return false;
    // Find channel by name
    for (int i = 0; i < g_mesh->getChannelCount(); i++) {
        auto* ch = g_mesh->getChannel(i);
        if (ch && strcmp(ch->name, channel_name) == 0) {
            return g_mesh->sendGroupText(i, text);
        }
    }
    return false;
}

// ── Message queue ───────────────────────────────

int pollMessages(MeshMessage* out, int max) {
    int n = 0;
    while (n < max && queue_pop(&out[n])) n++;
    return n;
}

int pendingMessageCount() { return msg_count; }

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
            out[n].rssi = c->last_rssi;
            out[n].last_seen = c->last_seen;
            n++;
        }
    }
    return n;
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
    return g_mesh ? g_mesh->addChannel(name, psk) : false;
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
}

const char* getOwnName() { return own_name; }

// ── Radio stats ─────────────────────────────────

int getNoiseFloor()   { return radio_driver.getNoiseFloor(); }
int getLastRSSI()     { return (int)radio_driver.getLastRSSI(); }
float getLastSNR()    { return radio_driver.getLastSNR(); }

bool sendAdvert() {
    if (!g_mesh) return false;
    if (slopos_gps_has_fix()) {
        g_mesh->broadcastAdvert(own_name,
            slopos_gps_latitude(), slopos_gps_longitude());
    } else {
        g_mesh->broadcastAdvert(own_name);
    }
    return true;
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
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = 0;
    tm.tm_isdst = 0;

    // Force UTC so that the wall-clock values the user typed become the correct epoch
    // (getCurrentLocalDateTime uses gmtime, so we must match on the write side)
    const char* old_tz = getenv("TZ");
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&tm);
    if (old_tz) setenv("TZ", old_tz, 1); else unsetenv("TZ");
    tzset();

    return (uint32_t)t;
}

static uint32_t trace_tag_counter = 0;

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

void saveState() {
    if (g_mesh) saveIdentity(g_mesh->self_id);
}

} // namespace mesh
} // namespace slopos
