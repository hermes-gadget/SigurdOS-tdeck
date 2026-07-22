// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// SigurdMeshV2 — method implementations split from sigurd_mesh_v2.h
//
#include "sigurd_mesh_v2.h"
#include "companion_message_policy.h"
#include "incoming_message_policy.h"
#include "anonymous_message_policy.h"
#include "advert_blob.h"
#include "advert_blob_store.h"
#include "client_repeat_policy.h"
#include "path_discovery.h"
#include "ping_result_policy.h"
#include "control_parser.h"
#include "login_response.h"
#include "utils/utf8_util.h"
#include "channel_validation.h"
#include "channel_slot_policy.h"
#include "strict_base64.h"
#include "telemetry_response_policy.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "mesh_wrapper.h"
#include "hal/prefs.h"
#include "hal/battery.h"
#include "hal/gps.h"
#include "hal/tdeck_board.h"
#include "../diagnostics/debug_cfg.h"

#if defined(ESP32_PLATFORM)
#include <esp_random.h>
#endif

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
        if (c.out_path_len == OUT_PATH_UNKNOWN ||
            !path::encodedLengthValid(c.out_path_len)) return false;
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

    bool SigurdMeshV2::sendRawDataCompanion(const uint8_t* path, uint8_t path_len,
                                            const uint8_t* data, size_t data_len) {
        if (path_len > 63 || (path_len > 0 && !path) || !data ||
            data_len < 4 || data_len > MAX_PACKET_PAYLOAD) {
            return false;
        }
        ::mesh::Packet* pkt = createRawData(data, data_len);
        if (!pkt) return false;
        sendDirect(pkt, path, path_len);
        return true;
    }

    bool SigurdMeshV2::sendControlDataCompanion(const uint8_t* data, size_t data_len) {
        if (!data || data_len == 0 || data_len > MAX_PACKET_PAYLOAD ||
            (data[0] & 0x80) == 0) {
            return false;
        }
        ::mesh::Packet* pkt = createControlData(data, data_len);
        if (!pkt) return false;
        sendZeroHop(pkt);
        return true;
    }

    int SigurdMeshV2::sendLoginCompanion(const ::ContactInfo& contact, const char* password, uint32_t& est_timeout) {
        est_timeout = 0;
        const int previous_idx = findLoginEntry(contact.id.pub_key);
        const LoginEntry previous =
            previous_idx >= 0 ? _login_entries[previous_idx] : LoginEntry{};
        const int login_idx = addLoginEntry(contact);
        if (login_idx < 0) return MSG_SEND_FAILED;

        int r = BaseChatMesh::sendLogin(contact, password ? password : "", est_timeout);
        if (r == MSG_SEND_FAILED) {
            _login_entries[login_idx] =
                previous_idx >= 0 ? previous : LoginEntry{};
            return r;
        }

        _login_entries[login_idx].started_at_ms = _ms->getMillis();
        _login_entries[login_idx].timeout_ms =
            login_session::normalizeTimeout(est_timeout);
        return r;
    }

    bool SigurdMeshV2::companionHasConnection(const uint8_t* pub_key) {
        return pub_key && BaseChatMesh::hasConnectionTo(pub_key);
    }

    void SigurdMeshV2::companionStopConnection(const uint8_t* pub_key) {
        if (!pub_key) return;
        BaseChatMesh::stopConnection(pub_key);
        removeLoginEntry(pub_key);
    }

    int SigurdMeshV2::exportSelfContact(const char* name, uint8_t* out, size_t out_cap) {
        if (!out || out_cap == 0) return 0;
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        ::mesh::Packet* pkt = nullptr;
        if (p.advert_loc_policy != 0 && sigurdos_gps_has_fix()) {
            pkt = createSelfAdvert(name ? name : "", sigurdos_gps_latitude(),
                                   sigurdos_gps_longitude());
        } else if (p.advert_loc_policy != 0 && p.advert_location_valid) {
            pkt = createSelfAdvert(name ? name : "",
                                   (double)p.advert_lat / 1000000.0,
                                   (double)p.advert_lon / 1000000.0);
        } else {
            pkt = createSelfAdvert(name ? name : "");
        }
        if (!pkt) return 0;
        pkt->header |= ROUTE_TYPE_FLOOD;
        uint8_t encoded[SIGURDOS_ADVERT_BLOB_MAX_LEN];
        const uint8_t n = pkt->writeTo(encoded);
        releasePacket(pkt);
        if (!advertBlobFitsOutput(n, out_cap)) return 0;
        memcpy(out, encoded, n);
        return (int)n;
    }

    void SigurdMeshV2::onTraceRecv(::mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags, const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
        const uint8_t hash_size = (uint8_t)(1U << (flags & 0x03));
        if (path_len > MAX_PATH_SIZE || path_len % hash_size != 0 ||
            (path_len > 0 && (!path_snrs || !path_hashes))) return;
        const uint8_t snr_count = path_len / hash_size;

        // Fan out the full trace result to the phone app after validating the
        // two independently sized arrays.
        int8_t final_snr_q = (int8_t)((pkt ? pkt->getSNR() : 0.0f) * 4.0f);
        sigurdos::mesh::mesh_v2_companion_trace_push(tag, auth_code, flags,
                                                     path_hashes, path_snrs,
                                                     path_len, final_snr_q);
        _last_trace_tag = tag;
        _last_trace_len = snr_count;
        _last_trace_hash_bytes = path_len;
        if (snr_count > 0) memcpy(_last_trace_snrs, path_snrs, snr_count);
        if (path_len > 0) memcpy(_last_trace_hashes, path_hashes, path_len);
        _has_trace_result = true;
    }

    void SigurdMeshV2::getTracePath(uint8_t* snrs_out, uint8_t* hashes_out) {
        if (snrs_out && _last_trace_len > 0)
            memcpy(snrs_out, _last_trace_snrs, _last_trace_len);
        if (hashes_out && _last_trace_hash_bytes > 0)
            memcpy(hashes_out, _last_trace_hashes, _last_trace_hash_bytes);
    }

    bool SigurdMeshV2::sendPingNearby() {
        uint32_t now = _ms->getMillis();
        if (_ping_last_at != 0 && now - _ping_last_at < PING_COOLDOWN_MS) return false;
        _ping_tag = 0;
        for (int attempt = 0; attempt < 4 && _ping_tag == 0; ++attempt) {
#if defined(ESP32_PLATFORM)
            _ping_tag = esp_random();
#else
            getRNG()->random(reinterpret_cast<uint8_t*>(&_ping_tag), sizeof(_ping_tag));
#endif
        }
        if (_ping_tag == 0) return false;
        _ping_sent_at = now;
        _ping_last_at = now;
        _ping_n_results = 0;

        char ping[20];
        int n = snprintf(ping, sizeof(ping), "PING:%08lx", (unsigned long)_ping_tag);
        if (n <= 0 || (size_t)n >= sizeof(ping)) return false;

        ::mesh::Packet* pkt = createRawData((uint8_t*)ping, (size_t)n);
        if (!pkt) return false;
        pkt->payload[0] |= 0x80;   // control-disco bit
        sendZeroHop(pkt);
        return true;
    }

    void SigurdMeshV2::onControlDataRecv(::mesh::Packet* pkt) {
        if (!pkt || pkt->payload_len == 0 || pkt->payload_len > MAX_PACKET_PAYLOAD) return;
        sigurdos::mesh::mesh_v2_companion_control_data_push(
            (int8_t)(_radio->getLastSNR() * 4.0f),
            (int8_t)_radio->getLastRSSI(),
            (uint8_t)pkt->path_len,
            pkt->payload,
            pkt->payload_len);
        if (pkt->payload_len < 5) return;
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
            uint32_t now_ms = _ms->getMillis();
            if (!pingWindowActive(_ping_sent_at, now_ms, PING_WINDOW_MS)) return;

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
            if (name_len == 0) return;

            const uint8_t* rssi_data = reinterpret_cast<const uint8_t*>(rssi_start + 1);
            const size_t rssi_len = pkt->payload_len -
                static_cast<size_t>((rssi_start + 1) -
                                    reinterpret_cast<const char*>(pkt->payload));
            int reported_rssi = 0;
            if (!parse_bounded_int(rssi_data, rssi_len, &reported_rssi)) return;
            (void)reported_rssi;  // Legacy field is untrusted; use receiver radio data.

            char name[32];
            memcpy(name, remaining, name_len);
            name[name_len] = '\0';
            recordUnauthenticatedPingHint(_ping_results, _ping_n_results,
                                           PING_RESULTS_MAX, name,
                                           (int)_radio->getLastRSSI());
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

    void SigurdMeshV2::onRawDataRecv(::mesh::Packet* pkt) {
        if (!pkt || pkt->payload_len > MAX_PACKET_PAYLOAD) return;
        sigurdos::mesh::mesh_v2_companion_raw_data_push(
            (int8_t)(_radio->getLastSNR() * 4.0f),
            (int8_t)_radio->getLastRSSI(),
            pkt->payload,
            pkt->payload_len);
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
                            _pending_reqs[j].companion_binary = false;
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
                            _pending_reqs[j].companion_binary = false;
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

    int SigurdMeshV2::sendBinaryRequestCompanion(const ::ContactInfo& contact,
                                                  const uint8_t* data,
                                                  uint8_t data_len,
                                                  uint32_t& tag,
                                                  uint32_t& est_timeout) {
        if (!data || data_len == 0) return MSG_SEND_FAILED;
        int pending = -1;
        for (int i = 0; i < MAX_PENDING_REQUESTS; ++i) {
            if (!_pending_reqs[i].in_use) {
                pending = i;
                break;
            }
        }
        if (pending < 0) return MSG_SEND_FAILED;

        const int result = BaseChatMesh::sendRequest(
            contact, data, data_len, tag, est_timeout);
        if (result == MSG_SEND_FAILED) return result;

        PendingRequest& request = _pending_reqs[pending];
        request.tag = tag;
        request.req_type = 0;
        request.companion_binary = true;
        std::strncpy(request.dest_name, contact.name,
                     sizeof(request.dest_name) - 1);
        request.dest_name[sizeof(request.dest_name) - 1] = '\0';
        request.channel_name[0] = '\0';
        request.sent_at_ms = _ms->getMillis();
        request.in_use = true;
        return result;
    }

    int SigurdMeshV2::sendAnonRequestCompanion(const uint8_t* pub_key,
                                                const uint8_t* data,
                                                uint8_t data_len,
                                                uint32_t& tag,
                                                uint32_t& est_timeout) {
        if (!pub_key || !data || data_len == 0 ||
            !anonymousRequestFits(data_len)) {
            return MSG_SEND_FAILED;
        }
        int pending = -1;
        for (int i = 0; i < MAX_PENDING_REQUESTS; ++i) {
            if (!_pending_reqs[i].in_use) {
                pending = i;
                break;
            }
        }
        if (pending < 0) return MSG_SEND_FAILED;

        ::ContactInfo transient{};
        ::ContactInfo* recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
        if (!recipient) {
            std::memcpy(transient.id.pub_key, pub_key, PUB_KEY_SIZE);
            transient.out_path_len = 0;
            transient.type = ADV_TYPE_NONE;
            transient.lastmod = getRTCClock()->getCurrentTime();
            if (!addContact(transient)) return MSG_SEND_FAILED;
            recipient = &transient;
        }

        const int result = BaseChatMesh::sendAnonReq(
            *recipient, data, data_len, tag, est_timeout);
        if (result == MSG_SEND_FAILED) return result;

        PendingRequest& request = _pending_reqs[pending];
        request.tag = tag;
        request.req_type = 0;
        request.companion_binary = true;
        std::strncpy(request.dest_name, recipient->name,
                     sizeof(request.dest_name) - 1);
        request.dest_name[sizeof(request.dest_name) - 1] = '\0';
        request.channel_name[0] = '\0';
        request.sent_at_ms = _ms->getMillis();
        request.in_use = true;
        return result;
    }

    void SigurdMeshV2::cancelCompanionBinaryRequests() {
        for (int i = 0; i < MAX_PENDING_REQUESTS; ++i) {
            if (_pending_reqs[i].in_use && _pending_reqs[i].companion_binary) {
                _pending_reqs[i].in_use = false;
                _pending_reqs[i].companion_binary = false;
            }
        }
    }

    void SigurdMeshV2::cancelCompanionBinaryRequest(uint32_t tag) {
        if (tag == 0) return;
        for (int i = 0; i < MAX_PENDING_REQUESTS; ++i) {
            if (_pending_reqs[i].in_use && _pending_reqs[i].companion_binary &&
                _pending_reqs[i].tag == tag) {
                _pending_reqs[i].in_use = false;
                _pending_reqs[i].companion_binary = false;
                return;
            }
        }
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
                            _pending_reqs[j].companion_binary = false;
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
            sigurdos::utf8_copy_truncate(e.text, sizeof(e.text), text);
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
        if (path && sigurdos::mesh::path::encodedLengthValid(path_len) &&
            ::mesh::Packet::isValidPathLen(path_len)) {
            storeAdvertPath(contact.id.pub_key, contact.name, path_len, path);
        }

        // Fan out to the phone app (NEW_ADVERT for a new contact, else ADVERT).
        sigurdos::mesh::mesh_v2_companion_advert_push(&contact, is_new);

        // Advert discovery mutates the contact table with no UI event site;
        // schedule a debounced save so the contact survives a power cut.
        sigurdos::mesh::markContactsDirty();

#if SIGURDOS_DEBUG_MESH
        Serial.printf("[mesh] %s contact: %s (type=%d)\n",
                      is_new ? "new" : "updated", contact.name, contact.type);
#endif
    }

    void SigurdMeshV2::onContactOverwrite(const uint8_t* pub_key) {
        deleteBlobByKey(SPIFFS, pub_key, PUB_KEY_SIZE);
        sigurdos::mesh::mesh_v2_companion_contact_deleted_push(pub_key);
    }

    void SigurdMeshV2::addPendingAck(const char* name, uint32_t ts,
                                     uint32_t expected_ack,
                                     uint32_t estimated_timeout_ms) {
        if (!name || !name[0] || expected_ack == 0) return;
        expirePendingAcks();
        const uint32_t now = _ms->getMillis();
        const uint32_t expires = now + pendingAckLifetimeMs(estimated_timeout_ms);
        for (int i = 0; i < MAX_PENDING_ACKS; i++) {
            if (!_pending_acks[i].in_use) {
                strncpy(_pending_acks[i].dest_name, name, sizeof(_pending_acks[i].dest_name)-1);
                _pending_acks[i].dest_name[sizeof(_pending_acks[i].dest_name)-1] = '\0';
                _pending_acks[i].timestamp = ts;
                _pending_acks[i].expected_ack = expected_ack;
                _pending_acks[i].sent_at_ms = now;
                _pending_acks[i].expires_at_ms = expires;
                _pending_acks[i].in_use = true;
                return;
            }
        }
        // Table full — evict oldest entry; count drops for observability
        int oldest = 0;
        uint32_t oldest_age = now - _pending_acks[0].sent_at_ms;
        for (int i = 1; i < MAX_PENDING_ACKS; i++) {
            const uint32_t age = now - _pending_acks[i].sent_at_ms;
            if (age > oldest_age) {
                oldest = i;
                oldest_age = age;
            }
        }
        _ack_drop_count++;
        sigurdos::mesh::registerConfirmationLost(_pending_acks[oldest].dest_name,
                                                  _pending_acks[oldest].timestamp);
        strncpy(_pending_acks[oldest].dest_name, name, sizeof(_pending_acks[oldest].dest_name)-1);
        _pending_acks[oldest].dest_name[sizeof(_pending_acks[oldest].dest_name)-1] = '\0';
        _pending_acks[oldest].timestamp = ts;
        _pending_acks[oldest].expected_ack = expected_ack;
        _pending_acks[oldest].sent_at_ms = now;
        _pending_acks[oldest].expires_at_ms = expires;
        _pending_acks[oldest].in_use = true;
    }

    void SigurdMeshV2::expirePendingAcks() {
        const uint32_t now = _ms->getMillis();
        for (int i = 0; i < MAX_PENDING_ACKS; i++) {
            PendingAck& pending = _pending_acks[i];
            if (!pending.in_use ||
                !pendingAckDeadlineReached(now, pending.expires_at_ms)) {
                continue;
            }
            pending.in_use = false;
            _ack_expired_count++;
            sigurdos::mesh::registerConfirmationLost(pending.dest_name,
                                                      pending.timestamp);
        }
    }

    ::ContactInfo* SigurdMeshV2::processAck(const uint8_t* data) {
        if (!data) return nullptr;
        expirePendingAcks();
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
                // Return the correct contact for BaseChatMesh internal processing
                // (e.g. handleReturnPathRetry). Must match the contact that actually
                // sent the ACK, not the first entry in the contact list (BUG-004).
                for (int j = 0; j < getNumContacts(); j++) {
                    if (getContactByIdx((uint32_t)j, _contact_cache) &&
                        strcmp(_contact_cache.name, _pending_acks[i].dest_name) == 0) {
                        return &_contact_cache;
                    }
                }
                return nullptr;  // contact gone — can't return the right one
            }
        }
        return ack_val == 0 ? nullptr : BaseChatMesh::checkConnectionsAck(data);
    }

    void SigurdMeshV2::onMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
        BaseChatMesh::markConnectionActive(contact);
        int rssi = (int)_radio->getLastRSSI();
        float snr = pkt ? pkt->getSNR() : _radio->getLastSNR();
        updateSignalSample(contact.id.pub_key, rssi, snr);
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr,
                                           sender_timestamp, companion_path_len,
                                           contact.id.pub_key,
                                           sigurdos::mesh::COMPANION_TEXT_PLAIN);
    }

    void SigurdMeshV2::onCommandDataRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
        BaseChatMesh::markConnectionActive(contact);
        sigurdos::mesh::pushCmdResponse(contact.name, text, sender_timestamp);
        // Store as COMPANION_TXT_CLI_DATA with the raw app payload text so the
        // companion bridge emits the correct txt_type to official apps. The
        // normal chat queue skips CLI data; pushCmdResponse feeds the admin
        // transcript instead.
        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr,
                                           sender_timestamp, companion_path_len,
                                           contact.id.pub_key,
                                           sigurdos::mesh::COMPANION_TEXT_CLI_DATA);
    }

    void SigurdMeshV2::onAnonDataRecv(::mesh::Packet* pkt, const uint8_t* secret, const ::mesh::Identity& sender, uint8_t* data, size_t len) {
        uint32_t sender_timestamp = 0;
        char text[160];
        if (!sigurdos::mesh::decodeAnonymousChat(
                data, len, sender_timestamp, text, sizeof(text))) return;

        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;

        char fallback[20];
        sigurdos::mesh::formatAnonymousSender(
            sender.pub_key, fallback, sizeof(fallback));

        sigurdos::mesh::mesh_v2_queue_push(
            fallback, "", text, rssi, snr, sender_timestamp,
            companion_path_len, sender.pub_key,
            sigurdos::mesh::COMPANION_TEXT_PLAIN);
    }

    void SigurdMeshV2::onSignedMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t* sender_prefix, const char* text) {
        BaseChatMesh::markConnectionActive(contact);
        int rssi = pkt ? (int)_radio->getLastRSSI() : 0;
        float snr = pkt ? pkt->getSNR() : 0.0f;
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(contact.name, "", text, rssi, snr,
                                           sender_timestamp, companion_path_len,
                                           contact.id.pub_key,
                                           sigurdos::mesh::COMPANION_TEXT_SIGNED_PLAIN,
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

        char sender_name[32];
        char msg_text[160];
        sigurdos::mesh::parseGroupText(
            text, sender_name, sizeof(sender_name), msg_text, sizeof(msg_text));
        // Channel messages are flood-routed: forward the real hop path so the
        // app shows the true hop count (0xFF only for a direct/unknown route).
        uint8_t companion_path_len =
            (pkt && pkt->isRouteFlood()) ? (uint8_t)pkt->path_len : 0xFF;
        sigurdos::mesh::mesh_v2_queue_push(sender_name, chname, msg_text, rssi, snr,
                                           timestamp, companion_path_len);
    }

    uint8_t SigurdMeshV2::onContactRequest(const ::ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) {
        // C-14/A-12/E-04: reflect the RF tag and enforce the packed base,
        // location, and environment permission modes independently.
        if (len >= 1 && data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
            const sigurdos::NodePrefs& prefs = sigurdos::prefs_get();
            const uint8_t inverse_mask = len >= 2 ? data[1] : 0;
            const uint8_t permissions = telemetry_policy::resolvePermissions(
                prefs.telemetry_modes, contact.flags, inverse_mask);
            const telemetry_policy::GpsSample gps{
                sigurdos_gps_has_fix(), sigurdos_gps_latitude(),
                sigurdos_gps_longitude(), sigurdos_gps_altitude_m()};
            return static_cast<uint8_t>(telemetry_policy::buildResponse(
                reply, 64, sender_timestamp, permissions,
                sigurdos_battery_mv(), gps));
        }
        return 0;  // unknown request type
    }

    void SigurdMeshV2::onContactResponse(const ::ContactInfo& contact, const uint8_t* data, uint8_t len) {
        if (!data || len < 4) return;
        BaseChatMesh::markConnectionActive(contact);
        uint32_t tag;
        memcpy(&tag, data, 4);

        // ── Check for login response ──────────────────
        // Format: bytes 0-3=tag, byte4=RESP_SERVER_LOGIN_OK(0)=success,
        //         byte5=keep_alive_secs/16, byte6=permissions, byte7=ACL (v7+)
        // Legacy: bytes 4-5 = "OK" (2 chars)
        // Only a pending login may consume this response. Active sessions also
        // receive status, telemetry, and binary replies from the same contact.
        int login_idx = findLoginEntry(contact.id.pub_key);
        if (login_idx >= 0 && _login_entries[login_idx].in_use &&
            _login_entries[login_idx].status == LOGIN_PENDING) {
            const login_response::Parsed response = login_response::parse(data, len);
            if (response.format == login_response::Format::CurrentSuccess) {
                const uint16_t keep_alive_secs = response.keep_alive_secs;
                const uint8_t perm = response.permission;
                const uint8_t acl = response.acl_permissions;

                _login_entries[login_idx].permission = perm;
                _login_entries[login_idx].acl_permissions = acl;

                // Start keep-alive connection
                bool keep_alive_ok = true;
                if (keep_alive_secs > 0) {
                    keep_alive_ok = BaseChatMesh::startConnection(contact, keep_alive_secs);
                }
                _login_entries[login_idx].keep_alive_active =
                    keep_alive_secs > 0 && keep_alive_ok;
                _login_entries[login_idx].status = keep_alive_ok ? LOGIN_OK : LOGIN_DROPPED;

                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact.id.pub_key, keep_alive_ok, perm, tag, acl,
                    response.firmware_level);

#if SIGURDOS_DEBUG_MESH
                Serial.printf("[mesh] Login OK for %s (perm=%d, acl=%d, ka=%us)\n",
                              contact.name, perm, acl, keep_alive_secs);
#endif
                return; // handled — don't push to ring buffer
            }
            if (response.format == login_response::Format::LegacySuccess) {
                _login_entries[login_idx].status = LOGIN_OK;
                _login_entries[login_idx].permission = 1; // legacy: admin if "OK"
                _login_entries[login_idx].acl_permissions = 0;
                _login_entries[login_idx].keep_alive_active = false;
                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact.id.pub_key, true, 0, 0, 0, 0,
                    /*legacy_success=*/true);
#if SIGURDOS_DEBUG_MESH
                Serial.printf("[mesh] Login OK (legacy) for %s\n", contact.name);
#endif
                return;
            }
            if (response.format == login_response::Format::CurrentFailure) {
                _login_entries[login_idx].status = LOGIN_FAILED;
                _login_entries[login_idx].keep_alive_active = false;
                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact.id.pub_key, false, 0, 0, 0, 0);
#if SIGURDOS_DEBUG_MESH
                Serial.printf("[mesh] Login FAILED for %s (reason=%d)\n",
                              contact.name, response.failure_code);
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
                const bool companion_binary = _pending_reqs[i].companion_binary;
                if (req_type == REQ_TYPE_GET_ROOM_MSGS && len > 4) {
                    char chan[32];
                    strncpy(chan, _pending_reqs[i].channel_name, sizeof(chan) - 1);
                    chan[sizeof(chan) - 1] = '\0';
                    _pending_reqs[i].in_use = false;
                    _pending_reqs[i].companion_binary = false;
                    parseRoomMsgResponse(contact, data + 4, len - 4, chan);
                } else {
                    _pending_reqs[i].in_use = false;
                    _pending_reqs[i].companion_binary = false;
                    if (len > 4) {
                        if (companion_binary) {
                            sigurdos::mesh::mesh_v2_companion_binary_push(
                                tag, data + 4, len - 4);
                        } else if (req_type == REQ_TYPE_GET_STATUS) {
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
        // Path updates mutate the contact table with no UI event site;
        // schedule a debounced save so the route survives a power cut.
        sigurdos::mesh::markContactsDirty();
    }

    bool SigurdMeshV2::onContactPathRecv(::ContactInfo& contact,
                                         uint8_t* in_path, uint8_t in_path_len,
                                         uint8_t* out_path, uint8_t out_path_len,
                                         uint8_t extra_type, uint8_t* extra,
                                         uint8_t extra_len) {
        if (extra_type == PAYLOAD_TYPE_RESPONSE && extra && extra_len > 4) {
            uint32_t tag = 0;
            memcpy(&tag, extra, sizeof(tag));
            for (int i = 0; i < MAX_DISCOVERY_PENDING; ++i) {
                DiscoveryPending& pending = _discovery_pending[i];
                if (!pending.in_use || pending.tag != tag) continue;

                pending.completed = true;
                if (::mesh::Packet::isValidPathLen(in_path_len) &&
                    ::mesh::Packet::isValidPathLen(out_path_len)) {
                    sigurdos::mesh::mesh_v2_companion_path_discovery_push(
                        contact.id.pub_key, in_path, in_path_len,
                        out_path, out_path_len);
                }
                return false;  // Discovery responses must not create a reciprocal path.
            }
        }

        return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len,
                                               out_path, out_path_len,
                                               extra_type, extra, extra_len);
    }

    uint32_t SigurdMeshV2::sendPathDiscovery(const char* name,
                                              uint32_t* est_timeout_out) {
        if (!name || !name[0]) return 0;
        if (est_timeout_out) *est_timeout_out = 0;

        int pending_slot = -1;
        for (int i = 0; i < MAX_DISCOVERY_PENDING; ++i) {
            if (!_discovery_pending[i].in_use || _discovery_pending[i].completed) {
                pending_slot = i;
                break;
            }
        }
        if (pending_slot < 0) return 0;

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
                uint8_t saved_path[32] = {0};
                if (saved_len <= 32 && saved_len != OUT_PATH_UNKNOWN)
                    memcpy(saved_path, c->out_path, saved_len);
                c->out_path_len = OUT_PATH_UNKNOWN;
                // Path discovery is a flooded telemetry request for BASE data.
                // The random suffix keeps otherwise identical packet hashes unique.
                uint8_t random_bytes[4] = {};
                getRNG()->random(random_bytes, sizeof(random_bytes));
                uint8_t req_data[PATH_DISCOVERY_REQUEST_LEN] = {};
                buildPathDiscoveryRequest(req_data, random_bytes);
                int r = BaseChatMesh::sendRequest(*c, req_data, sizeof(req_data), tag, est_timeout);
                // Restore original path ONLY if it wasn't legitimately updated during send
                if (c->out_path_len == OUT_PATH_UNKNOWN) {
                    c->out_path_len = saved_len;
                    if (saved_len != OUT_PATH_UNKNOWN && saved_len <= 32)
                        memcpy(c->out_path, saved_path, saved_len);
                }
                if (r != MSG_SEND_FAILED) {
                    DiscoveryPending& pending = _discovery_pending[pending_slot];
                    pending.tag = tag;
                    strncpy(pending.dest_name, name, sizeof(pending.dest_name) - 1);
                    pending.dest_name[sizeof(pending.dest_name) - 1] = '\0';
                    pending.in_use = true;
                    pending.completed = false;
                    pending.started_at_ms = _ms->getMillis();
                    if (est_timeout_out) *est_timeout_out = est_timeout;
                    return tag;
                }
                return 0;
            }
        }
        return 0;
    }

