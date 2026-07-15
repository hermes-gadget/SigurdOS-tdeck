#pragma once

#include <cstddef>
#include <cstdint>

// Minimal native-test stand-in for Arduino Crypto's SHA256. Transport-key
// store tests exercise explicit private keys and do not derive hashtag keys.
class SHA256 {
public:
    void update(const void*, size_t) {}
    void finalize(void* out, size_t len) {
        uint8_t* bytes = static_cast<uint8_t*>(out);
        for (size_t i = 0; i < len; ++i) bytes[i] = 0;
    }
    void resetHMAC(const void*, size_t) {}
    void finalizeHMAC(const void*, size_t, void* out, size_t len) {
        finalize(out, len);
    }
};
