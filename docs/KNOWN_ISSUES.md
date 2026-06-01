# Known Issues

This document tracks currently open known issues, bugs, and missing features in SigurdOS T-Deck firmware. All historically tracked issues that have been resolved are maintained in the Git history — check merged PRs and commit logs for the full record.

---

## UI / Dialogs

### Factory-reset confirmation "Cancel" deletes its dialog synchronously inside the click handler

On the Settings → System screen, the factory-reset confirmation dialog's **Cancel** button calls `lv_obj_del()` directly on the dialog from inside its own `LV_EVENT_CLICKED` handler (`screens.cpp:4184` — `lv_obj_del(lv_obj_get_parent(lv_event_get_target(ev)))`). Deleting the object whose event is currently being dispatched is a use-after-free: after the handler returns, the LVGL event/indev machinery may still dereference the freed object (gesture state, bubbling, `act->obj`). This is the exact trap called out in the gotchas table ("`lv_obj_del` in handler → use `lv_obj_del_async()`"). Every other dialog-close in the codebase already uses the async form (e.g. `screens.cpp:661, 697, 865, 1085, 1118, 2588, 2970`), so this one site is an inconsistency as well as a latent crash.

**What's needed:** Change the Cancel handler at `screens.cpp:4184` to `lv_obj_del_async(...)`. Audit the file for any other synchronous `lv_obj_del()` reachable from an event callback (the message-list prune in `chat_screen.cpp:1864` is fine — it runs from the message-add path, not an event handler).

---

## Mesh / Code Health

### Dead V1 mesh class (`sigurd_mesh.h`) still compiled in after the Phase 0 cutover

The Phase 0 migration replaced `SigurdMesh` with `SigurdMeshV2` and the roadmap explicitly called for deleting the old class. It was never removed: `sigurd_mesh.h` (828 lines — `class SigurdMesh`, `SlopContact`, `SlopChannel`, `PingResult`) is still `#include`d at `mesh_wrapper.cpp:13`, even though the live implementation is `SigurdMeshV2` (`mesh_wrapper.cpp:53`) and no `SigurdMesh::` method is ever called. The shared `utf8_truncate_bytes()` helper actually lives in `utils/utf8_util.h`, not the V1 header, so nothing in the active build depends on it. Two log strings also still read "SigurdMesh initialized" / "SigurdMesh allocation failed" (`mesh_wrapper.cpp:705, 779`) while V2 is what's running — misleading when reading serial logs.

**What's needed:** Confirm `sigurd_mesh.h` has no remaining references (grep for `SigurdMesh`, `SlopContact`, `SlopChannel`), then remove the file and its `#include`. Update the two log strings to say "SigurdMeshV2". Dead code is a documented rejection trigger; a maintainer should validate before deleting.

---

## Chat Screen

### Long or multi-byte messages are silently truncated with no length feedback

`lv_textarea_set_max_length(input_field, MAX_MSG_BYTES)` (`chat_screen.cpp:1452`) caps the input at 149 — but LVGL's `max_length` counts **characters (code points)**, while the mesh payload limit is 149 **bytes**. `do_send()` (`chat_screen.cpp:1380`) then byte-truncates with `utf8_truncate_bytes()` before transmitting. That truncation is safe (no overflow, no split codepoint), but the user gets no warning: a message containing accented characters or emoji can be cut well before 149 visible characters (149 emoji ≈ 596 bytes → only ~37 survive), and the dropped text is lost silently. There is no character/byte counter on the input bar.

**What's needed:** Show a remaining-bytes counter near the input (computed with the same byte accounting as `utf8_truncate_bytes`), and visually indicate when the next keystroke would be dropped. Optionally block input at the byte limit rather than truncating on send so what the user sees is what gets transmitted.

---

## Channels

### Deleting a channel takes effect on a single tap with no confirmation

The red × delete button in both the chat channel list (`chat_screen.cpp:575`) and the Channels screen (`screens.cpp:5043`) calls `sigurdos::mesh::removeChannel(idx)` immediately on `LV_EVENT_CLICKED`. The button is small (28×24 px) and sits next to the tappable channel row, so on a touchscreen an accidental tap permanently removes a channel (PSK/hashtag and its cached messages) with no undo. This is inconsistent with contact removal, which presents a confirmation dialog with Cancel/Confirm (`screens.cpp:1480`).

**What's needed:** Gate channel removal behind the same confirmation-dialog pattern used for contact removal (Cancel / red Confirm), or add an undo. Apply it to both delete sites so the Chat list and the Channels screen behave the same.

---

## Launcher Compatibility

### SigurdOS doesn't work under bmorcelli/Launcher

[Launcher](https://github.com/bmorcelli/Launcher) is an ESP32 app launcher with explicit T-Deck support (display, touch, keyboard, SD card). A user tried running SigurdOS as a Launcher-launched app and ran into problems — the keyboard doesn't work properly, and many other things break.

**Root cause:** SigurdOS is built as standalone firmware that expects full hardware control at boot. Launcher initialises the display, keyboard, I2C, SPI, and LoRa pins before handing off, which leaves GPIOs, peripheral registers, and I2C bus state in an incompatible state when SigurdOS starts.

**Status:** Not planned. SigurdOS is designed as standalone firmware, not a Launcher app. Fixing this would require deep changes to every HAL driver to detect and handle pre-initialised peripherals.

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full contribution workflow.
