# SigurdOS T-Deck — Project History & Direction

**Written 2026-08-03.** A full-arc review of the project from genesis to today,
and where it is heading. Companion to [`ROADMAP.md`](ROADMAP.md) (the forward
plan). Facts below are drawn from the git history (1,203 commits), the release
tags, the session records, and the design-goal briefs given by Ben.

---

## Part 1 — The Arc: Four Eras in Twelve Weeks

### Era 0 · Genesis — "SlopOS" (2026-05-14 → 05-31) · 360 commits

- **Day 1 (2026-05-14):** `init: SlopOS T-Deck firmware project skeleton` —
  the project is born as **SlopOS** with a dark Discord-like theme, splash
  screen, 3×4 home grid, and the MeshCore submodule wired in the *same day*.
  The concept from the first commit: "Discord UI on a LoRa radio."
- The rest of May is pure **feature velocity**: the MeshCore protocol surface
  was implemented phase-by-phase (Phase 0 → 4.8):
  - Chat, DMs, rooms, contacts, repeaters with login flows, adverts
  - GPS enable/interval controls, telemetry request with **CayenneLPP parsing**
  - Path discovery with flood-force routing, status requests, custom variables
  - Message search, **ACK delivery ticks in chat bubbles**, per-contact
    RSSI/SNR **sparklines**
  - Runtime theme system, contact persistence, onboarding wizard, emoji
    support, channel/message persistence, web-flasher compatibility
  - `SlopMeshV2` (now `SigurdMeshV2`) cutover as the default mesh (May 29)
- **Releases:** `beta-0.1.00` on day 1, `beta-0.1.31` by May 24 — about 1.3
  versions/day. Raw, rapid, prototype energy.

**Lesson of the era:** the product concept was right and the protocol was
understood early, but everything was built at prototype speed.

### Era 1 · Breadth — the rebrand and everything at once (2026-06) · 388 commits

- **2026-06-01:** `Rebrand: SlopOS -> SigurdOS`; repo moves to
  `hermes-gadget/SigurdOS-tdeck` with a fresh banner.
- June fills the feature surface: settings category submenus, guided repeater/
  room login, anonymous messages, room-server message fetch, group datagrams,
  repeater favourites, **Launcher compatibility (C1–C7, O3)**, deep-sleep
  preparation, boot watchdog, SPIFFS erased-partition recovery, and the first
  **BLE companion** work with the official MeshCore client interop harness.
- Releases `0.1.31` → `0.1.42`; the last pure "beta" tags.

**Lesson of the era:** breadth arrived — every screen, every protocol feature —
but reliability was still catching up (watchdog, sleep, SPIFFS recovery were
built *because* the field hurt).

### Era 2 · Trust — hardening (2026-07) · 387 commits

The pivot from *features* to *trust*:

- **2026-07-10:** the first **full repository audit** (`audit.md`) →
  **26 fixes** across four branches (`fix/audit-build-infra`,
  `fix/audit-security-auth`, `fix/audit-radio-map`, `fix/audit-concurrency`),
  issues #1466–#1490: build infra, security/auth, radio/map, concurrency.
- Companion BLE hardened: MTU-safe delivery, PIN pairing fixes, bond rotation,
  auth throttling/watchdogs, secure wipe.
