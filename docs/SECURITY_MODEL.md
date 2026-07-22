# Security model

This document states the trust boundaries of SigurdOS T-Deck. It is a review
aid, not a claim that physical possession of the device is harmless.

## Companion transports

| Boundary | Protection | Important limitation |
|---|---|---|
| BLE companion | ESP32 BLE Secure Connections bonding and the displayed pairing passkey | A bonded phone is an administrator. Remove stale bonds before transferring the device. The four-digit device PIN is not the BLE authenticator. |
| USB companion | Physical access to the USB data port | Companion USB is a diagnostic/trusted-host transport and has no protocol authentication. Do not connect it to an untrusted host. |
| Device PIN | Local LVGL Settings and identity-administration gates | The PIN is a short UI access-control mechanism, not disk encryption and not a companion-protocol credential. It does not protect USB administration. |

Companion commands can change configuration and, where enabled, import or
export identity material. Private-key import/export can be removed at compile
time with `SIGURDOS_ENABLE_PRIVATE_KEY_IMPORT=0` and
`SIGURDOS_ENABLE_PRIVATE_KEY_EXPORT=0`. A build that exposes export must treat
the connected BLE peer or USB host as trusted with the node's identity.

Factory reset is destructive, clears persisted application state and companion
credentials, and returns the device to onboarding. The local UI confirms the
operation. Over BLE it relies on the bonded-administrator boundary; over USB it
relies only on physical/trusted-host access. After a reset, previously bonded
phones must not regain an administrative session without pairing again.

## Stored secrets and physical access

Release builds enable ESP32 flash encryption and encrypted NVS partitions.
Those controls reduce offline extraction from a normally provisioned device;
they do not make a powered, unlocked, debug-enabled, or physically modified
device trustworthy. Debug and custom development builds can deliberately use a
weaker configuration and must not be represented as release-equivalent.

Assume an attacker with sustained physical possession can erase or replace the
device. Back up identity material only to a trusted system and erase/bond-reset
the device before transferring ownership.

## Firmware update trust

HTTPS authenticates the update server and protects transport. The release OTA
path validates the server certificate chain and rejects images whose embedded
security epoch is older than the running image. These checks address transport
impersonation and downgrade; they are not firmware publisher signatures.

SHA-256 files and the release evidence attestation detect accidental corruption
and bind recorded evidence to exact bytes. **A checksum is not a signature:** an
attacker who can replace both an image and its checksum can forge both. Until a
device-side public-key signature verifier is implemented, release authenticity
ultimately depends on the HTTPS/repository publication boundary. Keep an
offline serial recovery path available for certificate rotation or failed OTA.

## Release evidence

Every release candidate records these negative and lifecycle cases:

| Case | Required result |
|---|---|
| Firmware signature | Record whether device-side signer verification exists; do not substitute a checksum result. |
| Downgrade attempt | An image with a lower security epoch is rejected before flash activation. |
| TLS expiry/rotation | Expired or untrusted chains fail closed; the alternate maintained root and offline recovery path are exercised. |
| BLE bond revocation | A removed bond cannot reconnect as an administrator without new pairing. |
| Factory reset reconnect | Old companion credentials and bonds cannot silently regain access after reset. |
| USB administration | Test evidence labels the host and cable as physically trusted; it must not claim protocol authentication. |

See [Release evidence](RELEASE_EVIDENCE.md) for the recording format and
[Companion support](COMPANION_SUPPORT.md) for the command matrix.
