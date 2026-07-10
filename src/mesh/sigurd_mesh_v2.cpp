// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// SigurdMeshV2 — method implementations split from sigurd_mesh_v2.h
//
#include "sigurd_mesh_v2.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "mesh_wrapper.h"
#include "hal/prefs.h"
#include "hal/battery.h"
#include "hal/gps.h"
#include "hal/tdeck_board.h"
#include "../diagnostics/debug_cfg.h"

namespace sigurdos {
namespace mesh {

    void SigurdMeshV2::pushSignalHistory(int rssi, float snr) {
        uint32_t now = 0;
        if (getRTCClock()) now = getRTCClock()->getCurrentTime();
        _signal_history[_signal_history_head].rssi = rssi;
        _signal_history[_signal_history_head].snr = snr;
        _signal_history[_signal_history_head].timestamp = now;
        _signal_history_head = (_signal_history_head + 1) % SIGNAL_HISTORY_MAX;
        if (_signal_history_count < SIGNAL_HISTORY_MAX) _signal_history_count++;
    }

    void SigurdMeshV2::updateSignalSample(const uint8_t* pub_key, int rssi, float snr) {
        if (!pub_key) return;
        uint32_t now = getRTCClock() ? getRTCClock()->getCurrentTime() : 0;
        for (int i = 0; i < _n_signal_samples; i++) {
            if (memcmp(_signal_samples[i].key, pub_key, 4) == 0) {
                _signal_samples[i].rssi = rssi;
                _signal_samples[i].snr = snr;
                _signal_samples[i].updated_at = now;
                pushSignalHistory(rssi, snr);
                return;
            }
        }
        if (_n_signal_samples < SIGNAL_SAMPLES_MAX) {
            memcpy(_signal_samples[_n_signal_samples].key, pub_key, 4);
            _signal_samples[_n_signal_samples].rssi = rssi;
            _signal_samples[_n_signal_samples].snr = snr;
            _signal_samples[_n_signal_samples].updated_at = now;
            _n_signal_samples++;
        }
        pushSignalHistory(rssi, snr);
    }

    int SigurdMeshV2::getContactRSSI(const uint8_t* pub_key) const {
        if (!pub_key) return 0;
        for (int i = 0; i < _n_signal_samples; i++)
            if (memcmp(_signal_samples[i].key, pub_key, 4) == 0)
                return _signal_samples[i].rssi;
        return 0;
    }

    float SigurdMeshV2::getContactSNR(const uint8_t* pub_key) const {
        if (!pub_key) return 0.0f;
        for (int i = 0; i < _n_signal_samples; i++)
            if (memcmp(_signal_samples[i].key, pub_key, 4) == 0)
                return _signal_samples[i].snr;
        return 0.0f;
    }

    bool SigurdMeshV2::sendTrace(int contact_idx, uint32_t tag) {
        ::ContactInfo c;
        if (!getContactByIdx((uint32_t)contact_idx, c)) return false;
        if (c.out_path_len == OUT_PATH_UNKNOWN) return false;
        _has_trace_result = false;
        ::mesh::Packet* pkt = createTrace(tag, 0, 0);
        if (!pkt) return false;
        sendDirect(pkt, c.out_path, c.out_path_len);
        return true;
    }

    bool SigurdMeshV2::sendTracePathRaw(uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* path, uint8_t path_len, uint32_t& est_timeout) {
        ::mesh::Packet* pkt = createTrace(tag, auth, flags);
        if (!pkt) return false;
        _has_trace_result = false;
        sendDirect(pkt, const_cast<uint8_t*>(path), path_len);
        uint8_t path_sz = flags & 0x03;
        // Advisory estimate; airtime is dominated by per-hop round trips.
        est_timeout = calcDirectTimeoutMillisFor(150, (uint8_t)(path_len >> path_sz));
        return true;
    }

    int SigurdMeshV2::sendLoginCompanion(const ::ContactInfo& contact, const char* password, uint32_t& est_timeout) {
        est_timeout = 0;
        int r = BaseChatMesh::sendLogin(contact, password ? password : "", est_timeout);
        if (r != MSG_SEND_FAILED) addLoginEntry(contact.name);
        return r;
    }

    bool SigurdMeshV2::companionHasConnection(const uint8_t* pub_key) {
        return pub_key && BaseChatMesh::hasConnectionTo(pub_key);
    }

    void SigurdMeshV2::companionStopConnection(const uint8_t* pub_key) {
        if (pub_key) BaseChatMesh::stopConnection(pub_key);
    }

    int SigurdMeshV2::exportSelfContact(const char* name, uint8_t* out, size_t out_cap) {
        if (!out || out_cap < 1) return 0;
        ::mesh::Packet* pkt = createSelfAdvert(name ? name : "");
        if (!pkt) return 0;
        pkt->header |= ROUTE_TYPE_FLOOD;
        uint8_t n = pkt->writeTo(out);
        releasePacket(pkt);
        return (n > 0 && n <= out_cap) ? (int)n : 0;
    }

    void SigurdMeshV2::onTraceRecv(::mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags, const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
        // Fan out the full trace result to the phone app before clamping.
        int8_t final_snr_q = (int8_t)((pkt ? pkt->getSNR() : 0.0f) * 4.0f);
        sigurdos::mesh::mesh_v2_companion_trace_push(tag, auth_code, flags,
                                                     path_hashes, path_snrs,
                                                     path_len, final_snr_q);
        _last_trace_tag = tag;
        if (path_len > MAX_PATH_SIZE) path_len = MAX_PATH_SIZE;
        _last_trace_len = path_len;
        memcpy(_last_trace_snrs, path_snrs, path_len);
        memcpy(_last_trace_hashes, path_hashes, path_len);
        _has_trace_result = true;
    }

