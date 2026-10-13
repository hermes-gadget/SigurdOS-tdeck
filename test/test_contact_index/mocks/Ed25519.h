#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock Ed25519 for native testing — non-cryptographic, mirroring the upstream
// mocks' approach (mock SHA256/AES): lets core sources compile natively
// without linking the Crypto library (which does not build for framework-less
// native environments). Fails closed: no signature ever verifies here.
class Ed25519 {
public:
  static bool verify(const uint8_t* signature, const uint8_t* public_key,
                     const void* message, size_t msg_len) {
    (void)signature;
    (void)public_key;
    (void)message;
    (void)msg_len;
    return false;
  }
};