- **eFuse / root-of-trust audit (#1210):** deliberate posture — SigurdOS
  provides **no eFuse provisioning path**; documented as an intentional limit.
- **ASan/UBSan `native_sanitize` environment** added; the suite validated
  under sanitizers.
- `BUG-004` (processAck returning the wrong contact) fixed with a regression test.
- **2026-07-31:** **SigurdOS-client (Flutter) is deprecated.** The companion
  app is no longer used. This is the era's defining decision: **the firmware
  itself is the product** — no second screen, no app crutch.
- Release cadence slows deliberately: RC5 (6/28) → RC6 (7/12) → RC7 (7/18) →
  RC8 (7/31) → RC9 (8/2). Every release is now a *release candidate*, gated
  by RELEASE_EVIDENCE requirements.

**Lesson of the era:** the project grew up. Audits became routine, security
became posture, and the app was cut so the device could be the product.

### Era 3 · Focus — the parity campaign and polish (2026-08) · 68 commits

- **2026-07-29 → 08-03:** the **Wadamesh parity campaign** (approved 6-phase
  plan, up to 6 MissionDeck Lunamax agents):
  1. **Multi-transport companion** — TCP:5000 + WS:8765 servers alongside USB
     and BLE; push-to-all-clients with per-client sync dedup
  2. **SD deep history** — 5000 messages on `/sdcard/msgs`, SPIFFS fallback
  3. **Lock screen + idle power regime** — auto-off → lock, unlock, throttled
     idle loop
  4. **i18n** — EN/DE/FR/ES with a persisted language picker
  5. (WebMirror — **spawned then cancelled by Ben**)
  6. (MQTT bridge — **cancelled by Ben**; meshcore-mqtt covers broker work)
- **Text-fit follow-up:** UI-wide adaptive font ladder (16→8px, SemiBold 8px
  floor) — fixed the i18n overflow bug found on hardware; LVGL render-assert
  boot regression fixed the same day.
- **Hardware verification campaign** (2026-08-03): wizard completed on-device,
  German locale verified visually, transports/lock/SD screens captured and
  vision-verified, boot logs clean. **1622 native tests green.**
- **2026-08-02:** the **v0.6 brainstorm** — 13 new feature ideas + 4 roadmap
  pull-forwards (see Part 4).
- **2026-08-03:** NVS-reset-on-reflash finding filed as **#1492**; 11 stale
  MD docs purged; **fresh ROADMAP.md** written; current state
  `beta-0.1.47-RC9`, HEAD `3fe51b45`.

**Lesson of the era:** with scope decided and the surface complete, the work
turns to finishing — languages, power, persistence, proof.

---

## Part 2 — Where We Are Now (2026-08-03)

| Metric | Value |
| --- | --- |
| Commits | 1,203 (May 14 → Aug 3, 2026) |
| Code | ~100K LOC (src + test) |
| Releases | 49 tags, latest `beta-0.1.47-RC9` (pre-release) |
| Native tests | 1,622 (1,621 pass, 1 ESP32-only skip) |
| CI workflows | 6 (build-release, validation matrix, dependency refresh, nightly smoke, PR CI, security) |
| Hardware | T-Deck verified: wizard, home, transports, lock, i18n (DE), WiFi scan, SD storage |
| Transport surface | USB + BLE + TCP:5000 + WS:8765 companion serving |
| Persistence | SD deep history (5000), SPIFFS fallback, NVS prefs (**#1492 bug**), contacts, channels |
| Scope lock | T-Deck only; no WebMirror, no on-device MQTT, no app client |

The firmware now meets the design goals: polished handheld messenger,
MeshCore-interoperable, offline-first, battery-aware, multi-language, with a
real test and release discipline.

---

## Part 3 — The End Goal in Sight

Reading the arc — prototype (May) → breadth (Jun) → trust (Jul) → focus (Aug) —
the project has been converging on a single destination:

> **A production-grade, single-purpose handheld mesh radio — the device IS the
> product, and it must be trustworthy enough to be your field identity.**

Every big decision points there:
- **Firmware-first:** the companion app was *deleted* from the story (Jul 31).
- **Scope discipline:** boards, WebMirror, MQTT all declined — energy stays on
  one device, done right.
- **Trust as the core value:** two full audit waves, sanitizer runs, soak
  evidence, release gates. And the deepest signal: **device identity equals
  mesh presence** — losing on-device data means losing your place in the
  network. That is why #1492 and backup matter more than any feature.

**What "done" looks like (the end goal, concretely):**
1. **No data loss** — NVS persistence fixed and proven across reflash/OTA;
   identity/contacts/channels restorable (backup bundle)
2. **Full transport proof** — all four companion transports verified end-to-end
   against the official protocol, in the field
3. **Language complete** — NL/IT/PT added, zero clipped text in any locale
4. **Field-tested** — battery numbers, soak evidence, GPS UX, duty-cycle
   compliance for EU use
5. **A stable release** — the first non-RC tag, with RELEASE_EVIDENCE gates
   satisfied
6. **The safety story** — SOS beacon, because a field radio should be able to
   call for help

---

## Part 4 — The Future: What We Need to Do

### 4.1 From the current ROADMAP (phases A–E, P1 first)

- **A — Reliability:** fix #1492, OTA round-trip on hardware, battery
  measurement, soak evidence
- **B — i18n:** Dutch (first), Italian, Portuguese; fix the SETUP-tile
  residuals; locale grid sweep on hardware
- **C — Transport proof:** TCP/WS end-to-end with real WiFi; complete the
  USB/BLE/TCP/WS interop matrix
- **D — Field polish:** GPS position UX, offline map verification, notification
  depth, power/lock tuning
- **E — Release:** RELEASE_EVIDENCE gates, test-builds flow, flasher manifest

### 4.2 From the v0.6 brainstorm (2026-08-02, all 13 + 4 pull-forwards)

| # | Idea | Why | Effort |
| --- | --- | --- | --- |
| 1 | **Backup bundle to SD** (identity+contacts+channels+prefs, restore menu) | Identity = mesh presence; one-tap insurance; device migration | M |
| 2 | SD-card OTA sideload | Field updates and recovery without WiFi | M |
| 3 | **GPX track export** | The payoff for recorded tracks; view routes anywhere | S |
| 4 | **Canned messages / quick replies** | Typing is the slowest interaction; presets kill most of it | S |
| 5 | In-chat location sharing | Field/safety staple; map-renderable | S–M |
| 6 | **SOS / emergency beacon** (hold combo → SOS + GPS, repeats, buzzer) | The #1 reason people carry a radio in the field | M |
| 7 | Keyword & contact alerts | Mesh @mentions; catch what you'd miss | M |
| 8 | Airtime / duty-cycle meter | EU 1% duty-cycle compliance without math | S |
| 9 | Mesh time sync | Correct chat ordering without GPS/app | S |
| 10 | Reply/quote + forward | Cheap chat-UI wins | S |
| 11 | Waypoints + nav (bearing/distance) | Field navigation without a phone | M |
| 12 | WebUI over WiFi | Full keyboard/screen from a laptop; biggest ease lever, biggest effort (note: the mirror half was cancelled — a management page is different and remains open) | L |
| 13 | i18n expansion | European user base (now partially done; NL/IT/PT remain) | L |

**Pull forward from the old Phase 6:** draft persistence (don't lose half-typed
messages), power profiles (Field vs Desk one-tap), online map tiles + negative
cache, repeater/room guided login with explicit errors.

**Top-3 recommendation (2026-08-02, still valid):** 1) SD backup bundle,
2) canned messages, 3) SOS beacon. In one line: *protect the identity, speed
up the typing, make it a safety tool.*

