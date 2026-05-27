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
#include "../diagnostics/debug_cfg.h"

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

using slopos::mesh::MeshMessage;

// ════════════════════════════════════════════════════
// Global objects
// ════════════════════════════════════════════════════

static slopos::TDeckBoard        board;
static SPIClass                  lora_spi(FSPI);
static Module*                   lora_mod = nullptr;
static CustomSX1262*             radio_module = nullptr;
static CustomSX1262Wrapper*      radio_driver = nullptr;
static ESP32RTCClock             fallback_clock;
static AutoDiscoverRTCClock      rtc_clock(fallback_clock);
static StdRNG                    fast_rng;
static SimpleMeshTables          tables;
static ArduinoMillis             millis_clock;
static StaticPoolPacketManager   pkt_mgr(16);
static slopos::mesh::SlopMesh*   g_mesh = nullptr;

static bool initialized = false;
static char own_name[32] = "SlopOS";
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

static void queue_push(const char* sender, const char* channel, const char* text) {
    if (msg_count >= MAX_QUEUED) {
        msg_drop_count++;
#if SLOPOS_DEBUG_MESH
        SLOPOS_RUNTIME_FEAT(mesh) {
        Serial.printf("[mesh] WARN: message queue full — dropping msg from %s (%lu dropped so far)\n",
                      sender ? sender : "?", (unsigned long)msg_drop_count);
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
    // Increment unread count for incoming messages (reset when chat is opened)
    if (!sender || strcmp(sender, own_name) != 0) unread_count++;
    msg_head = (msg_head + 1) % MAX_QUEUED;
    msg_count++;
    // Log as packet entry (accessible via Packets screen)
    if (sender && sender[0] && g_mesh) {
        int rssi = (int)radio_driver->getLastRSSI();
        float snr = radio_driver->getLastSNR();
        const char* ptype = (channel && channel[0]) ? "CHANNEL" : "DM";
        slopos::mesh::pushPacketLog(sender, rssi, snr, ptype);
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
namespace slopos { namespace mesh { bool sendChannelMessage(const char* channel_name, const char* text); }}

static void onMeshMessage(const char* sender, const char* channel, const char* text) {
    queue_push(sender, channel, text);
#if SLOPOS_DEBUG
    // Auto-reply in debug mode to test full duplex
    if (channel && channel[0]) {
        char reply[160];
        snprintf(reply, sizeof(reply), "%s: Roger that (%s)", own_name, text);
        slopos::mesh::sendChannelMessage(channel, reply);
    }
#endif
#if SLOPOS_DEBUG_MESH
    SLOPOS_RUNTIME_FEAT(mesh) {
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
// Public API
// ════════════════════════════════════════════════════

namespace slopos {
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
#if SLOPOS_DEBUG_MESH
    SLOPOS_RUNTIME_FEAT(mesh) {
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

    fallback_clock.begin();
    rtc_clock.begin(Wire);

    // ── Radio configuration: use compile-time defaults if not configured ──
    const slopos::NodePrefs& p = slopos::prefs_get();
    float   freq     = p.configured ? p.freq  : LORA_FREQ;
    float   bw       = p.configured ? p.bw    : LORA_BW;
    int     sf       = p.configured ? p.sf    : LORA_SF;
    int     cr       = p.configured ? p.cr    : LORA_CR;
    int     tx_power = p.configured ? p.tx_power_dbm : LORA_TX_PWR;

#if SLOPOS_DEBUG
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
#if SLOPOS_DEBUG_MESH
        Serial.println("[mesh] Using compile-time defaults — open Settings to customize");
#endif
    }

    // ── SX1262 hard reset: radio may retain state across ESP32 reboots.
    //     If BUSY pin is stuck HIGH from a previous crash, std_init() hangs
    //     in waitForBusyPin() → watchdog reset → infinite bootloop.
    //     Solution: assert RST LOW for 100µs, release, wait 10ms for TCXO.
#if SLOPOS_DEBUG_MESH
    Serial.println("[mesh] hard-resetting SX1262 via RST pin...");
#endif
    pinMode(P_LORA_RESET, OUTPUT);
    digitalWrite(P_LORA_RESET, LOW);
    delayMicroseconds(100);
    digitalWrite(P_LORA_RESET, HIGH);
    delay(10);  // TCXO stabilization + radio calibration

#if SLOPOS_DEBUG_MESH
    Serial.println("[mesh] initializing LoRa SPI bus...");
#endif
    lora_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#if SLOPOS_DEBUG_MESH
    Serial.println("[mesh] calling radio_module->std_init()...");
#endif
    if (!radio_module->std_init(&lora_spi)) {
        Serial.println("[mesh] ERROR: Radio init failed");
        return false;
    }

    radio_module->setFrequency(freq);
    radio_module->setBandwidth(bw);
    radio_module->setSpreadingFactor(sf);
    radio_module->setCodingRate(cr);   // denominator (5–8); RadioLib rejects the SX126X enum constants
    radio_module->setOutputPower(tx_power);
#if SLOPOS_DEBUG_MESH
    Serial.printf("[mesh] Radio: %.3f MHz / %.1f kHz / SF%d / CR4/%d / %d dBm\n",
                  freq, bw, sf, cr, tx_power);
#endif

    fast_rng.begin(radio_module->random(0x7FFFFFFF));

    g_mesh = new SlopMesh(*radio_driver, millis_clock, fast_rng, rtc_clock, pkt_mgr, tables);
    if (!g_mesh) {
        Serial.println("[mesh] ERROR: SlopMesh allocation failed");
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

    // Restore persisted channels from NVS
    loadChannels();

    // Safety net: if no channels are loaded, auto-join the Public channel.
    // This handles fresh flashes where NVS was erased, ensuring the device
    // can at least receive Public channel messages even without completing
    // the onboarding wizard's channel setup.
    if (g_mesh->getChannelCount() == 0) {
        Serial.println("[mesh] No channels found — auto-joining Public channel");
        g_mesh->addChannel("Public", "izOH6cXN6mrJ5e26oRXNcg==");
        // Also auto-join chat channels discovered via incoming messages so
        // the device can reply on the same channel it received from.
        // Persist immediately so the channel survives reboot.
        saveChannels();
    }

    // Debug builds: auto-join the #testingslopos test channel for RF testing on
    // 869.525/SF10/BW250/CR5. addChannel() is a no-op if already present.
#if SLOPOS_DEBUG
    g_mesh->addChannel("testingslopos", "Si/tjXzmnwmPBA43Fw4b3Q==");
    saveChannels();

    // Force-configured in debug builds so adverts broadcast and the mesh
    // is fully operational without requiring Settings → Radio Setup.
    {
        slopos::NodePrefs dp = slopos::prefs_get();
        if (!dp.configured) {
            dp.configured = true;
            dp.freq = LORA_FREQ;
            dp.bw = LORA_BW;
            dp.sf = LORA_SF;
            dp.cr = LORA_CR;
            dp.tx_power_dbm = LORA_TX_PWR;
            slopos::prefs_set(dp);
            Serial.println("[mesh] DEBUG: forced configured=true for testing");
        }
    }
#endif

    // Only broadcast advert if user has explicitly configured radio params.
    // Compile-time defaults may be illegal in some regions — transmit gating
    // prevents first-boot broadcasts until user opens Settings → Radio Setup.
#if SLOPOS_DEBUG
    g_mesh->broadcastAdvert(own_name);
#else
    if (p.configured) {
        g_mesh->broadcastAdvert(own_name);
    }
#endif

    initialized = true;
#if SLOPOS_DEBUG_MESH
    Serial.println("[mesh] SlopMesh initialized");
#endif
    // Test entry to verify packet log works
    pushPacketLog("SYSTEM", 0, 0.0f, "BOOT");
    return true;
}

// ── LVGL timer forward declaration ───────────────────────
// Called periodically during mesh loop to prevent UI stuttering.
// Declared here rather than including lvgl.h to keep dependency light.
extern "C" uint32_t lv_timer_handler(void);

void loop()
{
    if (!initialized || !g_mesh) return;
    g_mesh->loop();  // Dispatcher::loop() — fast, non-blocking
    rtc_clock.tick();

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

bool sendMessage(const char* dest, const char* text) {
    bool ok = g_mesh ? g_mesh->sendTextTo(dest, text) : false;
    if (ok) pushPacketLog(own_name, 0, 0.0f, "TX_DM");
    return ok;
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
            out[n].has_location = c->has_location;
            out[n].latitude = c->latitude;
            out[n].longitude = c->longitude;
            out[n].rssi = c->last_rssi;
            out[n].snr = c->last_snr;
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
    if (g_mesh) g_mesh->setOwnName(own_name);
}

const char* getOwnName() { return own_name; }

// ── Radio stats ─────────────────────────────────

int getNoiseFloor()   { return g_mesh ? (int)radio_driver->getNoiseFloor() : -120; }
int getLastRSSI()     { return g_mesh ? (int)radio_driver->getLastRSSI() : 0; }
float getLastSNR()    { return g_mesh ? radio_driver->getLastSNR() : 0.0f; }

bool sendAdvert() {
    // Rate limit: reject calls within 10 seconds of the last successful advert.
    // The UI also enforces this via button cooldown, but programmatic
    // callers (e.g. Terminal's `advert` command) bypass that layer.
    static uint32_t last_advert_ms = 0;
    uint32_t now_ms = millis();
    if (last_advert_ms != 0 && now_ms - last_advert_ms < 10000) {
        return false;
    }

    bool has_fix = slopos_gps_has_fix();
    last_advert_time = getCurrentTime();
    last_advert_used_gps = has_fix;

    if (!g_mesh) {
        last_advert_success = false;
        return false;
    }

    if (has_fix && slopos::prefs_get().share_location) {
        g_mesh->broadcastAdvert(own_name,
            slopos_gps_latitude(), slopos_gps_longitude());
    } else {
        g_mesh->broadcastAdvert(own_name);
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
    if (!nvs.begin("slopos", false)) return;
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
    if (!nvs.begin("slopos", true)) return;
    int n = nvs.getUChar("ch_cnt", 0);
    for (int i = 0; i < n; i++) {
        char key[16];
        char name[32];
        uint8_t secret[32];
        uint8_t hash[32];
        snprintf(key, sizeof(key), "ch_%d_name", i);
        nvs.getString(key, name, sizeof(name));
        snprintf(key, sizeof(key), "ch_%d_sec", i);
        nvs.getBytes(key, secret, sizeof(secret));
        snprintf(key, sizeof(key), "ch_%d_hash", i);
        nvs.getBytes(key, hash, sizeof(hash));
        if (name[0]) g_mesh->loadChannel(secret, sizeof(secret), hash, name);
    }
    nvs.end();
}

void saveState() {
    if (g_mesh) saveIdentity(g_mesh->self_id);
}

void shutdown()
{
    if (!initialized) return;
    // Save all persistent state to NVS/SPIFFS before shutting down
    saveChannels();
    saveState();
    // Give flash writes time to complete before power cut
    delay(150);
    // Enter deep sleep indefinitely. User must press power button
    // (long press) or reset to wake. This is functionally equivalent
    // to power-off for the T-Deck.
    board.sleep(0);
}

int getPacketLogCount() { return pkt_log_count; }

bool getPacketLogEntry(int index, PacketLogEntry* out) {
    if (index < 0 || index >= pkt_log_count || !out) return false;
    int idx = (pkt_log_head - pkt_log_count + index + MAX_PACKET_LOG) % MAX_PACKET_LOG;
    *out = pkt_log[idx];
    return true;
}

} // namespace mesh
} // namespace slopos