    void SigurdMeshV2::getTracePath(uint8_t* snrs_out, uint8_t* hashes_out) {
        memcpy(snrs_out, _last_trace_snrs, _last_trace_len);
        memcpy(hashes_out, _last_trace_hashes, _last_trace_len);
    }

    bool SigurdMeshV2::sendPingNearby() {
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

    void SigurdMeshV2::onControlDataRecv(::mesh::Packet* pkt) {
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

        // ── Node Discovery Protocol (0x80/0x90) ──
        // Interoperability with MeshCore repeaters & sensors.
        // Request: payload[0]=0x80|prefix_flag, [1]=filter_bitmask,
        //          [2-5]=4-byte-tag, [6-9]=since (optional)
        // Response: payload[0]=0x90|node_type, [1]=snr×4,
        //           [2-5]=echoed_tag, [6-37]=pubkey (32 or 8 bytes)
        if ((pkt->payload[0] & 0xF0) == 0x80) {
            // Only answer discovery requests, not responses
            if (pkt->payload[0] == 0x80 || pkt->payload[0] == 0x81) {
                if (pkt->payload_len < 6) return;  // min: type+filter+tag

                bool prefix_only = (pkt->payload[0] & 0x01) != 0;
                uint8_t filter = pkt->payload[1];
                uint32_t tag;
                memcpy(&tag, pkt->payload + 2, 4);

                // Get our node type from prefs (default CHAT=1)
                uint8_t node_type = sigurdos::prefs_get().advert_type;
                if (node_type < 1 || node_type > 4) node_type = 1;

                // Check filter — skip if our type doesn't match requested types
                if (filter != 0 && !(filter & (1 << node_type))) return;

                // Build response
                uint8_t resp[64];
                size_t resp_len = 0;
                resp[resp_len++] = 0x90 | (node_type & 0x0F);

                // SNR (×4, clamped to 0-255)
                float snr_db = _radio->getLastSNR();
                int snr_scaled = (int)(snr_db * 4.0f);
                if (snr_scaled < 0) snr_scaled = 0;
                if (snr_scaled > 255) snr_scaled = 255;
                resp[resp_len++] = (uint8_t)snr_scaled;

                // Echo the request tag
                memcpy(resp + resp_len, &tag, 4);
                resp_len += 4;

                // Pubkey (full 32 bytes or 8-byte prefix)
                if (prefix_only) {
                    memcpy(resp + resp_len, self_id.pub_key, 8);
                    resp_len += 8;
                } else {
                    memcpy(resp + resp_len, self_id.pub_key, 32);
                    resp_len += 32;
                }

                ::mesh::Packet* r = createControlData(resp, resp_len);
                if (r) {
                    sendZeroHop(r);
                }
            }
            return;
        }
    }

    bool SigurdMeshV2::sendRequest(const char* name, uint8_t req_type) {
        if (!name || !name[0]) return false;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t tag = 0, est_timeout = 0;
                int r = BaseChatMesh::sendRequest(tmp, req_type, tag, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    for (int j = 0; j < MAX_PENDING_REQUESTS; j++) {
                        if (!_pending_reqs[j].in_use) {
                            _pending_reqs[j].tag = tag;
                            _pending_reqs[j].req_type = req_type;
                            strncpy(_pending_reqs[j].dest_name, name,
                                    sizeof(_pending_reqs[j].dest_name) - 1);
                            _pending_reqs[j].dest_name[sizeof(_pending_reqs[j].dest_name) - 1] = '\0';
                            _pending_reqs[j].sent_at_ms = _ms->getMillis();
                            _pending_reqs[j].in_use = true;
                            break;
                        }
                    }
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool SigurdMeshV2::sendRequestWithData(const char* name, const uint8_t* data, uint8_t data_len) {
        if (!name || !name[0] || !data || data_len == 0) return false;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t tag = 0, est_timeout = 0;
                int r = BaseChatMesh::sendRequest(tmp, data, data_len, tag, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    for (int j = 0; j < MAX_PENDING_REQUESTS; j++) {
                        if (!_pending_reqs[j].in_use) {
                            _pending_reqs[j].tag = tag;
                            _pending_reqs[j].req_type = 0; // custom data
                            strncpy(_pending_reqs[j].dest_name, name,
                                    sizeof(_pending_reqs[j].dest_name) - 1);
                            _pending_reqs[j].dest_name[sizeof(_pending_reqs[j].dest_name) - 1] = '\0';
                            _pending_reqs[j].sent_at_ms = _ms->getMillis();
                            _pending_reqs[j].in_use = true;
                            break;
                        }
                    }
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool SigurdMeshV2::sendRoomMsgFetchRequest(const char* name, const char* channel_name) {
        if (!name || !name[0] || !channel_name || !channel_name[0]) return false;
        _n_room_fetched = 0;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                // REQ data: [req_type][channel_name\0]
                uint8_t req_data[64];
                req_data[0] = REQ_TYPE_GET_ROOM_MSGS;
                size_t cn_len = strlen(channel_name);
                if (cn_len > 62) cn_len = 62;
                memcpy(&req_data[1], channel_name, cn_len + 1);
                uint32_t tag = 0, est_timeout = 0;
                int r = BaseChatMesh::sendRequest(tmp, req_data, 1 + cn_len + 1,
                                                  tag, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    for (int j = 0; j < MAX_PENDING_REQUESTS; j++) {
                        if (!_pending_reqs[j].in_use) {
                            _pending_reqs[j].tag = tag;
                            _pending_reqs[j].req_type = REQ_TYPE_GET_ROOM_MSGS;
                            strncpy(_pending_reqs[j].dest_name, name,
                                    sizeof(_pending_reqs[j].dest_name) - 1);
                            _pending_reqs[j].dest_name[sizeof(_pending_reqs[j].dest_name) - 1] = '\0';
                            strncpy(_pending_reqs[j].channel_name, channel_name,
                                    sizeof(_pending_reqs[j].channel_name) - 1);
                            _pending_reqs[j].channel_name[sizeof(_pending_reqs[j].channel_name) - 1] = '\0';
                            _pending_reqs[j].sent_at_ms = _ms->getMillis();
                            _pending_reqs[j].in_use = true;
                            break;
                        }
                    }
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    void SigurdMeshV2::parseRoomMsgResponse(const ::ContactInfo& contact, const uint8_t* data, uint8_t len, const char* channel_name) {
        // Response format: repeated entries of [sender\0][text\0]
        uint8_t pos = 0;
        while (pos + 2 < len && _n_room_fetched < MAX_ROOM_MSG_FETCH) {
            // Read null-terminated sender name
            const char* sender = (const char*)(data + pos);
            size_t slen = strnlen(sender, len - pos);
            if (slen == 0 || slen >= len - pos) break;
            pos += slen + 1;
            if (pos >= len) break;

            // Read null-terminated message text
            const char* text = (const char*)(data + pos);
            size_t tlen = strnlen(text, len - pos);
            if (tlen == 0 || tlen >= len - pos) break;
            pos += tlen + 1;

            RoomMsgFetchEntry& e = _room_fetch_buf[_n_room_fetched++];
            strncpy(e.sender, sender, sizeof(e.sender) - 1);
            e.sender[sizeof(e.sender) - 1] = '\0';
            strncpy(e.text, text, sizeof(e.text) - 1);
            e.text[sizeof(e.text) - 1] = '\0';
            e.timestamp = getRTCClock()->getCurrentTime();
            strncpy(e.channel, channel_name, sizeof(e.channel) - 1);
            e.channel[sizeof(e.channel) - 1] = '\0';
            e.valid = true;

            // Also push to mesh message queue so it appears in chat
            sigurdos::mesh::mesh_v2_queue_push(e.sender, e.channel, e.text, 0, 0.0f);
        }
    }

    void SigurdMeshV2::onDiscoveredContact(::ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
        int rssi = (int)_radio->getLastRSSI();
        float snr = _radio->getLastSNR();
        updateSignalSample(contact.id.pub_key, rssi, snr);
        sigurdos::mesh::pushPacketLog(contact.name, rssi, snr,
                                    is_new ? "ADVERT" : "ADVERT(UPDATE)");

        // ── Track inbound advert path ──────────────
        if (path && ::mesh::Packet::isValidPathLen(path_len)) {
            storeAdvertPath(contact.id.pub_key, contact.name, path_len, path);
        }

        // Fan out to the phone app (NEW_ADVERT for a new contact, else ADVERT).
        sigurdos::mesh::mesh_v2_companion_advert_push(&contact, is_new);

#if SIGURDOS_DEBUG_MESH
        Serial.printf("[mesh] %s contact: %s (type=%d)\n",
                      is_new ? "new" : "updated", contact.name, contact.type);
#endif
    }

    void SigurdMeshV2::onContactOverwrite(const uint8_t* pub_key) {
        sigurdos::mesh::mesh_v2_companion_contact_deleted_push(pub_key);
    }

    void SigurdMeshV2::addPendingAck(const char* name, uint32_t ts, uint32_t expected_ack) {
        for (int i = 0; i < MAX_PENDING_ACKS; i++) {
            if (!_pending_acks[i].in_use) {
                strncpy(_pending_acks[i].dest_name, name, sizeof(_pending_acks[i].dest_name)-1);
                _pending_acks[i].dest_name[sizeof(_pending_acks[i].dest_name)-1] = '\0';
                _pending_acks[i].timestamp = ts;
                _pending_acks[i].expected_ack = expected_ack;
                _pending_acks[i].sent_at_ms = _ms->getMillis();
                _pending_acks[i].in_use = true;
                return;
            }
        }
        // Table full — evict oldest entry
        int oldest = 0;
        uint32_t oldest_ms = _pending_acks[0].sent_at_ms;
        for (int i = 1; i < MAX_PENDING_ACKS; i++) {
            if (_pending_acks[i].sent_at_ms < oldest_ms) {
                oldest = i;
                oldest_ms = _pending_acks[i].sent_at_ms;
            }
        }
        strncpy(_pending_acks[oldest].dest_name, name, sizeof(_pending_acks[oldest].dest_name)-1);
        _pending_acks[oldest].dest_name[sizeof(_pending_acks[oldest].dest_name)-1] = '\0';
        _pending_acks[oldest].timestamp = ts;
        _pending_acks[oldest].expected_ack = expected_ack;
        _pending_acks[oldest].sent_at_ms = _ms->getMillis();
        _pending_acks[oldest].in_use = true;
    }

    ::ContactInfo* SigurdMeshV2::processAck(const uint8_t* data) {
        if (!data) return nullptr;
        uint32_t ack_val;
        memcpy(&ack_val, data, 4);
        for (int i = 0; i < MAX_PENDING_ACKS; i++) {
            if (_pending_acks[i].in_use && _pending_acks[i].expected_ack == ack_val) {
                _pending_acks[i].in_use = false;
                uint32_t trip_ms = _ms->getMillis() - _pending_acks[i].sent_at_ms;
                // Notify wrapper layer (local UI + persistent store)
                sigurdos::mesh::registerAckedMessage(_pending_acks[i].dest_name, _pending_acks[i].timestamp);
                // Notify the phone app so it marks the sent message delivered.
                sigurdos::mesh::mesh_v2_notify_send_confirmed(ack_val, trip_ms);
                // Return a valid ContactInfo for BaseChatMesh internal processing
                for (int j = 0; j < getNumContacts(); j++) {
                    if (getContactByIdx((uint32_t)j, _contact_cache)) {
                        return &_contact_cache;
                    }
                }
                return &_contact_cache;
            }
        }
        return nullptr;
    }

    void SigurdMeshV2::onMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
        int rssi = (int)_radio->getLastRSSI();
        float snr = pkt ? pkt->getSNR() : _radio->getLastSNR();
        updateSignalSample(contact.id.pub_key, rssi, snr);
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr,
                                           sender_timestamp, companion_path_len,
                                           contact.id.pub_key,
                                           0);  // COMPANION_TXT_PLAIN
    }

    void SigurdMeshV2::onCommandDataRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
        sigurdos::mesh::pushCmdResponse(contact.name, text);
        // Store as COMPANION_TXT_CLI_DATA with the raw app payload text so the
        // companion bridge emits the correct txt_type to official apps. The
        // local UI queue gets this raw text (pushCmdResponse handles the
        // command-specific display logic separately).
        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr,
                                           sender_timestamp, companion_path_len,
                                           contact.id.pub_key,
                                           1);  // COMPANION_TXT_CLI_DATA
    }

    void SigurdMeshV2::onAnonDataRecv(::mesh::Packet* pkt, const uint8_t* secret, const ::mesh::Identity& sender, uint8_t* data, size_t len) {
        if (len <= 4 || !data) return;
        // Copy text to local buffer — do NOT modify the packet data in-place
        // (the buffer may be shared between multiple callers).
        size_t tlen = len - 4;
        if (tlen > 159) tlen = 159;
        char text[160];
        memcpy(text, data + 4, tlen);
        text[tlen] = '\0';

        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;

        // Generate a fallback name from sender pubkey prefix (4 hex chars
        // to reduce collision probability to ~1/65536 per sender pair)
        char fallback[20];
        snprintf(fallback, sizeof(fallback), "anon_%02x%02x",
                 sender.pub_key[0], sender.pub_key[1]);

        sigurdos::mesh::mesh_v2_queue_push(fallback, "", text, rssi, snr);
    }

    void SigurdMeshV2::onSignedMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t* sender_prefix, const char* text) {
        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr,
                                           sender_timestamp, companion_path_len,
                                           contact.id.pub_key,
                                           2,              // COMPANION_TXT_SIGNED_PLAIN
                                           sender_prefix, 4);
    }

    void SigurdMeshV2::onChannelMessageRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* pkt, uint32_t timestamp, const char* text) {
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
        char sender_buf[32];  // hoisted: must outlive the if-block (used at queue_push below)
        if (colon && colon > text) {
            size_t nlen = colon - text;
            if (nlen > 31) nlen = 31;
            memcpy(sender_buf, text, nlen);
            sender_buf[nlen] = '\0';
            sender_name = sender_buf;
            msg_text = colon + 2;
        }
        // Channel messages are flood-routed: forward the real hop path so the
        // app shows the true hop count (0xFF only for a direct/unknown route).
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(sender_name, chname, msg_text, rssi, snr,
                                           timestamp, companion_path_len);
    }

