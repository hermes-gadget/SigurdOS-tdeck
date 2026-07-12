# Autonomous MeshCore BLE Companion Test Environment

**Goal:** Let Hermes Agent repeatedly validate that SigurdOS-TDeck speaks the companion protocol used by **Liam Cottle’s official MeshCore app**, without requiring a human to drive the phone UI for every change.

## Honest scope

| Autonomous today | Still human / blocked |
|------------------|------------------------|
| T-Deck BLE **advertising health** (NUS UUID + `MeshCore-*` name) | Full BLE **MITM PIN pair** via BlueZ on hermes-pi (connect drops before bond) |
| Serial control: `ble status\|on\|off\|pin` | Driving the closed-source Android/iOS UI |
| Full companion **opcode matrix** via USB stream framing (same codes as the app) against stock companion or `companion_usb` | One-shot official phone-app UX sign-off per release |

The host client is the open-source [`meshcore`](https://pypi.org/project/meshcore/) Python package (+ `bleak`), not a reverse-engineered app binary. It implements the same companion host protocol documented for Liam’s clients (NUS UUIDs / frame opcodes).

Official clients (manual reference):

- Android: `com.liamcottle.meshcore.android`
- Web: https://app.meshcore.nz  
- JS: https://github.com/liamcottle/meshcore.js

## Topology

```
hermes-pi ──USB /dev/ttyACM0──▶ T-Deck (SigurdOS_TDeck_ble_agent)
    │                               │
    │ BLE scan (hci0)               │ advertises MeshCore-<name>
    └───────────────────────────────┘   NUS 6E400001-…

Hermes VM ──USB /dev/ttyUSB0──▶ Heltec V3 stock companion_usb
    │                               │
    │ meshcore.create_serial        │ full opcode matrix (proven)
    └───────────────────────────────┘
```

## Firmware

```bash
cd ~/SigurdOS-tdeck
pio run -e SigurdOS_TDeck_ble_agent
scp .pio/build/SigurdOS_TDeck_ble_agent/firmware-merged.bin hermes-pi:/tmp/ble_agent.bin
ssh hermes-pi '~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0 /tmp/ble_agent.bin'
```

`SigurdOS_TDeck_ble_agent` = remote test controller + radio + BLE validation telemetry.

### Serial commands

```
ble status   # available/enabled/connected/pin/name/last_sync
ble on|off
ble pin
```

Advertised name: `MeshCore-<node_name>`.

## Host install (hermes-pi)

```bash
python3 -m venv ~/meshcore-ble-venv
~/meshcore-ble-venv/bin/pip install -U pip
~/meshcore-ble-venv/bin/pip install 'meshcore>=2.3.7' bleak pyserial dbus-next
scp ~/SigurdOS-tdeck/scripts/meshcore_ble_companion_test.py hermes-pi:/tmp/
ssh hermes-pi 'bluetoothctl power on'
```

## Agent commands

### A) BLE advertise health (works today)

```bash
ssh hermes-pi '~/meshcore-ble-venv/bin/python /tmp/meshcore_ble_companion_test.py \
  --auto --transport ble-health --json-out /tmp/ble_health.json'
```

Pass criteria: at least one advertiser with name `MeshCore-*` **or** service UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`.

### B) Full companion opcode matrix over USB (works today)

Same opcodes the official app uses after APP_START — run against Heltec stock companion on the VM:

```bash
~/meshcore-ble-venv/bin/python /home/ben/SigurdOS-tdeck/scripts/meshcore_ble_companion_test.py \
  --transport usb --serial /dev/ttyUSB0 --no-serial \
  --json-out /tmp/companion_usb_matrix.json
```

Cases: connect, device_query, self_info, set/get time, get_contacts, sync_next_message, get_channel_0, get_batt_storage.

For T-Deck itself, flash `SigurdOS_TDeck_companion_usb` and point `--serial` at the T-Deck CDC port (note: that build is USB companion framing, not the test-controller text protocol).

### C) Full BLE matrix (pairing WIP)

```bash
ssh hermes-pi '~/meshcore-ble-venv/bin/python /tmp/meshcore_ble_companion_test.py \
  --auto --transport ble -v --json-out /tmp/ble_matrix.json'
```

**Current lab observation (2026-07-12):** T-Deck connects briefly (`@ble_hw` shows `connect`/`mtu=176`) then disconnects; BlueZ reports pairing failure (`authok=0`). Production MITM (`ESP_LE_AUTH_REQ_SC_MITM_BOND` + ENC_MITM char perms) is intentional for the official app. BlueZ agent registration on hermes-pi needs more work before this path is green.

## Verified on 2026-07-12

| Check | Result |
|-------|--------|
| Build `SigurdOS_TDeck_ble_agent` | PASS |
| Flash T-Deck + `ble on` | PASS — NUS service starts |
| `ble status` / `ble pin` | PASS |
| BLE scan finds `MeshCore-SigurdOS T-Deck` + NUS UUID | PASS |
| USB companion matrix vs Heltec v1.15.0 | PASS (all required cases) |
| BLE MITM pair + GATT matrix via BlueZ | FAIL (pairing) |

## Agent workflow (copy/paste)

```bash
# Flash agent firmware
cd ~/SigurdOS-tdeck && pio run -e SigurdOS_TDeck_ble_agent
scp .pio/build/SigurdOS_TDeck_ble_agent/firmware-merged.bin hermes-pi:/tmp/ble_agent.bin
ssh hermes-pi '~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0 /tmp/ble_agent.bin'
sleep 12

# Deploy harness + BLE health
scp ~/SigurdOS-tdeck/scripts/meshcore_ble_companion_test.py hermes-pi:/tmp/
ssh hermes-pi '~/meshcore-ble-venv/bin/python /tmp/meshcore_ble_companion_test.py --auto --transport ble-health --json-out /tmp/ble_health.json'

# Protocol matrix against stock companion (VM)
python3 ~/SigurdOS-tdeck/scripts/meshcore_ble_companion_test.py --transport usb --serial /dev/ttyUSB0 --no-serial
```

## Security

- Do not paste BLE PINs or private keys into public issues/logs.
- Production BLE stays MITM-bonded; do not weaken auth in release builds.
- Keep testing local-network only.

## Files

| Path | Role |
|------|------|
| `scripts/meshcore_ble_companion_test.py` | BLE health / Python client harness |
| `platformio.ini` → `SigurdOS_TDeck_ble_agent` | Firmware env for BLE advertise tests |
| `src/test/test_controller.cpp` | `ble status\|on\|off\|pin` serial commands |
| `src/mesh/companion_adapter.cpp` | BLE bridge + validation telemetry |
| `scripts/official-meshcore-client-test/` | **Preferred app interop** — meshcore.js USB matrix |

## Related

- `scripts/official-meshcore-client-test/README.md` — official client matrix how-to
- `docs/COMPANION_SUPPORT.md` — command support matrix
- `docs/COMPANION_PARITY_ACTION_PLAN.md` — broader parity plan

## Next hardening (optional follow-ups)

1. BlueZ default agent that supplies the static passkey before GATT discovery.
2. `meshcore` create_ble with `pair_before_connect` + agent path tested end-to-end.
3. Extend official meshcore.js matrix: DM send, channel send, login/CLI, reconnect soak.
4. Keep one human official-app pairing check per release (app.meshcore.nz / Play Store / iOS).
