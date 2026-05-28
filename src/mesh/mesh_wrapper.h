// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// MeshCore protocol integration for SlopOS-TDeck.
// Uses SlopMesh, a minimal mesh::Mesh subclass.

#pragma once
#include <cstdint>

// Node type identifiers from MeshCore adverts — kept local so UI code can filter.
#define ADV_TYPE_NONE      0
#define ADV_TYPE_CHAT      1
#define ADV_TYPE_REPEATER  2
#define ADV_TYPE_ROOM      3
#define ADV_TYPE_SENSOR    4

namespace slopos {
namespace mesh {

struct MeshMessage {
    char sender[32];
    char channel[32];
    char text[256];
    uint32_t timestamp;
    bool is_self;
};

struct ContactInfo {
    char name[32];
    uint8_t type;  // ADV_TYPE_* (ADV_TYPE_CHAT=companion, ADV_TYPE_REPEATER, ADV_TYPE_ROOM, etc.)
    bool has_location;
    float latitude;
    float longitude;
    int  rssi;
    float snr;
    uint32_t last_seen;
};

struct PacketLogEntry {
    uint32_t timestamp;
    char     source[32];
    int      rssi;
    float    snr;
    char     type[16];
};

bool init(bool spiffs_ok = true);
void loop();

bool sendMessage(const char* dest_name, const char* text);
bool sendChannelMessage(const char* channel_name, const char* text);

int  pollMessages(MeshMessage* out, int max);
int  pendingMessageCount();
uint32_t getQueueDropCount();
int  getUnreadMessageCount();
void resetUnreadMessageCount();

int  getContactCount();
int  exportContacts(char names[][32], int max);
int  exportContactsFull(ContactInfo* out, int max);
int  findContactIndexByName(const char* name);

int  getChannelCount();
int  exportChannels(char names[][32], int max);
bool addChannel(const char* name, const char* psk_base64);
bool addHashtagChannel(const char* name);
bool joinPublicChannel();

void setOwnName(const char* name);
const char* getOwnName();

int   getNoiseFloor();
int   getLastRSSI();
float getLastSNR();
unsigned long getTotalTxAirtimeMs();
unsigned long getTotalRxAirtimeMs();
uint32_t getNumSentFlood();
uint32_t getNumSentDirect();
uint32_t getNumRecvFlood();
uint32_t getNumRecvDirect();
void resetPacketStats();

bool sendAdvert();
uint32_t getLastAdvertTime();
bool     getLastAdvertSuccess();
bool     getLastAdvertUsedGps();
void saveState();
void saveChannels();
void loadChannels();
void shutdown();

// RTC time for UI comparisons
uint32_t getCurrentTime();
bool setSystemTime(uint32_t epoch_seconds);

void getCurrentLocalDateTime(int* year, int* month, int* day, int* hour, int* minute);
uint32_t makeEpoch(int year, int month, int day, int hour, int minute);

// Packet log
int  getPacketLogCount();
bool getPacketLogEntry(int index, PacketLogEntry* out);
void pushPacketLog(const char* source, int rssi, float snr, const char* type);

// Inject a simulated incoming message into the message queue (for remote test mode).
// No radio transmission occurs. The message appears as if received from another node.
void injectMessage(const char* sender, const char* channel, const char* text);

// Trace route
bool sendTrace(int contact_idx, uint32_t* out_tag);
int  findContactIndex(const char* name);
bool hasTraceResult();
uint8_t getTracePathLen();
void   getTracePath(uint8_t* snrs_out, uint8_t* hashes_out);
void   clearTraceResult();
bool   contactHasPath(int contact_idx);

// ── Ping Nearby ─────────────────────────────────
struct PingResult {
    char name[32];
    int rssi;
};
bool     sendPingNearby();
bool     pingIsActive();
bool     pingOnCooldown();
uint32_t pingCooldownRemaining();
int      getPingResultCount();
const PingResult* getPingResult(int i);

} // namespace mesh
} // namespace slopos