    uint8_t SigurdMeshV2::onContactRequest(const ::ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) {
        // Telemetry request: send battery voltage (and GPS if available)
        if (len >= 1 && data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
            uint8_t pos = 0;

            // Battery voltage as CayenneLPP analog input (channel 1, 0.01V resolution)
            uint16_t mv = sigurdos_battery_mv();
            uint16_t val = mv / 10;  // mV → decivolts (0.01V)
            reply[pos++] = 0x01;          // channel 1
            reply[pos++] = LPP_ANALOG_INPUT; // analog input type
            reply[pos++] = (val >> 8) & 0xFF;
            reply[pos++] = val & 0xFF;

            // Optional: GPS position if fix available
            if (sigurdos_gps_has_fix() && pos + 11 <= 64) {
                LPPWriter lpp(reply + pos, 64 - pos);
                lpp.writeGPS(2, sigurdos_gps_latitude(),
                             sigurdos_gps_longitude(),
                             sigurdos_gps_altitude_m());
                pos += lpp.length();
            }

            return pos;
        }
        return 0;  // unknown request type
    }

    void SigurdMeshV2::onContactResponse(const ::ContactInfo& contact, const uint8_t* data, uint8_t len) {
        if (!data || len < 4) return;
        uint32_t tag;
        memcpy(&tag, data, 4);

        // ── Check for login response ──────────────────
        // Format: bytes 0-3=tag, byte4=RESP_SERVER_LOGIN_OK(0)=success,
        //         byte5=keep_alive_secs/16, byte6=permissions, byte7=ACL (v7+)
        // Legacy: bytes 4-5 = "OK" (2 chars)
        // Only check when there is a pending/active login entry for this contact.
        int login_idx = findLoginEntry(contact.name);
        if (login_idx >= 0 && _login_entries[login_idx].in_use && len >= 8) {
            // New-style login response
            if (data[4] == RESP_SERVER_LOGIN_OK) {
                uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
                uint8_t  perm = data[6];
                uint8_t  acl = (len > 7) ? data[7] : 0;

                _login_entries[login_idx].status = LOGIN_OK;
                _login_entries[login_idx].permission = perm;
                _login_entries[login_idx].acl_permissions = acl;

                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact.id.pub_key, true, perm, /*is_admin=*/false);

                // Start keep-alive connection
                if (keep_alive_secs > 0) {
                    BaseChatMesh::startConnection(contact, keep_alive_secs);
                }

#if SIGURDOS_DEBUG_MESH
                Serial.printf("[mesh] Login OK for %s (perm=%d, acl=%d, ka=%us)\n",
                              contact.name, perm, acl, keep_alive_secs);
#endif
                return; // handled — don't push to ring buffer
            }
            // Legacy login "OK" response
            if (data[4] == 'O' && data[5] == 'K') {
                _login_entries[login_idx].status = LOGIN_OK;
                _login_entries[login_idx].permission = 1; // legacy: admin if "OK"
                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact.id.pub_key, true, 0, /*is_admin=*/false);
#if SIGURDOS_DEBUG_MESH
                Serial.printf("[mesh] Login OK (legacy) for %s\n", contact.name);
#endif
                return;
            }
            // Explicit login failure — pending entry with nonzero code
            if (_login_entries[login_idx].status == LOGIN_PENDING && data[4] != 0) {
                _login_entries[login_idx].status = LOGIN_FAILED;
                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact.id.pub_key, false, 0, false);
#if SIGURDOS_DEBUG_MESH
                Serial.printf("[mesh] Login FAILED for %s (reason=%d)\n",
                              contact.name, data[4]);
#endif
                // Don't return — also store in ring buffer for inspection
            }
        }

        // ── Existing ring buffer logic ────────────────
        // Store the response in the ring buffer
        if (_n_responses < MAX_RESPONSES) {
            ResponseEntry& re = _responses[_n_responses++];
            re.tag = tag;
            strncpy(re.contact_name, contact.name, sizeof(re.contact_name) - 1);
            re.contact_name[sizeof(re.contact_name) - 1] = '\0';
            re.len = (len < MAX_RESPONSE_DATA) ? len : MAX_RESPONSE_DATA;
            memcpy(re.data, data, re.len);
            re.valid = true;
        }

        // Clear matching pending request — also parse room msg responses and
        // fan status/telemetry responses out to the phone app.
        for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
            if (_pending_reqs[i].in_use && _pending_reqs[i].tag == tag) {
                uint8_t req_type = _pending_reqs[i].req_type;
                if (req_type == REQ_TYPE_GET_ROOM_MSGS && len > 4) {
                    char chan[32];
                    strncpy(chan, _pending_reqs[i].channel_name, sizeof(chan) - 1);
                    chan[sizeof(chan) - 1] = '\0';
                    _pending_reqs[i].in_use = false;
                    parseRoomMsgResponse(contact, data + 4, len - 4, chan);
                } else {
                    _pending_reqs[i].in_use = false;
                    if (len > 4) {
                        if (req_type == REQ_TYPE_GET_STATUS) {
                            sigurdos::mesh::mesh_v2_companion_status_push(
                                contact.id.pub_key, data + 4, len - 4);
                        } else if (req_type == REQ_TYPE_GET_TELEMETRY_DATA) {
                            sigurdos::mesh::mesh_v2_companion_telemetry_push(
                                contact.id.pub_key, data + 4, len - 4);
                        }
                    }
                }
                break;
            }
        }
    }

    void SigurdMeshV2::onContactPathUpdated(const ::ContactInfo& contact) {
#if SIGURDOS_DEBUG_MESH
        Serial.printf("[mesh] Path updated for %s (len=%d)\n",
                      contact.name, contact.out_path_len);
#endif
        sigurdos::mesh::mesh_v2_companion_path_push(contact.id.pub_key);
        // If we had a pending discovery for this contact, mark it complete
        for (int i = 0; i < MAX_DISCOVERY_PENDING; i++) {
            if (_discovery_pending[i].in_use &&
                strcmp(_discovery_pending[i].dest_name, contact.name) == 0) {
                _discovery_pending[i].completed = true;
                break;
            }
        }
    }

    uint32_t SigurdMeshV2::sendPathDiscovery(const char* name) {
        if (!name || !name[0]) return 0;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                // Get a writable pointer to the contact
                ::ContactInfo* c = lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE);
                if (!c) return 0;
                uint32_t tag = 0, est_timeout = 0;
                // Force flood by temporarily clearing path
                uint8_t saved_len = c->out_path_len;
                uint8_t saved_path[32];
                if (saved_len <= 32 && saved_len != OUT_PATH_UNKNOWN)
                    memcpy(saved_path, c->out_path, saved_len);
                c->out_path_len = OUT_PATH_UNKNOWN;
                // Send minimal discovery request
                uint8_t req_data[5] = {0x04, 0x00, 0x00, 0x00, 0x00};
                int r = BaseChatMesh::sendRequest(*c, req_data, sizeof(req_data), tag, est_timeout);
                // Restore original path ONLY if it wasn't legitimately updated during send
                if (c->out_path_len == OUT_PATH_UNKNOWN) {
                    c->out_path_len = saved_len;
                    if (saved_len != OUT_PATH_UNKNOWN && saved_len <= 32)
                        memcpy(c->out_path, saved_path, saved_len);
                }
                if (r != MSG_SEND_FAILED) {
                    for (int j = 0; j < MAX_DISCOVERY_PENDING; j++) {
                        if (!_discovery_pending[j].in_use) {
                            _discovery_pending[j].tag = tag;
                            strncpy(_discovery_pending[j].dest_name, name,
                                    sizeof(_discovery_pending[j].dest_name) - 1);
                            _discovery_pending[j].dest_name[sizeof(_discovery_pending[j].dest_name) - 1] = '\0';
                            _discovery_pending[j].in_use = true;
                            _discovery_pending[j].completed = false;
                            _discovery_pending[j].started_at_ms = _ms->getMillis();
                            return tag;
                        }
                    }
                }
                return 0;
            }
        }
        return 0;
    }

