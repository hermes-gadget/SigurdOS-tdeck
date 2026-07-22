// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Comprehensive debug module for SigurdOS.
// Build with -D SIGURDOS_DEBUG=1 (env:SigurdOS_TDeck_debug in platformio.ini).
//
// Per-feature flags can be set independently — see debug_cfg.h.
//
// Outputs to Serial at 115200 baud:
//   - Periodic full system dumps every 5s  (level >= 2, DIAG enabled)
//   - LVGL flush area on every display update (DISPLAY enabled)
//   - LVGL invalidation areas (DISPLAY enabled)
//   - Trackball GPIO raw state (level >= 2, DIAG enabled)
//   - Home screen tile layout diagnostics (DIAG enabled, level >= 3)
//   - Memory, task, mesh radio detail (DIAG enabled, level >= 3)

#include "debug.h"
#include "debug_cfg.h"
#include "reset_policy.h"
#include "debug_text_policy.h"

#if defined(SIGURDOS_DEBUG) && SIGURDOS_DEBUG

#include <Arduino.h>
#include <lvgl.h>
#include <esp_core_dump.h>
#include <cstddef>
#include <cstring>

#include "../hal/tdeck_pins.h"
#include "../hal/battery.h"
#include "../hal/display.h"
#include "../hal/trackball.h"
#include "../hal/touch.h"
#include "../hal/keyboard.h"
#include "../ui/responsive.h"
#include "../ui/theme.h"
#include "../ui/navigation.h"
#include "../mesh/mesh_wrapper.h"

namespace sigurdos {
namespace debug {

using namespace responsive;
using namespace theme;

static constexpr uint32_t DUMP_INTERVAL_MS = 5000;
static uint32_t last_dump_ms = 0;
static uint8_t  current_level = clamp_level(SIGURDOS_DEBUG_LEVEL);

// ── Per-feature runtime state ─────────────────────────────
// Default to the compile-time flag so builds with individual
// flags start with that feature on, and full-SIGURDOS_DEBUG builds
// start with everything on.
static bool feat_display = SIGURDOS_DEBUG_DISPLAY ? true : false;
static bool feat_mesh    = SIGURDOS_DEBUG_MESH    ? true : false;
static bool feat_ui      = SIGURDOS_DEBUG_UI      ? true : false;
static bool feat_map     = SIGURDOS_DEBUG_MAP     ? true : false;
static bool feat_diag    = SIGURDOS_DEBUG_DIAG    ? true : false;

// ── Crash ring buffer ───────────────────────────────────
#if SIGURDOS_CRASH_RING
static constexpr uint32_t DEBUG_RING_MAGIC = 0x53445247U;  // "SDRG"
static constexpr uint16_t DEBUG_RING_VERSION = 1;

struct DebugRingRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t head;
    uint16_t count;
    uint16_t reserved;
    DebugRingEntry entries[DEBUG_RING_SIZE];
    uint32_t checksum;
};

// RTC_NOINIT_ATTR avoids startup zeroing so the previous boot's final log
// entries survive panic, watchdog, and software resets.
static RTC_NOINIT_ATTR DebugRingRecord s_ring;

static uint32_t ring_checksum(const DebugRingRecord& record)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < offsetof(DebugRingRecord, checksum); i++) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static bool ring_valid()
{
    return s_ring.magic == DEBUG_RING_MAGIC &&
           s_ring.version == DEBUG_RING_VERSION &&
           s_ring.head < DEBUG_RING_SIZE &&
           s_ring.count <= DEBUG_RING_SIZE &&
           s_ring.checksum == ring_checksum(s_ring);
}

static void ring_reset_record()
{
    memset(&s_ring, 0, sizeof(s_ring));
    s_ring.magic = DEBUG_RING_MAGIC;
    s_ring.version = DEBUG_RING_VERSION;
    s_ring.checksum = ring_checksum(s_ring);
}

static void ring_prepare()
{
    if (!ring_valid()) ring_reset_record();
}

void ring_log(const char* line)
{
    ring_prepare();
    DebugRingEntry* e = &s_ring.entries[s_ring.head];
    e->timestamp_ms = millis();
    strncpy(e->line, line ? line : "", sizeof(e->line) - 1);
    e->line[sizeof(e->line) - 1] = '\0';
    s_ring.head = (uint16_t)((s_ring.head + 1U) % DEBUG_RING_SIZE);
    if (s_ring.count < DEBUG_RING_SIZE) s_ring.count++;
    s_ring.checksum = ring_checksum(s_ring);
}

