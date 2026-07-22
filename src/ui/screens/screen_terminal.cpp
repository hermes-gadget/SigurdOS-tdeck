// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include "../screens.h"
#include "../screens_common.h"
#include "../navigation.h"
#include "../theme.h"
#include "../responsive.h"
#include "../terminal_line_cap.h"
#include "../terminal_var_policy.h"
#include "../../hal/prefs.h"
#include "../../hal/atomic_file.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static constexpr size_t TERMINAL_VAR_STORE_MAX = 4096;
static constexpr const char* TERMINAL_VAR_PATH = "/custom_vars.txt";

struct TerminalVarWriteCtx { const char* data; size_t length; };

static bool terminal_var_write(sigurdos::storage::AtomicFileWriter& writer, void* data)
{
    auto* ctx = static_cast<TerminalVarWriteCtx*>(data);
    return ctx && writer.write(ctx->data, ctx->length) == ctx->length;
}

static bool terminal_var_validate(sigurdos::storage::AtomicFileReader& reader, void* data)
{
    auto* ctx = static_cast<TerminalVarWriteCtx*>(data);
    if (!ctx || reader.size() != ctx->length) return false;
    char chunk[128];
    size_t offset = 0;
    while (offset < ctx->length) {
        size_t count = ctx->length - offset;
        if (count > sizeof(chunk)) count = sizeof(chunk);
        if (reader.read(chunk, count) != count ||
            std::memcmp(chunk, ctx->data + offset, count) != 0) return false;
        offset += count;
    }
    return true;
}

enum class TerminalVarUpdate { Ok, NotFound, TooLarge, NoMemory, IoError };

static TerminalVarUpdate terminal_var_update(const char* key, const char* value)
{
    size_t input_len = 0;
    File file = SPIFFS.open(TERMINAL_VAR_PATH, "r");
    if (file) {
        input_len = file.size();
        if (input_len > TERMINAL_VAR_STORE_MAX) { file.close(); return TerminalVarUpdate::TooLarge; }
    }
    auto* input = new(std::nothrow) char[input_len + 1];
    auto* output = new(std::nothrow) char[TERMINAL_VAR_STORE_MAX + 1];
    if (!input || !output) {
        delete[] input; delete[] output;
        if (file) file.close();
        return TerminalVarUpdate::NoMemory;
    }
    if (file && input_len > 0 && file.readBytes(input, input_len) != input_len) {
        file.close(); delete[] input; delete[] output;
        return TerminalVarUpdate::IoError;
    }
    if (file) file.close();
    input[input_len] = '\0';

    size_t output_len = 0;
    bool found = false;
    const bool built = terminal_var_rewrite(input, input_len, key, value, output,
                                             TERMINAL_VAR_STORE_MAX + 1,
                                             &output_len, &found);
    delete[] input;
    if (!built) { delete[] output; return TerminalVarUpdate::TooLarge; }
    if (!value && !found) { delete[] output; return TerminalVarUpdate::NotFound; }

    TerminalVarWriteCtx ctx{output, output_len};
    const bool saved = sigurdos::storage::atomicFileReplace(
        TERMINAL_VAR_PATH, terminal_var_write, &ctx, terminal_var_validate, &ctx);
    delete[] output;
    return saved ? TerminalVarUpdate::Ok : TerminalVarUpdate::IoError;
}

// ════════════════════════════════════════════════════════
// Terminal — colored log output helpers
// ════════════════════════════════════════════════════════
static uint32_t term_classify_line(const char* text)
{
    if (strstr(text, "ERROR") || strstr(text, "FAIL") || strstr(text, "failed"))
        return ACCENT_RED;
    if (strstr(text, "WARN") || strstr(text, "WARNING"))
        return ACCENT_ORANGE;
    if (strstr(text, "OK") || strstr(text, "ok") ||
        strstr(text, "sent") || strstr(text, "Pong"))
        return ACCENT_GREEN;
    if (strstr(text, "RSSI") || strstr(text, "SNR") ||
        strstr(text, "Noise") || strstr(text, "dBm") || strstr(text, "MHz"))
        return ACCENT;
    if (text[0] == '>')
        return TEXT_PRIMARY;
    return 0x00ff00u;
}

