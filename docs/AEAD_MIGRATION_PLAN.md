# MeshCore AEAD Migration Plan

**Tracks:** GitHub issue #1207  
**Status:** Coordination plan only. Wire format remains interoperable with
upstream MeshCore (AES-ECB-style blocks + truncated HMAC).

## Why SigurdOS cannot ship this alone

MeshCore companions, repeaters, room servers, and phone apps share one
ciphertext layout. Changing encryption without a negotiated version breaks
the network. SigurdOS-tdeck consumes MeshCore as a pinned submodule and must
stay wire-compatible with the official app family.

## Current wire crypto (baseline)

- Symmetric block cipher path in MeshCore `Utils.cpp` (AES-128 style blocks)
- HMAC-SHA-256 truncated to a short tag (online forgery cost is low vs modern AEAD)
- No AEAD associated-data binding of route/version metadata

## Target properties

| Property | Target |
|----------|--------|
| Cipher | ChaCha20-Poly1305 or AES-GCM |
| Tag | ≥ 96-bit |
| Nonce | Unique per key (counter or random with rekey) |
| KDF | Separate message keys from long-term identity material |
| AAD | Route type, version, path length, scoped transport codes |
| Replay | Sliding window per peer |
| Negotiation | Version byte / capability bit in advert or hello |
| Legacy | Time-boxed dual-stack; rate-limit invalid legacy tags |

## Phased rollout

1. **Upstream design** — meshcore-dev/MeshCore issue + ADR for frame layout
2. **Capability advert** — nodes advertise AEAD support without requiring it
3. **Dual decode** — accept AEAD and legacy; prefer AEAD when both peers can
4. **Repeater/room** — infrastructure fleets upgrade before phone apps
5. **Companion apps** — official + SigurdOS client flip default to AEAD
6. **Deprecate legacy** — rate-limit then reject legacy ciphertext after date

## SigurdOS interim hardening (without breaking interop)

- Keep rate limits and auth throttles on companion BLE pairing (#814 related)
- Keep OTA image authenticity and epoch checks independent of mesh crypto
- Document threat model in `docs/SECURITY_MODEL.md`
- Do **not** fork silent crypto in `lib/meshcore` for SigurdOS-only messages

## Acceptance for #1207

- [x] Written migration plan with upstream coordination requirement
- [ ] Upstream MeshCore ADR accepted
- [ ] Dual-stack implementation behind capability bit
- [ ] Interop matrix: T-Deck ↔ official companion ↔ repeater

This issue stays **open** until upstream adopts a versioned AEAD path.
SigurdOS will track the pin and add dual-stack support once the upstream
frame is frozen.