void ring_dump()
{
    if (!ring_valid()) return;
    const uint16_t start = (s_ring.count < DEBUG_RING_SIZE) ? 0 : s_ring.head;
    for (uint16_t i = 0; i < s_ring.count; i++) {
        const auto* e = &s_ring.entries[(start + i) % DEBUG_RING_SIZE];
        Serial.printf("[ring] t=%lu %s\n", (unsigned long)e->timestamp_ms, e->line);
    }
}

void ring_clear()
{
    ring_reset_record();
}

bool has_crash_record()
{
    return sigurdos::diagnostics::reset_retains_crash_evidence(
        static_cast<int>(esp_reset_reason()));
}
#else
void ring_log(const char*) {}
void ring_dump() {}
void ring_clear() {}
bool has_crash_record() { return false; }
#endif // SIGURDOS_CRASH_RING

void feat_set_display(bool on) { feat_display = on; }
bool feat_get_display() { return feat_display; }
void feat_set_mesh(bool on)    { feat_mesh = on; }
bool feat_get_mesh()    { return feat_mesh; }
void feat_set_ui(bool on)      { feat_ui = on; }
bool feat_get_ui()      { return feat_ui; }
void feat_set_map(bool on)     { feat_map = on; }
bool feat_get_map()     { return feat_map; }
void feat_set_diag(bool on)    { feat_diag = on; }
bool feat_get_diag()    { return feat_diag; }

void feat_set_all_mask(bool on)
{
    feat_display = on;
    feat_mesh    = on;
    feat_ui      = on;
    feat_map     = on;
    feat_diag    = on;
}

void feat_from_mask(uint8_t mask)
{
    feat_display = (mask & 0x01) != 0;
    feat_mesh    = (mask & 0x02) != 0;
    feat_ui      = (mask & 0x04) != 0;
    feat_map     = (mask & 0x08) != 0;
    feat_diag    = (mask & 0x10) != 0;
}

uint8_t feat_to_mask()
{
    return (uint8_t)(
        (feat_display ? 0x01 : 0) |
        (feat_mesh    ? 0x02 : 0) |
        (feat_ui      ? 0x04 : 0) |
        (feat_map     ? 0x08 : 0) |
        (feat_diag    ? 0x10 : 0));
}

void set_level(uint8_t level) {
    current_level = clamp_level(level);
    Serial.printf("[debug] level set to %u\n", (unsigned)current_level);
}

uint8_t get_level() {
    return current_level;
}

void init()
{
#if SIGURDOS_CRASH_RING
    // Check if previous boot ended in a crash
    esp_reset_reason_t reason = esp_reset_reason();
    if (sigurdos::diagnostics::reset_retains_crash_evidence(
            static_cast<int>(reason))) {
        Serial.println("\n⚠═══════════════════════════════════════");
        Serial.println("⚠ PREVIOUS BOOT ENDED IN A CRASH");
        Serial.printf("⚠ Reason: %d\n", (int)reason);
        Serial.println("⚠═══════════════════════════════════════");
        if (ring_valid() && s_ring.count > 0) {
            ring_dump();
            // Keep the retained record intact until all bytes have been handed
            // to the serial driver, then start a fresh ring for this boot.
            Serial.flush();
            ring_clear();
        } else {
            Serial.println("[ring] no valid retained entries");
            ring_clear();
        }
        if (esp_core_dump_image_check() == ESP_OK) {
            Serial.println("[crash] Core dump saved to flash. Use esptool.py to extract.");
        }
        Serial.println();
    } else {
        // Normal boot — clear old ring buffer
        ring_clear();
    }
#endif

    Serial.println();
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║   SigurdOS DEBUG build                  ║");
    Serial.println("║   (light periodic status; heavy dumps on demand) ║");
    Serial.printf(  "║   Debug level: %u                               ║\n", (unsigned)current_level);
    uint8_t mask = feat_to_mask();
    Serial.printf(  "║   Features: %s%s%s%s%s          ║\n",
        (mask & 0x01) ? "D" : "d",
        (mask & 0x02) ? "M" : "m",
        (mask & 0x04) ? "U" : "u",
        (mask & 0x08) ? "P" : "p",
        (mask & 0x10) ? "S" : "s");
    Serial.println("╚══════════════════════════════════════════════╝");
    Serial.println();
    Serial.println("  Feature keys: D=display M=mesh U=ui P=map S=diag");
    Serial.println();

    // Light one-line status only — full tree walks are too slow for setup/loop
    uint32_t now = millis();
    Serial.printf("[boot] debug enabled level=%u heap=%u psram=%u batt=%u%%\n",
                  (unsigned)current_level,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)sigurdos_battery_pct());
