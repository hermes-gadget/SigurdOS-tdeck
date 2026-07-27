# Production ESP32-S3 Root of Trust

**Tracks:** GitHub issue #1210  
**Status:** SigurdOS intentionally provides no eFuse provisioning path.

Current SigurdOS T-Deck artifacts are unsigned and unencrypted. This repository
does not contain a production provisioning profile, script, or process. Normal
build, CI, OTA, and hardware-test paths must never burn eFuses.

## Current profile

| Control | SigurdOS artifact |
|---------|--------------------|
| Secure Boot V2 | Disabled; firmware is unsigned |
| Flash encryption | Disabled; flash and NVS are unencrypted |
| Hardware anti-rollback | Disabled |
| Software rollback | Enabled for OTA boot-health recovery |
| Image epoch | Software comparison only (`SIGURDOS_SECURITY_EPOCH`) |
| Debug/download access | Not disabled through eFuses |

## Build profiles

| Profile | How | eFuse impact |
|---------|-----|--------------|
| **Development / release** (`SigurdOS_TDeck`) | Current `sdkconfig.defaults` | Explicitly configured never to burn eFuses |
| **Production candidate** | Not provided by this repository | Enabling Secure Boot, release-mode flash encryption, hardware anti-rollback, or ROM download disable can burn irreversible eFuses |

There is no `sdkconfig.defaults.production`. Adding or merging an irreversible
profile into this repository contradicts its “never burn eFuses” contract.

## Provisioning boundary

Production provisioning is not part of this repository. If a future product
requires a hardware root of trust, it must use a separately authorized
manufacturing system with separate hardware ownership and review.

`esptool` flashes bootloaders, partition tables, and application images.
`espefuse` reads and programs eFuses; it is not an image-flashing tool. No
SigurdOS workflow should invoke `espefuse` to program a device.

## OTA interaction

Network OTA already enforces:

- HTTPS transport auth for GitHub downloads
- Security epoch anti-downgrade in the app descriptor
- ESP image verify (`esp_image_verify`) on the pending OTA partition after write
- Local AP OTA requires WPA2 session secret + 6+ digit device PIN + CSRF

These checks do not provide device-enforced publisher signatures. A separately
provisioned product could use Secure Boot for that property, but enabling it is
outside the SigurdOS repository and can irreversibly alter hardware.

## What this repo will not do

- Burn eFuses from any build, CI, OTA, or hardware-test path
- Provide eFuse provisioning scripts or configuration profiles
- Commit private signing keys
- Claim that current artifacts use Secure Boot, flash encryption, or encrypted NVS

## Acceptance for #1210

- [x] Default configuration explicitly prohibits eFuse-burning features
- [x] OTA health confirmation fails to compile with hardware anti-rollback
- [x] eFuse safety contract rejects dangerous default settings
- [x] Production provisioning profile and stale process removed
- [x] Security documentation describes unsigned, unencrypted artifacts

See [the eFuse audit](EFUSE_AUDIT.md) for the findings and technical basis.
