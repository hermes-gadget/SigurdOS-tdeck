# Production ESP32-S3 Root of Trust

**Tracks:** GitHub issue #1210  
**Status:** Profile and process defined. eFuses are **not** burned by default builds.

This document defines the irreversible production security profile for
SigurdOS T-Deck devices. Development builds remain unsigned and
unencrypted so field bring-up, Launcher installs, and agent flashing keep
working.

## Goals

| Control | Production intent | Dev / CI default |
|---------|-------------------|------------------|
| Secure Boot V2 | Only signed app + bootloader boot | Off |
| Flash encryption | Release mode; keys in eFuse | Off |
| Anti-rollback | `secure_version` + eFuse secure version | Image epoch stamp only (`SIGURDOS_SECURITY_EPOCH`) |
| Debug ports | USB-JTAG / UART download disabled after provision | Enabled |
| Secret storage | NVS encryption / key in eFuse | Plain NVS |

## Build profiles

| Profile | How | eFuse impact |
|---------|-----|--------------|
| **Development** (`SigurdOS_TDeck`) | Current `sdkconfig.defaults` | None |
| **Production candidate** | Merge `sdkconfig.defaults` + `sdkconfig.defaults.production` before first flash of a blank part | None until provision script runs |
| **Provisioned unit** | Run `scripts/provision_production_device.py` (operator) after Q/A | **Irreversible** |

`sdkconfig.defaults.production` enables the compile-time switches only.
Burning eFuses is a separate, intentional operator step.

## Provisioning sequence (operator)

1. Build production images with Secure Boot signing key held **offline**
   (never in GitHub Actions secrets for the private key).
2. Flash unsigned bring-up firmware once for functional test (dev profile).
3. Flash signed bootloader + partition table + app with `espefuse` dry-run.
4. Burn Secure Boot digest, flash-encryption enable, and download-mode
   disable only after signed image boots and OTA epoch path is verified.
5. Record eFuse summary (`espefuse summary`) as release evidence for the
   unit serial number.
6. Document RMA: encrypted flash cannot be recovered without the device
   key; failed units are replaced, not reworked in the field.

## OTA interaction

Network OTA already enforces:

- HTTPS transport auth for GitHub downloads
- Security epoch anti-downgrade in the app descriptor
- ESP image verify (`esp_image_verify`) on the pending OTA partition after write
- Local AP OTA requires WPA2 session secret + 6+ digit device PIN + CSRF

Full publisher authenticity for offline local uploads requires Secure Boot
so a device rejects apps not signed with the production key even if the
web UI is compromised. That is why #814 (local OTA) and #1210 close
together for a production SKU, not for the open-source development image.

## What this repo will not do automatically

- Burn eFuses from CI or from the normal `pio run` / hw_test path
- Commit private signing keys
- Claim “secure boot enabled” without attached eFuse evidence on a unit

## Acceptance for #1210

- [x] Separate production sdkconfig fragment checked in
- [x] Documented irreversible provision + RMA path
- [ ] First production SKU unit with attached `espefuse summary` evidence
- [ ] Release pipeline signs with offline key (ops, not code)

Until the last two boxes are checked, keep describing Secure Boot as
**defined, not yet provisioned on field units**.
