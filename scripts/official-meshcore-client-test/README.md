# Official MeshCore Client Interop (meshcore.js)

**Purpose:** Autonomously prove SigurdOS CompanionBridge speaks the protocol that
Liam Cottle’s official clients use — the same library as [app.meshcore.nz](https://app.meshcore.nz/).

This does **not** drive Web Bluetooth in a browser (that still needs a human).
It uses the **official Node transport** of `@liamcottle/meshcore.js` over USB serial companion framing, which is the same command/response surface the web/app clients use after APP_START / DEVICE_QUERY.

## Why this is the best autonomous check

| Check | Confidence for “app will work” |
|-------|--------------------------------|
| Native unit tests | High for frame layout, not live device |
| Python `meshcore` package | Good community client |
| **meshcore.js over USB** | **Highest** — official client library |
| app.meshcore.nz Web BLE | True UI, not agent-automatable |

## Targets

1. **Baseline:** Heltec V3 stock `companion_radio_usb` on Hermes VM `/dev/ttyUSB0`
2. **Product under test:** T-Deck `SigurdOS_TDeck_companion_usb` on Pi `/dev/ttyACM0`

## Setup

```bash
cd ~/SigurdOS-tdeck/scripts/official-meshcore-client-test
npm install
```

## Run

```bash
# Stock MeshCore companion (baseline — must stay green)
node matrix.mjs --port /dev/ttyUSB0 --label heltec-stock --json-out /tmp/official_heltec.json

# SigurdOS T-Deck companion USB (flash companion_usb first)
node matrix.mjs --port /dev/ttyACM0 --label tdeck --json-out /tmp/official_tdeck.json
```

### Flash T-Deck companion USB

```bash
cd ~/SigurdOS-tdeck
pio run -e SigurdOS_TDeck_companion_usb
scp .pio/build/SigurdOS_TDeck_companion_usb/firmware-merged.bin hermes-pi:/tmp/tdeck_companion_usb.bin
ssh hermes-pi '~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0 /tmp/tdeck_companion_usb.bin'
# wait ~12s for boot, then run matrix from Pi or via SSH-forwarded serial
```

On the Pi (if node/npm available), or run from VM when T-Deck is attached there:

```bash
# From hermes-pi after copying the test dir:
scp -r ~/SigurdOS-tdeck/scripts/official-meshcore-client-test hermes-pi:/tmp/
ssh hermes-pi 'cd /tmp/official-meshcore-client-test && npm install && node matrix.mjs --port /dev/ttyACM0 --label tdeck --json-out /tmp/official_tdeck.json'
```

## Matrix cases (required)

| Case | Official API | App relevance |
|------|--------------|---------------|
| connect | `deviceQuery` in `onConnected` | Version negotiation |
| getSelfInfo_appStart | `getSelfInfo` → APP_START | First thing the app does |
| syncDeviceTime | `syncDeviceTime` | Clock for message order |
| getDeviceTime | `getDeviceTime` | Time readback |
| getContacts | `getContacts` | Contact sync |
| getWaitingMessages | `getWaitingMessages` | Offline queue drain |
| getChannels | `getChannels` | Channel list |
| getBatteryVoltage | `getBatteryVoltage` | Status UI |
| exportLocalContact | `exportContact()` | Share-me / identity advert |

## Exit codes

- `0` — all required cases passed  
- `1` — connected but a required case failed  
- `2` — could not open/connect  

## Agent workflow

```bash
# 1) Baseline stock companion
cd ~/SigurdOS-tdeck/scripts/official-meshcore-client-test && npm install
node matrix.mjs --port /dev/ttyUSB0 --label heltec --json-out /tmp/official_heltec.json

# 2) Flash T-Deck companion_usb + matrix (see flash commands above)

# 3) Keep BLE health separate (advertise only) with ble_agent firmware when testing BLE
```

## Limits

- Does not exercise Web Bluetooth / MITM PIN UI (use a human on app.meshcore.nz once per release).
- companion_usb disables BLE transport on T-Deck for that image — dual-transport needs separate builds.
- Do not log private keys; this matrix avoids private-key export.

## Related

- `docs/COMPANION_BLE_TEST_ENV.md` — BLE advertise health harness  
- `docs/COMPANION_SUPPORT.md` — command support matrix  
- `docs/COMPANION_PARITY_ACTION_PLAN.md` — broader parity plan  