### 4.3 Process improvements this history argues for

1. **Data-loss bugs outrank features.** #1492 is the single most important
   open item — it undermines the identity-equals-presence promise.
2. **i18n was an afterthought** (7 commits in the entire history). Treat
   languages as a first-class feature: finish NL/IT/PT and keep translations
   in lockstep with new strings.
3. **Ship a stable.** 49 tags, all pre-release. The RC discipline is good; a
   stable gate (gates satisfied → 0.1.0 stable) is the missing finale.
4. **Hardware verification is now protocolized** (HARDWARE_TESTING) — keep it;
   every future feature should have its on-device proof before merge.
5. **Docs churn is real** (11 MDs purged). Keep the living docs current as we
   go — stale docs cost more than writing them fresh.
6. **Input speed is the UX bottleneck.** Canned messages, draft persistence,
   and quick replies are the cheapest daily-impact wins available.
7. **Field safety completes the product story.** SOS + location sharing +
   waypoints turn a hobby radio into a tool people carry.

---

## Part 5 — The One-Paragraph Summary

Twelve weeks, 1,203 commits: SigurdOS was prototyped at insane speed (May),
broadened into a full mesh-messaging surface (June), hardened into a
trustworthy radio with audits, sanitizers and a deliberate security posture
(July), and focused into a finished, verified, scope-locked device with the
companion app cut and every remaining feature either done or deliberately
declined (August). The end goal is in sight: **a stable, field-ready handheld
whose identity cannot be lost, whose transports all work, whose languages all
fit, and which can call for help.** The path there is Phase A–E of the
ROADMAP, topped up by the v0.6 shortlist — starting with the #1492 fix, the
SD backup bundle, canned messages, and the SOS beacon.