#if defined(ESP32_PLATFORM)
    int SigurdMeshV2::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
        char path[48];
        if (!dest_buf || !makeAdvertBlobPath(key, key_len, path, sizeof(path))) return 0;
        sigurdos::storage::atomicFileRecover(
            path, sigurdos::mesh::validateAdvertBlob, nullptr);
        if (!SPIFFS.exists(path)) {
            if (!makeAdvertBlobPath(key, key_len, path, sizeof(path), true) ||
                !SPIFFS.exists(path)) {
                return 0;
            }
        }
        File f = SPIFFS.open(path, "r");
        if (!f) return 0;
        const size_t file_len = f.size();
        if (!advertBlobLengthValid(file_len)) {
            f.close();
            return 0;
        }
        const size_t read = f.read(dest_buf, file_len);
        f.close();
        return read == file_len ? (int)read : 0;
    }
#endif  // ESP32_PLATFORM

#if defined(ESP32_PLATFORM)
    bool SigurdMeshV2::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) {
        char path[48];
        if (!src_buf || len < 0 || !advertBlobLengthValid((size_t)len) ||
            !makeAdvertBlobPath(key, key_len, path, sizeof(path))) {
            return false;
        }
        if (!sigurdos::storage::atomicFileRecover(
                path, sigurdos::mesh::validateAdvertBlob, nullptr)) return false;
        AdvertBlobWriteContext context{src_buf, static_cast<size_t>(len)};
        if (!sigurdos::storage::atomicFileReplace(
                path, sigurdos::mesh::writeAdvertBlob, &context,
                sigurdos::mesh::validateAdvertBlob, &context)) return false;

        char legacy_path[48];
        if (makeAdvertBlobPath(key, key_len, legacy_path, sizeof(legacy_path), true) &&
            strcmp(path, legacy_path) != 0 && SPIFFS.exists(legacy_path)) {
            SPIFFS.remove(legacy_path);
        }
        return true;
    }