#if SIGURDOS_CRASH_RING
    {
        char ring_buf[64];
        snprintf(ring_buf, sizeof(ring_buf), "[boot] debug level=%u heap=%u psram=%u batt=%u%%",
                 (unsigned)current_level,
                 (unsigned)ESP.getFreeHeap(),
                 (unsigned)ESP.getFreePsram(),
                 (unsigned)sigurdos_battery_pct());
        ring_log(ring_buf);
    }
#endif
    last_dump_ms = now;
}

void loop()
{
    if (!feat_diag) return;
    // Level 1 (quiet): no periodic output
    if (current_level < 2) return;

    uint32_t now = millis();
    if (now - last_dump_ms >= DUMP_INTERVAL_MS) {
        last_dump_ms = now;

        // === LIGHT periodic status (safe, non-blocking) ===
        lv_mem_monitor_t lv_mem;
        lv_mem_monitor(&lv_mem);
        Serial.printf("[stat] t=%lu  heap=%u/%u  psram=%u  batt=%u%%  flush=%lu  feat=%02x  "
                      "lvgl=%u/%u largest=%u used=%u%% frag=%u%%\n",
                      (unsigned long)(now/1000),
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)ESP.getFreePsram(),
                      (unsigned)sigurdos_battery_pct(),
                      (unsigned long)0 /* flush count is local to display.cpp */,
                      (unsigned)feat_to_mask(),
                      (unsigned)lv_mem.free_size,
                      (unsigned)lv_mem.total_size,
                      (unsigned)lv_mem.free_biggest_size,
                      (unsigned)lv_mem.used_pct,
                      (unsigned)lv_mem.frag_pct);
#if SIGURDOS_CRASH_RING
        {
            char ring_buf[64];
            snprintf(ring_buf, sizeof(ring_buf), "[stat] t=%lu heap=%u/%u psram=%u batt=%u%%",
                     (unsigned long)(now/1000),
                     (unsigned)ESP.getFreeHeap(),
                     (unsigned)ESP.getMinFreeHeap(),
                     (unsigned)ESP.getFreePsram(),
                     (unsigned)sigurdos_battery_pct());
            ring_log(ring_buf);
        }
#endif

        // Quick pin snapshot (non-intrusive)
        Serial.printf("[pins] U=%d D=%d L=%d R=%d BTN=%d\n",
                      digitalRead(PIN_TRACKBALL_UP),
                      digitalRead(PIN_TRACKBALL_DOWN),
                      digitalRead(PIN_TRACKBALL_LEFT),
                      digitalRead(PIN_TRACKBALL_RIGHT),
                      digitalRead(PIN_TRACKBALL_BTN));
#if SIGURDOS_CRASH_RING
        {
            char ring_buf[64];
            snprintf(ring_buf, sizeof(ring_buf), "[pins] U=%d D=%d L=%d R=%d BTN=%d",
                     digitalRead(PIN_TRACKBALL_UP),
                     digitalRead(PIN_TRACKBALL_DOWN),
                     digitalRead(PIN_TRACKBALL_LEFT),
                     digitalRead(PIN_TRACKBALL_RIGHT),
                     digitalRead(PIN_TRACKBALL_BTN));
            ring_log(ring_buf);
        }
#endif
    }
}

