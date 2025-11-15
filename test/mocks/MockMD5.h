#ifndef MOCK_MD5_H
#define MOCK_MD5_H

#ifdef UNIT_TEST

#include <cstdint>
#include <cstring>

// Mock MD5 context structure
struct MD5Context {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
};

// Simple mock MD5 implementation for testing
// This is NOT a real MD5 - it just creates deterministic hashes for testing
inline void MD5Init(struct MD5Context* context) {
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->count[0] = 0;
    context->count[1] = 0;
}

inline void MD5Update(struct MD5Context* context, const uint8_t* input, size_t inputLen) {
    // Simple mock: just XOR the input bytes into the state
    for (size_t i = 0; i < inputLen; i++) {
        context->state[i % 4] ^= input[i];
        context->state[i % 4] = (context->state[i % 4] << 1) | (context->state[i % 4] >> 31);
    }
    context->count[0] += inputLen;
}

inline void MD5Final(uint8_t digest[16], struct MD5Context* context) {
    // Convert state to digest
    for (int i = 0; i < 4; i++) {
        digest[i * 4 + 0] = (context->state[i] >> 0) & 0xFF;
        digest[i * 4 + 1] = (context->state[i] >> 8) & 0xFF;
        digest[i * 4 + 2] = (context->state[i] >> 16) & 0xFF;
        digest[i * 4 + 3] = (context->state[i] >> 24) & 0xFF;
    }
}

#endif // UNIT_TEST

#endif // MOCK_MD5_H