#if defined(ESP32_PLATFORM)
    int SigurdMeshV2::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
        char path[48];
        snprintf(path, sizeof(path), "/blob_%02x%02x%02x%02x",
                 key_len > 0 ? key[0] : 0, key_len > 1 ? key[1] : 0,
                 key_len > 2 ? key[2] : 0, key_len > 3 ? key[3] : 0);
        if (!SPIFFS.exists(path)) return 0;
        File f = SPIFFS.open(path, "r");
        if (!f) return 0;
        int len = f.read(dest_buf, 4096);
        f.close();
        return len;
    }
#endif  // ESP32_PLATFORM

#if defined(ESP32_PLATFORM)
    bool SigurdMeshV2::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) {
        char path[48];
        snprintf(path, sizeof(path), "/blob_%02x%02x%02x%02x",
                 key_len > 0 ? key[0] : 0, key_len > 1 ? key[1] : 0,
                 key_len > 2 ? key[2] : 0, key_len > 3 ? key[3] : 0);
        if (SPIFFS.exists(path)) SPIFFS.remove(path);
        File f = SPIFFS.open(path, "w");
        if (!f) return false;
        size_t written = f.write(src_buf, len);
        f.close();
        return written == (size_t)len;
    }
#endif  // ESP32_PLATFORM

    bool SigurdMeshV2::allowPacketForward(const ::mesh::Packet* packet) {
        if (sigurdos::prefs_get().client_repeat == 0) return false;
        // Deny forward if packet matches a region with DENY_FLOOD flag
        if (sigurdos::mesh::regionDeniesFlood(const_cast<::mesh::Packet*>(packet))) return false;
        return true;
    }

    bool SigurdMeshV2::shouldAutoAddContactType(uint8_t type) const {
        return type == ADV_TYPE_CHAT || type == ADV_TYPE_ROOM || type == ADV_TYPE_REPEATER || type == ADV_TYPE_NONE;
    }

    int SigurdMeshV2::addLoginEntry(const char* name) {
        int idx = findLoginEntry(name);
        if (idx >= 0) return idx;
        for (int i = 0; i < MAX_LOGIN_ENTRIES; i++) {
            if (!_login_entries[i].in_use) {
                strncpy(_login_entries[i].contact_name, name,
                        sizeof(_login_entries[i].contact_name) - 1);
                _login_entries[i].contact_name[sizeof(_login_entries[i].contact_name) - 1] = '\0';
                _login_entries[i].status = LOGIN_PENDING;
                _login_entries[i].started_at_ms = millis();
                _login_entries[i].in_use = true;
                return i;
            }
        }
        return -1; // table full
    }

    void SigurdMeshV2::sendLoginTo(const ::ContactInfo& contact, const char* password) {
        if (!password) return;
        uint32_t est_timeout = 0;
        int r = BaseChatMesh::sendLogin(contact, password, est_timeout);
        if (r != MSG_SEND_FAILED) {
            addLoginEntry(contact.name);
#if SIGURDOS_DEBUG_MESH
            Serial.printf("[mesh] Login sent to %s (result=%d, timeout=%u)\n",
                          contact.name, r, est_timeout);
#endif
        }
    }

    void SigurdMeshV2::sendLogoutTo(const ::ContactInfo& contact) {
        BaseChatMesh::stopConnection(contact.id.pub_key);
        removeLoginEntry(contact.name);
#if SIGURDOS_DEBUG_MESH
        Serial.printf("[mesh] Logged out from %s\n", contact.name);
#endif
    }

    bool SigurdMeshV2::sendCommandDataTo(const ::ContactInfo& contact, const char* text) {
        if (!text || !text[0]) return false;
        uint32_t est_timeout = 0;
        uint32_t ts = getRTCClock()->getCurrentTime();
        int r = BaseChatMesh::sendCommandData(contact, ts, 0, text, est_timeout);
        if (r != MSG_SEND_FAILED) {
#if SIGURDOS_DEBUG_MESH
            Serial.printf("[mesh] Command sent to %s (result=%d)\n", contact.name, r);
#endif
            return true;
        }
        return false;
    }

    // slot 0; an empty name marks the end.
    int SigurdMeshV2::getChannelCount() {
        int n = 0;
        ChannelDetails tmp;
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
            if (!BaseChatMesh::getChannel(i, tmp)) break;
            if (tmp.name[0] == '\0') break;
            n++;
        }
        return n;
    }

    const ChannelDetails* SigurdMeshV2::getChannel(int idx) {
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return nullptr;
        if (!BaseChatMesh::getChannel(idx, _ch_cache)) return nullptr;
        if (_ch_cache.name[0] == '\0') return nullptr;
        return &_ch_cache;
    }

    const ::ContactInfo* SigurdMeshV2::getContact(int idx) {
        if (idx < 0 || idx >= getNumContacts()) return nullptr;
        if (!getContactByIdx((uint32_t)idx, _contact_cache)) return nullptr;
        return &_contact_cache;
    }

    bool SigurdMeshV2::removeContact(int idx) {
        ::ContactInfo tmp;
        if (!getContactByIdx((uint32_t)idx, tmp)) return false;
        return BaseChatMesh::removeContact(tmp);
    }

    bool SigurdMeshV2::resetPathTo(int idx) {
        ::ContactInfo tmp;
        if (!getContactByIdx((uint32_t)idx, tmp)) return false;
        // resetPathTo() mutates the passed reference, so operate on the live
        // stored contact (returned by lookupContactByPubKey), not a copy.
        ::ContactInfo* live = lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE);
        if (!live) return false;
        BaseChatMesh::resetPathTo(*live);
        return true;
    }

    bool SigurdMeshV2::removeChannel(int idx) {
        int n = getChannelCount();
        if (idx < 0 || idx >= n) return false;
        ChannelDetails tmp;
        for (int i = idx; i < n - 1; i++) {
            if (BaseChatMesh::getChannel(i + 1, tmp)) BaseChatMesh::setChannel(i, tmp);
        }
        ChannelDetails empty{};
        BaseChatMesh::setChannel(n - 1, empty);
        return true;
    }

    int SigurdMeshV2::decode_b64(const char* in, size_t in_len, uint8_t* out, size_t out_cap) {
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

    bool SigurdMeshV2::addChannelBool(const char* name, const char* psk_base64) {
        if (!name || !name[0]) return false;
        int idx = getChannelCount();
        if (idx >= MAX_GROUP_CHANNELS) return false;
        for (int i = 0; i < idx; i++) {
            ChannelDetails t;
            if (BaseChatMesh::getChannel(i, t) && strcmp(t.name, name) == 0) return true;
        }
        ChannelDetails cd{};
        int len = decode_b64(psk_base64, strlen(psk_base64),
                             cd.channel.secret, sizeof(cd.channel.secret));
        if (len != 32 && len != 16) return false;
        strncpy(cd.name, name, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        return BaseChatMesh::setChannel(idx, cd);  // setChannel recomputes hash
    }

    bool SigurdMeshV2::addHashtagChannel(const char* name) {
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
        ChannelDetails cd{};
        ::mesh::Utils::sha256(cd.channel.secret, CIPHER_KEY_SIZE,
                              (const uint8_t*)normalized, strlen(normalized));
        strncpy(cd.name, normalized, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        return BaseChatMesh::setChannel(idx, cd);  // setChannel recomputes hash
    }

    bool SigurdMeshV2::loadChannel(const uint8_t* secret, size_t secret_len, const uint8_t* hash, const char* name) {
        if (!name || !name[0]) return false;
        int idx = getChannelCount();
        if (idx >= MAX_GROUP_CHANNELS) return false;
        ChannelDetails cd{};
        size_t cpy = secret_len < sizeof(cd.channel.secret) ? secret_len
                                                            : sizeof(cd.channel.secret);
        memcpy(cd.channel.secret, secret, cpy);
        strncpy(cd.name, name, sizeof(cd.name) - 1);
        cd.name[sizeof(cd.name) - 1] = '\0';
        // setChannel() recomputes the hash from the secret (same derivation
        // used when the channel was created), so the stored hash is reproduced.
        return BaseChatMesh::setChannel(idx, cd);
    }

    bool SigurdMeshV2::sendTextTo(const char* name, const char* text) {
        if (!name || !text) return false;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t expected_ack = 0, est_timeout = 0;
                uint32_t ts = getRTCClock()->getCurrentTime();
                int r = BaseChatMesh::sendMessage(tmp, ts, 0, text,
                                                  expected_ack, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    addPendingAck(name, ts, expected_ack);
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool SigurdMeshV2::sendTextTo(const char* name, const char* text, uint32_t fixed_ts) {
        if (!name || !text) return false;
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t expected_ack = 0, est_timeout = 0;
                int r = BaseChatMesh::sendMessage(tmp, fixed_ts, 0, text,
                                                  expected_ack, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    addPendingAck(name, fixed_ts, expected_ack);
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool SigurdMeshV2::sendGroupText(int idx, const char* text) {
        if (idx < 0 || idx >= getChannelCount() || !text || !text[0]) return false;
        ChannelDetails cd;
        if (!BaseChatMesh::getChannel(idx, cd)) return false;
        uint32_t ts = getRTCClock()->getCurrentTime();
        return BaseChatMesh::sendGroupMessage(ts, cd.channel, _own_name, text,
                                              (int)strlen(text));
    }

    bool SigurdMeshV2::sendGroupDataToChannel(int idx, uint16_t data_type, const uint8_t* data, int data_len) {
        return sendGroupDataToChannel(idx, nullptr, OUT_PATH_UNKNOWN,
                                      data_type, data, data_len);
    }

    bool SigurdMeshV2::sendGroupDataToChannel(int idx, const uint8_t* path, uint8_t path_len, uint16_t data_type, const uint8_t* data, int data_len) {
        if (idx < 0 || idx >= getChannelCount()) return false;
        if (data_len > 0 && !data) return false;
        if (path_len != OUT_PATH_UNKNOWN && !::mesh::Packet::isValidPathLen(path_len)) return false;
        if (path_len != OUT_PATH_UNKNOWN && !path) return false;
        ChannelDetails cd;
        if (!BaseChatMesh::getChannel(idx, cd)) return false;
        uint8_t* route_path = path_len == OUT_PATH_UNKNOWN ? nullptr : const_cast<uint8_t*>(path);
        return BaseChatMesh::sendGroupData(cd.channel, route_path, path_len, data_type, data, data_len);
    }

    void SigurdMeshV2::onChannelDataRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* pkt, uint16_t data_type, const uint8_t* data, size_t data_len) {
        // Resolve channel name/index once; the companion bridge should still
        // receive the datagram even if the local debug buffer is full.
        char chname[32] = "[group]";
        int channel_idx = -1;
        for (int i = 0; i < getChannelCount(); i++) {
            ChannelDetails cd;
            if (BaseChatMesh::getChannel(i, cd) &&
                memcmp(cd.channel.hash, channel.hash, sizeof(channel.hash)) == 0) {
                strncpy(chname, cd.name, sizeof(chname) - 1);
                chname[sizeof(chname) - 1] = '\0';
                channel_idx = i;
                break;
            }
        }

        uint8_t companion_path_len = OUT_PATH_UNKNOWN;
        int8_t companion_snr = 0;
        if (pkt) {
            companion_path_len = pkt->isRouteFlood() ? (uint8_t)pkt->path_len : OUT_PATH_UNKNOWN;
            companion_snr = pkt->_snr;
        }
        sigurdos::mesh::mesh_v2_group_data_push(
            channel_idx >= 0 ? (uint8_t)channel_idx : 0xFF,
            companion_path_len,
            companion_snr,
            data_type,
            data,
            data_len);

        // Store in receive buffer
        if (_n_grp_data_recv < MAX_GROUP_DATA_RECV) {
            GroupDataEntry& e = _grp_data_recv[_n_grp_data_recv++];
            e.data_type = data_type;
            e.data_len = (data_len < sizeof(e.data)) ? (uint8_t)data_len : sizeof(e.data);
            if (e.data_len > 0 && data) memcpy(e.data, data, e.data_len);
            strncpy(e.channel_name, chname, sizeof(e.channel_name) - 1);
            e.channel_name[sizeof(e.channel_name) - 1] = '\0';
            e.timestamp = getRTCClock()->getCurrentTime();
            e.valid = true;

#if SIGURDOS_DEBUG_MESH
            Serial.printf("[mesh] Group data recv on %s type=0x%04x len=%d\n",
                          chname, data_type, (int)data_len);
#endif
        } else {
#if SIGURDOS_DEBUG_MESH
            Serial.printf("[mesh] Group data recv buffer full (dropped type=0x%04x)\n",
                          data_type);
#endif
        }
    }

    int SigurdMeshV2::getGroupDataCount() const { return _n_grp_data_recv; }

    const SigurdMeshV2::GroupDataEntry* SigurdMeshV2::getGroupDataEntry(int idx) const {
        if (idx < 0 || idx >= _n_grp_data_recv) return nullptr;
        return &_grp_data_recv[idx];
    }

    void SigurdMeshV2::clearGroupData() { _n_grp_data_recv = 0; }

    bool SigurdMeshV2::sendAnonMessage(const uint8_t* pub_key, const char* text) {
        if (!pub_key || !text || !text[0]) return false;

        // Build a temporary ContactInfo with the given pubkey
        ::ContactInfo tmp{};
        memcpy(tmp.id.pub_key, pub_key, PUB_KEY_SIZE);
        tmp.out_path_len = OUT_PATH_UNKNOWN;
        tmp.type = ADV_TYPE_CHAT;

        uint32_t tag = 0, est_timeout = 0;
        uint32_t ts = getRTCClock()->getCurrentTime();

        // Data format: [4-byte timestamp][null-terminated text]
        uint8_t buf[256];
        memcpy(buf, &ts, 4);
        size_t tlen = strlen(text);
        if (tlen > 250) tlen = 250;
        memcpy(buf + 4, text, tlen);
        buf[4 + tlen] = '\0';

        int r = BaseChatMesh::sendAnonReq(tmp, buf, 5 + tlen, tag, est_timeout);
        if (r != MSG_SEND_FAILED) {
#if SIGURDOS_DEBUG_MESH
            Serial.printf("[mesh] Anon msg sent to pubkey %02x%02x... (result=%d, tag=%u)\n",
                          pub_key[0], pub_key[1], r, tag);
#endif
            return true;
        }
        return false;
    }

    int SigurdMeshV2::hexToBytes(const char* hex, uint8_t* out, size_t out_max) {
        if (!hex || !out) return 0;
        size_t hlen = strlen(hex);
        if (hlen % 2 != 0 || hlen / 2 > out_max) return 0;
        int o = 0;
        for (size_t i = 0; i < hlen; i += 2) {
            char hi = hex[i];
            char lo = hex[i + 1];
            uint8_t b = 0;
            if (hi >= '0' && hi <= '9') b = (hi - '0') << 4;
            else if (hi >= 'a' && hi <= 'f') b = (hi - 'a' + 10) << 4;
            else if (hi >= 'A' && hi <= 'F') b = (hi - 'A' + 10) << 4;
            else return 0;
            if (lo >= '0' && lo <= '9') b |= (lo - '0');
            else if (lo >= 'a' && lo <= 'f') b |= (lo - 'a' + 10);
            else if (lo >= 'A' && lo <= 'F') b |= (lo - 'A' + 10);
            else return 0;
            out[o++] = b;
        }
        return o;
    }

    void SigurdMeshV2::broadcastAdvert(const char* name, uint8_t adv_type) {
        AdvertDataBuilder builder(adv_type, name);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt, 0, pathHashSize());
    }

    void SigurdMeshV2::broadcastAdvert(const char* name, double lat, double lon, uint8_t adv_type) {
        AdvertDataBuilder builder(adv_type, name, lat, lon);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        if (pkt) sendFlood(pkt, 0, pathHashSize());
    }

    float SigurdMeshV2::getPacketSNR() const {
        return _radio ? _radio->getLastSNR() : 0.0f;
    }

    uint8_t SigurdMeshV2::getAdvertPathLen(const char* name) const {
        if (!name) return 0;
        // Find by name match in the advert path table
        for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
            if (_advert_paths[i].recv_timestamp > 0
                && strcmp(_advert_paths[i].name, name) == 0) {
                return _advert_paths[i].path_len;
            }
        }
        return 0;
    }

    const SigurdMeshV2::AdvertPathEntry*
    SigurdMeshV2::getAdvertPathByKey(const uint8_t* pub_key) const {
        if (!pub_key) return nullptr;
        for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
            if (_advert_paths[i].recv_timestamp > 0 &&
                memcmp(_advert_paths[i].pubkey_prefix, pub_key,
                       sizeof(_advert_paths[i].pubkey_prefix)) == 0) {
                return &_advert_paths[i];
            }
        }
        return nullptr;
    }

    void SigurdMeshV2::setActiveScope(const uint8_t* key16) {
        if (key16) {
            memcpy(_active_scope.key, key16, 16);
            _send_unscoped = false;
        } else {
            clearActiveScope();
        }
    }

} // namespace mesh
} // namespace sigurdos