void dump_system()
{
    if (!feat_diag) return;

    uint32_t now = millis();

    Serial.println();
    Serial.println("━━━━━━━━━━━━━━ SigurdOS DEBUG DUMP ━━━━━━━━━━━━━━");
    Serial.printf("[sys]  uptime=%.1fs  free_heap=%u  min_heap=%u  free_psram=%u  min_psram=%u\n",
                  now / 1000.0f,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)ESP.getMinFreePsram());
    Serial.printf("[cpu]  freq=%uMHz  cores=2  cycle=%u\n",
                  (unsigned)ESP.getCpuFreqMHz(),
                  (unsigned)ESP.getCycleCount());
    Serial.printf("[flash] size=%u  free=%u\n",
                  (unsigned)ESP.getFlashChipSize(),
                  (unsigned)ESP.getFreeSketchSpace());

    uint16_t mv = sigurdos_battery_mv();
    uint8_t pct = sigurdos_battery_pct();
    Serial.printf("[batt] mv=%u  pct=%u%%\n", (unsigned)mv, (unsigned)pct);

    // Feature-conditional dumps
    if (feat_display) dump_display_config();
    if (feat_ui)      dump_lvgl_rendering();
    if (feat_diag)    dump_trackball_state();
    if (feat_ui)      dump_home_screen_layout();
    if (feat_diag)    dump_memory();
    if (feat_mesh)    dump_mesh_state();

    Serial.printf("[tasks] num=%u  stack_hwm=%u\n",
                  (unsigned)uxTaskGetNumberOfTasks(),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    Serial.println("━━━━━━━━━━━━━━ END DUMP ━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println();
}

void dump_display_config()
{
    Serial.println("[display] ── config ──");
    Serial.printf("  TFT_WIDTH=%d  TFT_HEIGHT=%d\n", TFT_WIDTH, TFT_HEIGHT);
    Serial.printf("  DISPLAY_W=%d  DISPLAY_H=%d\n", DISPLAY_W, DISPLAY_H);
    Serial.printf("  CONTENT_X=%d  CONTENT_Y=%d  CONTENT_W=%d  CONTENT_H=%d\n",
                  CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H);
    Serial.printf("  TOP_BAR_H=%d  BOT_BAR_H=%d  DIVIDER_H=%d\n",
                  TOP_BAR_H, BOT_BAR_H, DIVIDER_H);
    Serial.printf("  PIXEL_BORDER=%d\n", PIXEL_BORDER);
    Serial.printf("  LV_COLOR_DEPTH=%d  LV_COLOR_FORMAT=%d\n",
                  (int)LV_COLOR_DEPTH, (int)LV_COLOR_FORMAT_NATIVE);
    Serial.printf("  display_on=%d\n", sigurdos_display_is_on() ? 1 : 0);

    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        lv_display_rotation_t rot = lv_display_get_rotation(disp);
        int32_t dw = lv_display_get_horizontal_resolution(disp);
        int32_t dh = lv_display_get_vertical_resolution(disp);
        Serial.printf("  lv_disp: res=%ldx%ld  rot=%d\n",
                       (long)dw, (long)dh, (int)rot);
    } else {
        Serial.println("  lv_disp: NULL");
    }
}

static bool is_sensitive_text_subtree(lv_obj_t* obj)
{
    for (lv_obj_t* cursor = obj; cursor; cursor = lv_obj_get_parent(cursor)) {
        if (lv_obj_check_type(cursor, &lv_textarea_class) ||
            lv_obj_has_flag(cursor, LV_OBJ_FLAG_USER_1)) {
            return true;
        }
    }
    return false;
}

static void dump_obj_tree(lv_obj_t* obj, int depth)
{
    if (!obj) return;

    int32_t x = lv_obj_get_x(obj);
    int32_t y = lv_obj_get_y(obj);
    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);
    lv_coord_t border_w = lv_obj_get_style_border_width(obj, 0);
    lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
    lv_coord_t radius = lv_obj_get_style_radius(obj, 0);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const char* type = "obj";
    if (lv_obj_check_type(obj, &lv_label_class)) type = "label";

    char label_text[33] = "";
#if SIGURDOS_DEBUG_UI_TEXT
    if (lv_obj_check_type(obj, &lv_label_class)) {
        if (sigurdos::diagnostics::debug_text_capture_allowed(
                true, is_sensitive_text_subtree(obj))) {
            const char* txt = lv_label_get_text(obj);
            if (txt) { strncpy(label_text, txt, 32); label_text[32] = '\0'; }
        } else {
            strncpy(label_text, "<redacted>", sizeof(label_text));
        }
    }
#endif

    for (int i = 0; i < depth; i++) Serial.print("  ");
    Serial.printf("%s pos=(%ld,%ld) size=(%ld,%ld) bw=%d opa=%d r=%d "
                  "coords=(%ld,%ld,%ld,%ld)",
                  type, (long)x, (long)y, (long)w, (long)h,
                  (int)border_w, (int)bg_opa, (int)radius,
                  (long)coords.x1, (long)coords.y1,
                  (long)coords.x2, (long)coords.y2);

    if (label_text[0]) Serial.printf(" text=\"%s\"", label_text);
    Serial.println();

    // Feed watchdog / USB CDC during very deep manual dumps
    static uint16_t yield_cnt = 0;
    if (++yield_cnt % 8 == 0) {
        yield();
    }

    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        dump_obj_tree(lv_obj_get_child(obj, i), depth + 1);
    }
}

void dump_lvgl_rendering()
{
    Serial.println("[lvgl] ── object tree ──");
    lv_obj_t* act = lv_scr_act();
    if (!act) { Serial.println("  No active screen"); return; }
    Serial.printf("  Active screen: %p  children=%u\n", act, (unsigned)lv_obj_get_child_count(act));
    dump_obj_tree(act, 0);
}