#endif  // ESP32_PLATFORM

    int SigurdMeshV2::exportContactBounded(const ::ContactInfo& contact,
                                            uint8_t* out, size_t out_cap) {
        if (!out || out_cap < SIGURDOS_ADVERT_BLOB_MAX_LEN) return 0;
        const int len = (int)BaseChatMesh::exportContact(contact, out);
        return len > 0 && advertBlobFitsOutput((size_t)len, out_cap) ? len : 0;
    }

    bool SigurdMeshV2::allowPacketForward(const ::mesh::Packet* packet) {
        (void)packet;
        return sigurdos::mesh::clientRepeatAllowsForward(
            sigurdos::prefs_get().client_repeat);
    }

    bool SigurdMeshV2::shouldAutoAddContactType(uint8_t type) const {
        const sigurdos::NodePrefs& prefs = sigurdos::prefs_get();
        return auto_add_policy::typeAllowed(
            prefs.manual_add_contacts, prefs.autoadd_config, type);
    }

    int SigurdMeshV2::addLoginEntry(const ::ContactInfo& contact,
                                    uint32_t estimated_timeout_ms) {
        int idx = findLoginEntry(contact.id.pub_key);
        if (idx < 0) {
            idx = login_session::selectReservationSlot(
                _login_entries, MAX_LOGIN_ENTRIES);
        }
        if (idx < 0) return -1;

        LoginEntry& entry = _login_entries[idx];
        entry = LoginEntry{};
        strncpy(entry.contact_name, contact.name, sizeof(entry.contact_name) - 1);
        entry.contact_name[sizeof(entry.contact_name) - 1] = '\0';
        memcpy(entry.pub_key, contact.id.pub_key, PUB_KEY_SIZE);
        entry.status = LOGIN_PENDING;
        entry.started_at_ms = _ms->getMillis();
        entry.timeout_ms = login_session::normalizeTimeout(estimated_timeout_ms);
        entry.in_use = true;
        return idx;
    }

    void SigurdMeshV2::invalidateAllLoginSessions() {
        for (int i = 0; i < MAX_LOGIN_ENTRIES; i++) {
            const LoginEntry& entry = _login_entries[i];
            if (!entry.in_use) continue;
            BaseChatMesh::stopConnection(entry.pub_key);
        }
        clearAllLoginEntries();
    }

    void SigurdMeshV2::updateLoginSessions(uint32_t now_ms) {
        for (int i = 0; i < MAX_LOGIN_ENTRIES; i++) {
            LoginEntry& entry = _login_entries[i];
            if (!entry.in_use) continue;

            const ::ContactInfo* contact = nullptr;
            if (entry.status == LOGIN_OK && entry.keep_alive_active) {
                contact = lookupContactByPubKey(entry.pub_key, PUB_KEY_SIZE);
            }
            const bool connected = contact && BaseChatMesh::hasConnectionTo(contact->id.pub_key);
            const login_session::Status next = login_session::evaluate(
                static_cast<login_session::Status>(entry.status),
                entry.started_at_ms,
                entry.timeout_ms,
                entry.keep_alive_active,
                connected,
                now_ms);
            if ((uint8_t)next == entry.status) continue;

            entry.status = (uint8_t)next;
            entry.keep_alive_active = false;
            if (!contact) {
                contact = lookupContactByPubKey(entry.pub_key, PUB_KEY_SIZE);
            }
            if (contact) {
                sigurdos::mesh::mesh_v2_companion_login_push(
                    contact->id.pub_key, false, 0, 0, 0, 0);
            }
        }
    }

    void SigurdMeshV2::loop() {
        // Apply a deadline before dispatch so a late response cannot resurrect
        // a login whose advertised response window has already elapsed.
        updateLoginSessions(_ms->getMillis());
        BaseChatMesh::loop();
        BaseChatMesh::checkConnections();
        updateLoginSessions(_ms->getMillis());
    }

    bool SigurdMeshV2::sendLoginTo(const ::ContactInfo& contact, const char* password) {
        if (!password) return false;
        const int previous_idx = findLoginEntry(contact.id.pub_key);
        const LoginEntry previous =
            previous_idx >= 0 ? _login_entries[previous_idx] : LoginEntry{};
        const int login_idx = addLoginEntry(contact);
        if (login_idx < 0) return false;

        uint32_t est_timeout = 0;
        int r = BaseChatMesh::sendLogin(contact, password, est_timeout);
        if (r != MSG_SEND_FAILED) {
            _login_entries[login_idx].started_at_ms = _ms->getMillis();
            _login_entries[login_idx].timeout_ms =
                login_session::normalizeTimeout(est_timeout);
#if SIGURDOS_DEBUG_MESH
            Serial.printf("[mesh] Login sent to %s (result=%d, timeout=%u)\n",
                          contact.name, r, est_timeout);
#endif
            return true;
        } else {
            _login_entries[login_idx] =
                previous_idx >= 0 ? previous : LoginEntry{};
            return false;
        }
    }

    void SigurdMeshV2::sendLogoutTo(const ::ContactInfo& contact) {
        BaseChatMesh::stopConnection(contact.id.pub_key);
        removeLoginEntry(contact.id.pub_key);
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
        return removeContactByPubKey(tmp.id.pub_key);
    }

    bool SigurdMeshV2::removeContactByPubKey(const uint8_t* pub_key) {
        if (!pub_key) return false;
        ::ContactInfo* contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
        if (!contact) return false;

        uint8_t key[PUB_KEY_SIZE] = {};
        memcpy(key, pub_key, sizeof(key));
        if (!BaseChatMesh::removeContact(*contact)) return false;
        deleteBlobByKey(SPIFFS, key, sizeof(key));
        return true;
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
        ChannelDetails empty{};
        return setChannelSlot(idx, empty);
    }

    bool SigurdMeshV2::setChannelSlot(int idx, const ChannelDetails& details) {
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;
        return set_contiguous_channel_slot<MAX_GROUP_CHANNELS>(
            static_cast<size_t>(idx), details,
            [this](size_t slot, ChannelDetails& out) {
                return BaseChatMesh::getChannel(static_cast<int>(slot), out);
            },
            [this](size_t slot, const ChannelDetails& value) {
                return BaseChatMesh::setChannel(static_cast<int>(slot), value);
            },
            [](const ChannelDetails& value) { return value.name[0] == '\0'; });
    }

    int SigurdMeshV2::decode_b64(const char* in, size_t in_len, uint8_t* out, size_t out_cap) {
        size_t decoded = 0;
        return decodeBase64Strict(in, in_len, out, out_cap, decoded)
            ? static_cast<int>(decoded) : 0;
    }

    bool SigurdMeshV2::addChannelBool(const char* name, const char* psk_base64) {
        if (!name || !name[0] || !psk_base64 || !channel_name_valid(name)) return false;
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
        char normalized[32];
        if (!hashtag_channel_name_normalise(name, normalized,
                                            sizeof(normalized))) return false;

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
        memcpy(cd.name, normalized, strlen(normalized) + 1);
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
        char safe_text[MAX_TEXT_LEN];
        sigurdos::utf8_copy_truncate(safe_text, sizeof(safe_text), text);
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t expected_ack = 0, est_timeout = 0;
                uint32_t ts = getRTCClock()->getCurrentTimeUnique();
                int r = BaseChatMesh::sendMessage(tmp, ts, 0, safe_text,
                                                  expected_ack, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    addPendingAck(name, ts, expected_ack, est_timeout);
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool SigurdMeshV2::sendTextTo(const char* name, const char* text, uint32_t fixed_ts) {
        if (!name || !text) return false;
        char safe_text[MAX_TEXT_LEN];
        sigurdos::utf8_copy_truncate(safe_text, sizeof(safe_text), text);
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                uint32_t expected_ack = 0, est_timeout = 0;
                int r = BaseChatMesh::sendMessage(tmp, fixed_ts, 0, safe_text,
                                                  expected_ack, est_timeout);
                if (r != MSG_SEND_FAILED) {
                    addPendingAck(name, fixed_ts, expected_ack, est_timeout);
                }
                return r != MSG_SEND_FAILED;
            }
        }
        return false;
    }

    bool SigurdMeshV2::sendGroupText(int idx, const char* text) {
        uint32_t ts = getRTCClock()->getCurrentTimeUnique();
        if (ts == 0) ts = 1;
        return sendGroupText(idx, text, ts);
    }

    bool SigurdMeshV2::sendGroupText(int idx, const char* text, uint32_t fixed_ts) {
        if (idx < 0 || idx >= getChannelCount() || !text || !text[0]) return false;
        ChannelDetails cd;
        if (!BaseChatMesh::getChannel(idx, cd)) return false;
        const size_t prefix_len = strnlen(_own_name, MAX_TEXT_LEN) + 2;
        if (prefix_len >= MAX_TEXT_LEN) return false;
        char safe_text[MAX_TEXT_LEN + 1];
        const size_t text_len = sigurdos::utf8_copy_truncate(
            safe_text, MAX_TEXT_LEN - prefix_len + 1, text);
        return BaseChatMesh::sendGroupMessage(fixed_ts, cd.channel, _own_name, safe_text,
                                              (int)text_len);
    }

    bool SigurdMeshV2::sendGroupDataToChannel(int idx, uint16_t data_type, const uint8_t* data, int data_len) {
        return sendGroupDataToChannel(idx, nullptr, OUT_PATH_UNKNOWN,
                                      data_type, data, data_len);
    }

    bool SigurdMeshV2::sendGroupDataToChannel(int idx, const uint8_t* path, uint8_t path_len, uint16_t data_type, const uint8_t* data, int data_len) {
        if (idx < 0 || idx >= getChannelCount()) return false;
        if (data_len > 0 && !data) return false;
        if (path_len != OUT_PATH_UNKNOWN &&
            (!sigurdos::mesh::path::encodedLengthValid(path_len) ||
             !::mesh::Packet::isValidPathLen(path_len))) return false;
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
        size_t text_len = 0;
        if (!pub_key || !anonymousTextFits(text, &text_len)) return false;

        // Build a temporary ContactInfo with the given pubkey
        ::ContactInfo tmp{};
        memcpy(tmp.id.pub_key, pub_key, PUB_KEY_SIZE);
        tmp.out_path_len = OUT_PATH_UNKNOWN;
        tmp.type = ADV_TYPE_CHAT;

        uint32_t tag = 0, est_timeout = 0;
        uint32_t ts = getRTCClock()->getCurrentTime();

        // Data format: [4-byte timestamp][null-terminated text]
        uint8_t buf[MAX_ANON_REQUEST_BYTES];
        memcpy(buf, &ts, ANON_TIMESTAMP_BYTES);
        memcpy(buf + ANON_TIMESTAMP_BYTES, text, text_len + 1);

        const uint8_t request_len = static_cast<uint8_t>(
            ANON_TIMESTAMP_BYTES + text_len + ANON_TEXT_TERMINATOR_BYTES);
        int r = BaseChatMesh::sendAnonReq(
            tmp, buf, request_len, tag, est_timeout);
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

    bool SigurdMeshV2::broadcastAdvert(const char* name, uint8_t adv_type,
                                       bool apply_default_scope) {
        AdvertDataBuilder builder(adv_type, name);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        return sendAdvertImpl(pkt, apply_default_scope);
    }

    bool SigurdMeshV2::broadcastAdvert(const char* name, double lat, double lon,
                                       uint8_t adv_type, bool apply_default_scope) {
        AdvertDataBuilder builder(adv_type, name, lat, lon);
        uint8_t app[MAX_ADVERT_DATA_SIZE];
        uint8_t app_len = builder.encodeTo(app);
        ::mesh::Packet* pkt = createAdvert(self_id, app, app_len);
        return sendAdvertImpl(pkt, apply_default_scope);
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
                return path::hashCount(_advert_paths[i].encoded_path_len);
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
        _flood_scope.setDefault(key16);
    }

} // namespace mesh
} // namespace sigurdos
