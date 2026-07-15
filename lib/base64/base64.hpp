// Minimal Base64 decoder — matches the interface expected by BaseChatMesh.cpp
// MeshCore upstream provides this header as part of its full distribution.
#pragma once
#include <string.h>
#include <stdint.h>

static const uint8_t BASE64_DECODE_TABLE[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,62,0,0,0,63,52,53,54,55,56,57,58,59,60,61,0,0,0,0,0,0,
    0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,0,0,0,0,0,
    0,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static inline int decode_base64(const unsigned char* in, unsigned int inLen, unsigned char* out) {
    if (!in || !out || inLen < 4) return 0;
    unsigned int outLen = 0;
    for (unsigned int i = 0; i + 3 < inLen; i += 4) {
        uint8_t a = BASE64_DECODE_TABLE[in[i]];
        uint8_t b = BASE64_DECODE_TABLE[in[i+1]];
        uint8_t c = BASE64_DECODE_TABLE[in[i+2]];
        uint8_t d = BASE64_DECODE_TABLE[in[i+3]];
        out[outLen++] = (a << 2) | (b >> 4);
        if (in[i+2] != '=') out[outLen++] = (b << 4) | (c >> 2);
        if (in[i+3] != '=') out[outLen++] = (c << 6) | d;
    }
    return outLen;
}