void dump_trackball_state()
{
    Serial.println("[trackball] ── pin state ──");
    Serial.printf("  UP(gpio%d)=%d  DOWN(gpio%d)=%d  LEFT(gpio%d)=%d  RIGHT(gpio%d)=%d  BTN(gpio%d)=%d\n",
                  PIN_TRACKBALL_UP, digitalRead(PIN_TRACKBALL_UP),
                  PIN_TRACKBALL_DOWN, digitalRead(PIN_TRACKBALL_DOWN),
                  PIN_TRACKBALL_LEFT, digitalRead(PIN_TRACKBALL_LEFT),
                  PIN_TRACKBALL_RIGHT, digitalRead(PIN_TRACKBALL_RIGHT),
                  PIN_TRACKBALL_BTN, digitalRead(PIN_TRACKBALL_BTN));
}

void dump_home_screen_layout()
{
    Serial.println("[layout] ── responsive layout ──");
    GridLayout gl = compute_grid(3);
    Serial.printf("  compute_grid => cols=%d\n", gl.cols);
    Serial.printf("  DISPLAY_W=%d  DISPLAY_H=%d\n", DISPLAY_W, DISPLAY_H);
    Serial.printf("  CONTENT_X=%d  CONTENT_Y=%d  CONTENT_W=%d  CONTENT_H=%d\n",
                  CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H);
    Serial.printf("  TOP_BAR_H=%d  BOT_BAR_H=%d  DIVIDER_H=%d\n",
                  TOP_BAR_H, BOT_BAR_H, DIVIDER_H);

    int active_cols = gl.cols;
    int active_rows = (12 + active_cols - 1) / active_cols;
    int usable_w = CONTENT_W - (3 * 2) - (3 * (active_cols - 1));
    int usable_h = CONTENT_H - (3 * 2) - (3 * (active_rows - 1));
    int base_w = usable_w / active_cols;
    int extra_w = usable_w - (base_w * active_cols);
    int base_h = usable_h / active_rows;
    int extra_h = usable_h - (base_h * active_rows);

    Serial.printf("  tile_base_w=%d  extra_w=%d  tile_base_h=%d  extra_h=%d\n",
                  base_w, extra_w, base_h, extra_h);

    static const char* icon_names[] = {
        "CHATS", "CONTACTS", "REPEATERS", "FINDER", "PACKETS", "MAP",
        "ADVERTISE", "SETTINGS", "TRACE", "TERMINAL", "SETUP", "SIGNAL"
    };

    for (int i = 0; i < 12; i++) {
        int col = i % active_cols;
        int row = i / active_cols;
        int tw = base_w + (col < extra_w ? 1 : 0);
        int th = base_h + (row < extra_h ? 1 : 0);
        int tx = 3 + col * (base_w + 3) + (col < extra_w ? col : extra_w);
        int ty = 3 + row * (base_h + 3) + (row < extra_h ? row : extra_h);
        Serial.printf("  tile[%02d] %-10s col=%d row=%d x=%d y=%d w=%d h=%d  r=%d b=%d\n",
                      i, icon_names[i], col, row, tx, ty, tw, th, tx + tw, ty + th);
    }
}

void dump_memory()
{
    Serial.println("[mem] ── heap & PSRAM ──");
    Serial.printf("  heap_free=%u  heap_min=%u  max_alloc=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    Serial.printf("  psram_free=%u  psram_min=%u  max_alloc=%u\n",
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)ESP.getMinFreePsram(),
                  (unsigned)ESP.getMaxAllocPsram());
    Serial.printf("  stack_hwm=%u (main task)\n",
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("  lvgl_mem: total=%u  free=%u  used_pct=%u  frag=%u  biggest=%u\n",
                  (unsigned)mon.total_size, (unsigned)mon.free_size,
                  (unsigned)mon.used_pct, (unsigned)mon.frag_pct,
                  (unsigned)mon.free_biggest_size);
}

void dump_mesh_state()
{
    Serial.println("[mesh] ── radio & contacts ──");
    Serial.printf("  own_name=%s\n", sigurdos::mesh::getOwnName());
    Serial.printf("  contacts=%d  channels=%d\n",
                  sigurdos::mesh::getContactCount(),
                  sigurdos::mesh::getChannelCount());
    Serial.printf("  rssi=%d  snr=%.1f  noise=%d\n",
                  sigurdos::mesh::getLastRSSI(),
                  sigurdos::mesh::getLastSNR(),
                  sigurdos::mesh::getNoiseFloor());
}

} // namespace debug
} // namespace sigurdos

#endif // SIGURDOS_DEBUG