// ── Terminal line cap — prevent unbounded label accumulation ──
static constexpr uint32_t MAX_TERM_COMMAND_LENGTH = 255;
static constexpr size_t MAX_TERM_VAR_KEY_LENGTH = 31;
static constexpr size_t MAX_TERM_VAR_VALUE_LENGTH = 127;

static bool terminal_var_key_valid(const char* key, size_t len)
{
    if (!key || len == 0 || len > MAX_TERM_VAR_KEY_LENGTH) return false;
    for (size_t i = 0; i < len; ++i) {
        const char c = key[i];
        const bool valid = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') ||
                           c == '_' || c == '-' || c == '.';
        if (!valid) return false;
    }
    return true;
}

static void term_add_line(lv_obj_t* log, const char* text)
{
    const bool ready = terminal_prune_oldest_for_append(
        log,
        [](lv_obj_t* obj) { return lv_obj_get_child_cnt(obj); },
        [](lv_obj_t* obj) { return lv_obj_get_child(obj, 0); },
        [](lv_obj_t* obj) { lv_obj_delete(obj); });
    if (!ready) return;

    lv_obj_t* lbl = lv_label_create(log);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(term_classify_line(text)), 0);
    lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_scroll_to_view(lbl, LV_ANIM_OFF);
}

// ════════════════════════════════════════════════════════
// Terminal — colored log output + command input
// ════════════════════════════════════════════════════════
void terminal_screen_show()
{
    // PIN gate check
    if (sigurdos::prefs_get().device_pin != 0 && !pin_grace_active()) {
        pin_entry_show(Screen::Terminal);
        return;
    }
    lv_obj_t* scr = make_screen_full("Terminal");

    static constexpr int TERM_INPUT_H = 28;
    static constexpr int TERM_LOG_H   = CONTENT_H - TERM_INPUT_H - DIVIDER_H;  // 167

    // Log output container (pure black, flex-column, scrollable)
    lv_obj_t* log = lv_obj_create(scr);
    lv_obj_set_size(log, LV_PCT(100), TERM_LOG_H);
    lv_obj_align(log, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(log, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(log, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(log, 0, 0);
    lv_obj_set_style_pad_all(log, 4, 0);
    lv_obj_set_flex_flow(log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(log, LV_SCROLLBAR_MODE_OFF);

    // Boot header lines
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    term_add_line(log, "SigurdOS T-Deck Terminal");
    term_add_line(log, "MeshCore protocol active");
    if (p.configured) {
        char radio_buf[64];
        snprintf(radio_buf, sizeof(radio_buf), "Radio: SX1262 %.3f MHz configured", p.freq);
        term_add_line(log, radio_buf);
    } else {
        term_add_line(log, "Radio: ERROR - not configured");
    }

    // Divider between log and input
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, CONTENT_Y + TERM_LOG_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Command input
    lv_obj_t* input = lv_textarea_create(scr);
    lv_obj_set_size(input, LV_PCT(100), TERM_INPUT_H);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, CONTENT_Y + TERM_LOG_H + DIVIDER_H);
    lv_obj_set_style_bg_color(input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_obj_set_style_pad_all(input, 4, 0);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, MAX_TERM_COMMAND_LENGTH);
    lv_textarea_set_placeholder_text(input, "> enter command...");
    apply_focus_style(input);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, input);
        lv_group_focus_obj(input);
    }

    lv_obj_add_event_cb(input, [](lv_event_t* e) {
        lv_obj_t* ta        = (lv_obj_t*)lv_event_get_target(e);
        const char* cmd     = lv_textarea_get_text(ta);
        if (!cmd || !cmd[0]) return;

        lv_obj_t* log_cont = (lv_obj_t*)lv_event_get_user_data(e);

        // Echo the command
        static char echo[280];
        snprintf(echo, sizeof(echo), "> %s", cmd);
        term_add_line(log_cont, echo);

        static char result[256];
        if (strcmp(cmd, "help") == 0) {
            snprintf(result, sizeof(result), "Commands: help status advert ping sign anon fetchmsgs groupdata emoji-list exportkey importkey import getvar setvar delvar listvars");
        } else if (strncmp(cmd, "import ", 7) == 0) {
            const char* uri = cmd + 7;
            while (*uri == ' ' || *uri == '\t') uri++;
            if (!*uri) {
                snprintf(result, sizeof(result), "Usage: import meshcore://contact/add?name=...&public_key=...&type=...  OR  import meshcore://channel/add?name=...&secret=...");
            } else if (strstr(uri, "channel/add")) {
                if (sigurdos::mesh::addChannelByUri(uri)) {
                    snprintf(result, sizeof(result), "Channel added via URI");
                } else {
                    snprintf(result, sizeof(result), "Failed to add channel (invalid URI or already exists?)");
                }
            } else {
                if (sigurdos::mesh::importContactByUri(uri)) {
                    snprintf(result, sizeof(result), "Contact imported via URI");
                } else {
                    snprintf(result, sizeof(result), "Failed to import (invalid URI or already exists?)");
                }
            }
        } else if (strncmp(cmd, "getvar ", 7) == 0) {
            const char* key = cmd + 7;
            if (!key[0]) {
                snprintf(result, sizeof(result), "Usage: getvar <key>");
            } else {
                File f = SPIFFS.open("/custom_vars.txt", "r");
                bool found = false;
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.startsWith(key) && line.charAt(strlen(key)) == '=') {
                            const char* val = line.c_str() + strlen(key) + 1;
                            snprintf(result, sizeof(result), "%s = %s", key, val);
                            found = true;
                            break;
                        }
                    }
                    f.close();
                }
                if (!found) {
                    snprintf(result, sizeof(result), "Key '%s' not found", key);
                }
            }
        } else if (strncmp(cmd, "setvar ", 7) == 0) {
            const char* arg = cmd + 7;
            const char* sep = strchr(arg, ' ');
            if (!sep || sep == arg) {
                snprintf(result, sizeof(result), "Usage: setvar <key> <value>");
            } else {
                const size_t klen = (size_t)(sep - arg);
                const char* value = sep + 1;
                const size_t value_len = strlen(value);
                if (!terminal_var_key_valid(arg, klen)) {
                    snprintf(result, sizeof(result),
                        "Invalid key (max %zu; use letters, digits, _, -, .)",
                        MAX_TERM_VAR_KEY_LENGTH);
                } else if (value_len == 0 || value_len > MAX_TERM_VAR_VALUE_LENGTH) {
                    snprintf(result, sizeof(result), "Value must be 1-%zu characters",
                        MAX_TERM_VAR_VALUE_LENGTH);
                } else {
                    char key[MAX_TERM_VAR_KEY_LENGTH + 1];
                    memcpy(key, arg, klen);
                    key[klen] = '\0';
                    const TerminalVarUpdate status = terminal_var_update(key, value);
                    if (status == TerminalVarUpdate::Ok) {
                        snprintf(result, sizeof(result), "Set: %s = %s", key, value);
                    } else if (status == TerminalVarUpdate::TooLarge) {
                        snprintf(result, sizeof(result), "Variable store full (max %zu bytes)",
                                 TERMINAL_VAR_STORE_MAX);
                    } else if (status == TerminalVarUpdate::NoMemory) {
                        snprintf(result, sizeof(result), "Set failed: not enough memory");
                    } else {
                        snprintf(result, sizeof(result), "Set failed: storage write error");
                    }
                }
            }
        } else if (strncmp(cmd, "delvar ", 7) == 0) {
            const char* key = cmd + 7;
            if (!key[0]) {
                snprintf(result, sizeof(result), "Usage: delvar <key>");
            } else if (!terminal_var_key_valid(key, strlen(key))) {
                snprintf(result, sizeof(result),
                    "Invalid key (max %zu; use letters, digits, _, -, .)",
                    MAX_TERM_VAR_KEY_LENGTH);
            } else {
                const TerminalVarUpdate status = terminal_var_update(key, nullptr);
                if (status == TerminalVarUpdate::Ok) {
                    snprintf(result, sizeof(result), "Deleted: %s", key);
                } else if (status == TerminalVarUpdate::NotFound) {
                    snprintf(result, sizeof(result), "Key '%s' not found", key);
                } else if (status == TerminalVarUpdate::TooLarge) {
                    snprintf(result, sizeof(result), "Delete refused: variable store exceeds %zu bytes",
                             TERMINAL_VAR_STORE_MAX);
                } else if (status == TerminalVarUpdate::NoMemory) {
                    snprintf(result, sizeof(result), "Delete failed: not enough memory");
                } else {
                    snprintf(result, sizeof(result), "Delete failed: storage write error");
                }
            }
        } else if (strcmp(cmd, "listvars") == 0) {
            File f = SPIFFS.open("/custom_vars.txt", "r");
            if (!f) {
                snprintf(result, sizeof(result), "No custom variables set");
            } else {
                int count = 0;
                char lb[128];
                while (f.available()) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0 && line.indexOf('=') > 0) {
                        snprintf(lb, sizeof(lb), "  %s", line.c_str());
                        term_add_line(log_cont, lb);
                        count++;
                    }
                }
                f.close();
                snprintf(result, sizeof(result), "%d variable%s", count, count == 1 ? "" : "s");
            }
        } else if (strcmp(cmd, "status") == 0) {
            int rssi  = sigurdos::mesh::getLastRSSI();
            float snr = sigurdos::mesh::getLastSNR();
            int noise = sigurdos::mesh::getNoiseFloor();
            snprintf(result, sizeof(result),
                "RSSI:%ddBm SNR:%.1fdB Noise:%ddBm  Contacts:%d Channels:%d",
                rssi, snr, noise,
                sigurdos::mesh::getContactCount(),
                sigurdos::mesh::getChannelCount());
        } else if (strcmp(cmd, "advert") == 0) {
            bool ok = sigurdos::mesh::sendAdvert();
            snprintf(result, sizeof(result), ok ? "Advert sent" : "Send failed");
        } else if (strcmp(cmd, "ping") == 0) {
            snprintf(result, sizeof(result), "Pong! Uptime: %lums", millis());
        } else if (strncmp(cmd, "sign ", 5) == 0) {
            const char* sign_data = cmd + 5;
            if (!sign_data[0]) {
                snprintf(result, sizeof(result), "Usage: sign <data>");
            } else {
                uint8_t sig[64];
                int sig_len = sigurdos::mesh::signMessage(sign_data, sig);
                if (sig_len > 0) {
                    // Hex-encode the signature
                    int pos = 0;
                    for (int i = 0; i < sig_len && pos < (int)sizeof(result) - 3; i++) {
                        pos += snprintf(result + pos, sizeof(result) - pos, "%02x", sig[i]);
                    }
                } else {
                    snprintf(result, sizeof(result), "Sign failed (no identity?)");
                }
            }
        } else if (strncmp(cmd, "anon ", 5) == 0) {
            const char* anon_arg = cmd + 5;
            const char* anon_space = strchr(anon_arg, ' ');
            if (!anon_space || anon_space == anon_arg) {
                snprintf(result, sizeof(result), "Usage: anon <64hex_pubkey> <text>");
            } else {
                char pubkey_hex[65];
                size_t pklen = (size_t)(anon_space - anon_arg);
                if (pklen > 64) pklen = 64;
                memcpy(pubkey_hex, anon_arg, pklen);
                pubkey_hex[pklen] = '\0';
                const char* msg = anon_space + 1;
                if (sigurdos::mesh::sendAnonMessage(pubkey_hex, msg)) {
                    snprintf(result, sizeof(result), "Anon msg sent to %s", pubkey_hex);
                } else {
                    snprintf(result, sizeof(result), "Send failed (bad hex? %zu chars)", pklen);
                }
            }
        } else if (strncmp(cmd, "fetchmsgs ", 10) == 0) {
            const char* fetch_arg = cmd + 10;
            const char* fetch_space = strchr(fetch_arg, ' ');
            if (!fetch_space || fetch_space == fetch_arg) {
                snprintf(result, sizeof(result), "Usage: fetchmsgs <contact> <channel>");
            } else {
                char contact_name[64];
                size_t cn_len = (size_t)(fetch_space - fetch_arg);
                if (cn_len > 63) cn_len = 63;
                memcpy(contact_name, fetch_arg, cn_len);
                contact_name[cn_len] = '\0';
                const char* channel = fetch_space + 1;
                if (sigurdos::mesh::sendRoomMsgFetchRequest(contact_name, channel)) {
                    snprintf(result, sizeof(result), "Room fetch sent to %s for %s", contact_name, channel);
                } else {
                    snprintf(result, sizeof(result), "Fetch failed (contact '%s' not found?)", contact_name);
                }
            }
        } else if (strncmp(cmd, "groupdata ", 10) == 0) {
            // Format: groupdata <channel_idx> <type_hex> <hex_payload>
            const char* gd_arg = cmd + 10;
            int ch_idx = atoi(gd_arg);
            const char* after_ch = gd_arg;
            while (*after_ch && *after_ch != ' ') after_ch++;
            if (!*after_ch) {
                snprintf(result, sizeof(result), "Usage: groupdata <channel_idx> <type_hex> <hex_payload>");
            } else {
                after_ch++; // skip space
                unsigned int dt = 0;
                if (sscanf(after_ch, "%x", &dt) != 1 || dt > 0xFFFF) {
                    snprintf(result, sizeof(result), "Bad type hex");
                } else {
                    const char* hex_start = after_ch;
                    while (*hex_start && *hex_start != ' ') hex_start++;
                    if (!*hex_start) {
                        snprintf(result, sizeof(result), "Usage: groupdata <channel_idx> <type_hex> <hex_payload>");
                    } else {
                        hex_start++; // skip space
                        uint8_t payload[64];
                        int plen = sigurdos::mesh::hexToBytes(hex_start, payload, sizeof(payload));
                        if (plen <= 0) {
                            snprintf(result, sizeof(result), "Bad hex payload");
                        } else {
                            bool ok = sigurdos::mesh::sendGroupDataToChannel(
                                ch_idx, (uint16_t)dt, payload, plen);
                            snprintf(result, sizeof(result), ok
                                ? "Group data sent to ch%d type=0x%04x (%d bytes)"
                                : "Send failed (bad channel? %d)", ch_idx, dt, plen);
                        }
                    }
                }
            }
        } else if (strcmp(cmd, "emoji-list") == 0) {
            term_add_line(log_cont, "--- Emoji list (362 available) ---");
            char line_buf[128];
            for (int i = 0; i < emoji_font_get_count(); i += 8) {
                int pos = 0;
                for (int j = 0; j < 8 && i + j < emoji_font_get_count(); j++) {
                    if (const char* e = emoji_font_get_by_index(i + j)) {
                        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%s ", e);
                    }
                }
                if (pos > 0) term_add_line(log_cont, line_buf);
            }
            term_add_line(log_cont, "--- End emoji list ---");
            lv_textarea_set_text(ta, "");
            return;
        } else if (strcmp(cmd, "exportkey") == 0) {
            char hex[PRV_KEY_SIZE * 2 + 1] = {0};
            if (sigurdos::mesh::exportIdentity(hex, sizeof(hex))) {
                term_add_line(log_cont, "Private key (keep secret!):");
                snprintf(result, sizeof(result), "%s", hex);
            } else {
                snprintf(result, sizeof(result), "Export failed (mesh not ready?)");
            }
        } else if (strncmp(cmd, "importkey ", 10) == 0) {
            const char* hex_in = cmd + 10;
            while (*hex_in == ' ' || *hex_in == '\t') hex_in++;
            size_t hlen = strlen(hex_in);
            if (hlen != PRV_KEY_SIZE * 2) {
                snprintf(result, sizeof(result),
                    "Bad key length: got %zu hex, need %d",
                    hlen, PRV_KEY_SIZE * 2);
            } else if (sigurdos::mesh::importIdentity(hex_in)) {
                term_add_line(log_cont, "Identity imported. Reboot for contacts to re-pair.");
                lv_textarea_set_text(ta, "");
                return;
            } else {
                snprintf(result, sizeof(result), "Import failed (invalid key?)");
            }
        } else {
            snprintf(result, sizeof(result), "Unknown: %s  (type 'help')", cmd);
        }

        term_add_line(log_cont, result);
        lv_textarea_set_text(ta, "");
    }, LV_EVENT_READY, log);

    show_screen(scr);
}

} // namespace sigurdos::ui
