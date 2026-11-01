#include "Crypto.h"
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Crypto {

    // =========================================================================
    // CSPRNG & CONSTANT-TIME UTILITIES
    // =========================================================================

    void GenerateRandomBytes(uint8_t* out, size_t len) {
        if (!out || len == 0) return;
        thread_local std::mt19937_64 rng([]() {
            std::random_device rd;
            return ((uint64_t)rd() << 32) ^ rd();
        }());
        size_t i = 0;
        while (i + sizeof(uint64_t) <= len) {
            uint64_t val = rng();
            std::memcpy(out + i, &val, sizeof(uint64_t));
            i += sizeof(uint64_t);
        }
        if (i < len) {
            uint64_t val = rng();
            std::memcpy(out + i, &val, len - i);
        }
    }

    bool ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len) {
        if (!a || !b) return false;
        uint8_t diff = 0;
        for (size_t i = 0; i < len; ++i) {
            diff |= (a[i] ^ b[i]);
        }
        return (diff == 0);
    }

    // =========================================================================
    // FIPS 180-4 SHA-256 IMPLEMENTATION
    // =========================================================================

    namespace {
        inline uint32_t Rotr32(uint32_t v, int n) {
            return (v >> n) | (v << (32 - n));
        }

        class Sha256Context {
        private:
            uint32_t state[8];
            uint64_t bitCount;
            uint8_t buffer[64];

            static const uint32_t K[64];

            static void Transform(uint32_t s[8], const uint8_t chunk[64]) {
                uint32_t w[64];
                for (int i = 0; i < 16; ++i) {
                    w[i] = ((uint32_t)chunk[i * 4] << 24) |
                           ((uint32_t)chunk[i * 4 + 1] << 16) |
                           ((uint32_t)chunk[i * 4 + 2] << 8) |
                           ((uint32_t)chunk[i * 4 + 3]);
                }
                for (int i = 16; i < 64; ++i) {
                    uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
                uint32_t e = s[4], f = s[5], g = s[6], h = s[7];

                for (int i = 0; i < 64; ++i) {
                    uint32_t S1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
                    uint32_t ch = (e & f) ^ ((~e) & g);
                    uint32_t temp1 = h + S1 + ch + K[i] + w[i];
                    uint32_t S0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
                    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    uint32_t temp2 = S0 + maj;

                    h = g;
                    g = f;
                    f = e;
                    e = d + temp1;
                    d = c;
                    c = b;
                    b = a;
                    a = temp1 + temp2;
                }

                s[0] += a; s[1] += b; s[2] += c; s[3] += d;
                s[4] += e; s[5] += f; s[6] += g; s[7] += h;
            }

        public:
            void Init() {
                state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
                state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
                state[4] = 0x510e527f; state[5] = 0x9b05688c;
                state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
                bitCount = 0;
            }

            void Update(const uint8_t* data, size_t len) {
                if (!data || len == 0) return;
                size_t bufIdx = (size_t)((bitCount / 8) % 64);
                bitCount += (uint64_t)len * 8;

                size_t i = 0;
                if (bufIdx > 0) {
                    size_t needed = 64 - bufIdx;
                    if (len < needed) {
                        std::memcpy(buffer + bufIdx, data, len);
                        return;
                    }
                    std::memcpy(buffer + bufIdx, data, needed);
                    Transform(state, buffer);
                    i += needed;
                }

                for (; i + 64 <= len; i += 64) {
                    Transform(state, data + i);
                }

                if (i < len) {
                    std::memcpy(buffer, data + i, len - i);
                }
            }

            void Final(uint8_t outHash[32]) {
                size_t bufIdx = (size_t)((bitCount / 8) % 64);
                buffer[bufIdx++] = 0x80;
                if (bufIdx > 56) {
                    std::memset(buffer + bufIdx, 0, 64 - bufIdx);
                    Transform(state, buffer);
                    bufIdx = 0;
                }
                std::memset(buffer + bufIdx, 0, 56 - bufIdx);
                for (int i = 0; i < 8; ++i) {
                    buffer[56 + i] = (uint8_t)(bitCount >> (56 - i * 8));
                }
                Transform(state, buffer);

                for (int i = 0; i < 8; ++i) {
                    outHash[i * 4]     = (uint8_t)(state[i] >> 24);
                    outHash[i * 4 + 1] = (uint8_t)(state[i] >> 16);
                    outHash[i * 4 + 2] = (uint8_t)(state[i] >> 8);
                    outHash[i * 4 + 3] = (uint8_t)(state[i]);
                }
            }
        };

        const uint32_t Sha256Context::K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };
    }

    void Sha256(const uint8_t* data, size_t len, uint8_t outHash[SHA256_DIGEST_SIZE]) {
        Sha256Context ctx;
        ctx.Init();
        if (data && len > 0) {
            ctx.Update(data, len);
        }
        ctx.Final(outHash);
    }

    std::string Sha256Hex(const uint8_t* data, size_t len) {
        uint8_t hash[SHA256_DIGEST_SIZE];
        Sha256(data, len, hash);
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
            oss << std::setw(2) << (int)hash[i];
        }
        return oss.str();
    }

    std::string Sha256Hex(const std::string& input) {
        return Sha256Hex(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    }

    void DeriveKey(const std::string& passwordOrToken, const uint8_t* salt, size_t saltLen, uint8_t outKey[KEY_SIZE]) {
        Sha256Context ctx;
        ctx.Init();
        ctx.Update(reinterpret_cast<const uint8_t*>(passwordOrToken.data()), passwordOrToken.size());
        if (salt && saltLen > 0) {
            ctx.Update(salt, saltLen);
        }
        ctx.Final(outKey);
    }

    // =========================================================================
    // RFC 8439 CHACHA20 STREAM CIPHER
    // =========================================================================

    namespace {
        inline uint32_t Rotl32(uint32_t v, int n) {
            return (v << n) | (v >> (32 - n));
        }

        #define CHACHA_QR(a, b, c, d) \
            a += b; d ^= a; d = Rotl32(d, 16); \
            c += d; b ^= c; b = Rotl32(b, 12); \
            a += b; d ^= a; d = Rotl32(d, 8);  \
            c += d; b ^= c; b = Rotl32(b, 7);

        void ChaCha20Block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t outBlock[64]) {
            uint32_t s[16];
            // Constants "expand 32-byte k"
            s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;

            // 256-bit Key (8 words, Little-Endian)
            for (int i = 0; i < 8; ++i) {
                s[4 + i] = (uint32_t)key[i * 4] |
                           ((uint32_t)key[i * 4 + 1] << 8) |
                           ((uint32_t)key[i * 4 + 2] << 16) |
                           ((uint32_t)key[i * 4 + 3] << 24);
            }

            // Block Counter
            s[12] = counter;

            // 96-bit Nonce (3 words, Little-Endian)
            for (int i = 0; i < 3; ++i) {
                s[13 + i] = (uint32_t)nonce[i * 4] |
                            ((uint32_t)nonce[i * 4 + 1] << 8) |
                            ((uint32_t)nonce[i * 4 + 2] << 16) |
                            ((uint32_t)nonce[i * 4 + 3] << 24);
            }

            uint32_t x[16];
            std::memcpy(x, s, sizeof(x));

            for (int r = 0; r < 10; ++r) {
                // Column round
                CHACHA_QR(x[0], x[4], x[8],  x[12]);
                CHACHA_QR(x[1], x[5], x[9],  x[13]);
                CHACHA_QR(x[2], x[6], x[10], x[14]);
                CHACHA_QR(x[3], x[7], x[11], x[15]);

                // Diagonal round
                CHACHA_QR(x[0], x[5], x[10], x[15]);
                CHACHA_QR(x[1], x[6], x[11], x[12]);
                CHACHA_QR(x[2], x[7], x[8],  x[13]);
                CHACHA_QR(x[3], x[4], x[9],  x[14]);
            }

            for (int i = 0; i < 16; ++i) {
                uint32_t val = x[i] + s[i];
                outBlock[i * 4]     = (uint8_t)(val);
                outBlock[i * 4 + 1] = (uint8_t)(val >> 8);
                outBlock[i * 4 + 2] = (uint8_t)(val >> 16);
                outBlock[i * 4 + 3] = (uint8_t)(val >> 24);
            }
        }

        void ChaCha20Process(const uint8_t key[32], const uint8_t nonce[12], uint32_t startCounter,
                             const uint8_t* in, uint8_t* out, size_t len) {
            uint8_t block[64];
            uint32_t counter = startCounter;
            size_t offset = 0;
            while (offset < len) {
                ChaCha20Block(key, nonce, counter++, block);
                size_t take = (len - offset < 64) ? (len - offset) : 64;
                for (size_t i = 0; i < take; ++i) {
                    out[offset + i] = in[offset + i] ^ block[i];
                }
                offset += take;
            }
        }

        // =========================================================================
        // RFC 8439 POLY1305 MAC IMPLEMENTATION (26-bit limbs, constant time)
        // =========================================================================

        struct Poly1305State {
            uint32_t r[5];
            uint32_t h[5];
            uint32_t pad[4];
            size_t leftover;
            uint8_t buffer[16];
        };

        inline uint32_t Load32LE(const uint8_t *p) {
            return ((uint32_t)p[0]) |
                   ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[3] << 24);
        }

        void Poly1305Init(Poly1305State *st, const uint8_t key[32]) {
            uint32_t t0 = Load32LE(key + 0);
            uint32_t t1 = Load32LE(key + 4);
            uint32_t t2 = Load32LE(key + 8);
            uint32_t t3 = Load32LE(key + 12);

            // r &= 0xffffffc0ffffffc0ffffffc0fffffff (RFC 8439 clamping)
            st->r[0] = (t0) & 0x3ffffff;
            st->r[1] = ((t0 >> 26) | (t1 << 6)) & 0x3ffff03;
            st->r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
            st->r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
            st->r[4] = (t3 >> 8) & 0x00fffff;

            st->h[0] = 0; st->h[1] = 0; st->h[2] = 0; st->h[3] = 0; st->h[4] = 0;

            st->pad[0] = Load32LE(key + 16);
            st->pad[1] = Load32LE(key + 20);
            st->pad[2] = Load32LE(key + 24);
            st->pad[3] = Load32LE(key + 28);

            st->leftover = 0;
        }

        void Poly1305Blocks(Poly1305State *st, const uint8_t *m, size_t bytes, uint32_t hibit) {
            uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
            uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
            uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

            while (bytes >= 16) {
                uint32_t t0 = Load32LE(m + 0);
                uint32_t t1 = Load32LE(m + 4);
                uint32_t t2 = Load32LE(m + 8);
                uint32_t t3 = Load32LE(m + 12);

                h0 += (t0) & 0x3ffffff;
                h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
                h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
                h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
                h4 += (t3 >> 8) | hibit;

                uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
                uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
                uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
                uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
                uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

                uint32_t c;
                c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
                c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
                c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
                c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
                c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
                c = (h0 >> 26);           h0 = h0 & 0x3ffffff;           h1 += c;

                m += 16;
                bytes -= 16;
            }

            st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
        }

        void Poly1305Update(Poly1305State *st, const uint8_t *m, size_t bytes) {
            if (st->leftover) {
                size_t want = 16 - st->leftover;
                if (want > bytes) want = bytes;
                for (size_t i = 0; i < want; ++i) st->buffer[st->leftover + i] = m[i];
                bytes -= want;
                m += want;
                st->leftover += want;
                if (st->leftover < 16) return;
                Poly1305Blocks(st, st->buffer, 16, 1 << 24);
                st->leftover = 0;
            }

            if (bytes >= 16) {
                size_t want = bytes & ~(size_t)15;
                Poly1305Blocks(st, m, want, 1 << 24);
                m += want;
                bytes -= want;
            }

            if (bytes) {
                for (size_t i = 0; i < bytes; ++i) st->buffer[st->leftover + i] = m[i];
                st->leftover += bytes;
            }
        }

        void Poly1305Finish(Poly1305State *st, uint8_t mac[16]) {
            if (st->leftover) {
                size_t i = st->leftover;
                st->buffer[i++] = 1;
                for (; i < 16; ++i) st->buffer[i] = 0;
                Poly1305Blocks(st, st->buffer, 16, 0);
            }

            uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
            uint32_t c;

            c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
            c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
            c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
            c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
            c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

            // Compute h + -p (p = 2^130 - 5)
            uint32_t g0 = h0 + 5;
            c = g0 >> 26; g0 &= 0x3ffffff;
            uint32_t g1 = h1 + c;
            c = g1 >> 26; g1 &= 0x3ffffff;
            uint32_t g2 = h2 + c;
            c = g2 >> 26; g2 &= 0x3ffffff;
            uint32_t g3 = h3 + c;
            c = g3 >> 26; g3 &= 0x3ffffff;
            uint32_t g4 = h4 + c - (1 << 26);

            // Constant-time selection if h >= p
            uint32_t mask = (g4 >> 31) - 1;
            g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
            mask = ~mask;
            h0 = (h0 & mask) | g0;
            h1 = (h1 & mask) | g1;
            h2 = (h2 & mask) | g2;
            h3 = (h3 & mask) | g3;
            h4 = (h4 & mask) | g4;

            // Reassemble 128-bit accumulator
            h0 = (h0)       | (h1 << 26);
            h1 = (h1 >> 6)  | (h2 << 20);
            h2 = (h2 >> 12) | (h3 << 14);
            h3 = (h3 >> 18) | (h4 << 8);

            // Add pad mod 2^128
            uint64_t f0 = (uint64_t)h0 + st->pad[0];
            uint64_t f1 = (uint64_t)h1 + st->pad[1] + (f0 >> 32);
            uint64_t f2 = (uint64_t)h2 + st->pad[2] + (f1 >> 32);
            uint64_t f3 = (uint64_t)h3 + st->pad[3] + (f2 >> 32);

            h0 = (uint32_t)f0;
            h1 = (uint32_t)f1;
            h2 = (uint32_t)f2;
            h3 = (uint32_t)f3;

            for (int i = 0; i < 4; ++i) {
                mac[i + 0]  = (uint8_t)(h0 >> (i * 8));
                mac[i + 4]  = (uint8_t)(h1 >> (i * 8));
                mac[i + 8]  = (uint8_t)(h2 >> (i * 8));
                mac[i + 12] = (uint8_t)(h3 >> (i * 8));
            }
        }

        // Computes RFC 8439 AEAD Tag over AAD and Ciphertext
        void ComputeAeadTag(const uint8_t polyKey[32],
                            const uint8_t* aad, size_t aadLen,
                            const uint8_t* ciphertext, size_t cipherLen,
                            uint8_t outTag[16]) {
            Poly1305State st;
            Poly1305Init(&st, polyKey);

            if (aad && aadLen > 0) {
                Poly1305Update(&st, aad, aadLen);
                if (aadLen % 16 != 0) {
                    uint8_t zeroPad[16] = {0};
                    Poly1305Update(&st, zeroPad, 16 - (aadLen % 16));
                }
            }

            if (ciphertext && cipherLen > 0) {
                Poly1305Update(&st, ciphertext, cipherLen);
                if (cipherLen % 16 != 0) {
                    uint8_t zeroPad[16] = {0};
                    Poly1305Update(&st, zeroPad, 16 - (cipherLen % 16));
                }
            }

            // Length block: 8 bytes AAD length (Little-Endian) + 8 bytes Ciphertext length (Little-Endian)
            uint8_t lenBuf[16] = {0};
            uint64_t aLen = (uint64_t)aadLen;
            uint64_t cLen = (uint64_t)cipherLen;
            for (int i = 0; i < 8; ++i) {
                lenBuf[i] = (uint8_t)(aLen >> (i * 8));
                lenBuf[8 + i] = (uint8_t)(cLen >> (i * 8));
            }
            Poly1305Update(&st, lenBuf, 16);

            Poly1305Finish(&st, outTag);
        }
    }

    // =========================================================================
    // AEAD ENCRYPT & DECRYPT INTERFACES
    // =========================================================================

    bool EncryptWithAd(const uint8_t* key, const uint8_t nonce[NONCE_SIZE],
                       const uint8_t* aad, size_t aadLen,
                       const uint8_t* plaintext, size_t plainLen,
                       std::vector<uint8_t>& outCipher) {
        if (!key || !nonce || (plaintext == nullptr && plainLen > 0)) {
            return false;
        }

        outCipher.resize(ENCRYPTION_OVERHEAD + plainLen);

        // 1. Prepend 12-byte Nonce/IV
        std::memcpy(outCipher.data(), nonce, NONCE_SIZE);

        // 2. Generate one-time Poly1305 key using ChaCha20 block 0
        uint8_t block0[64];
        ChaCha20Block(key, nonce, 0, block0);
        uint8_t polyKey[32];
        std::memcpy(polyKey, block0, 32);

        // 3. Encrypt plaintext starting at counter 1
        uint8_t* cipherPayload = outCipher.data() + NONCE_SIZE;
        if (plainLen > 0) {
            ChaCha20Process(key, nonce, 1, plaintext, cipherPayload, plainLen);
        }

        // 4. Compute Poly1305 authentication tag over AAD + ciphertext
        uint8_t* tagDest = outCipher.data() + NONCE_SIZE + plainLen;
        ComputeAeadTag(polyKey, aad, aadLen, cipherPayload, plainLen, tagDest);

        return true;
    }

    bool EncryptWithNonce(const uint8_t* key, const uint8_t nonce[NONCE_SIZE],
                          const uint8_t* plaintext, size_t plainLen,
                          std::vector<uint8_t>& outCipher) {
        return EncryptWithAd(key, nonce, nullptr, 0, plaintext, plainLen, outCipher);
    }

    bool Encrypt(const uint8_t* key, const uint8_t* plaintext, size_t plainLen, std::vector<uint8_t>& outCipher) {
        if (!key || (plaintext == nullptr && plainLen > 0)) {
            return false;
        }
        uint8_t nonce[NONCE_SIZE];
        GenerateRandomBytes(nonce, NONCE_SIZE);
        return EncryptWithNonce(key, nonce, plaintext, plainLen, outCipher);
    }

    bool DecryptWithAd(const uint8_t* key, const uint8_t* aad, size_t aadLen,
                       const uint8_t* ciphertext, size_t cipherLen, std::vector<uint8_t>& outPlain) {
        if (!key || !ciphertext || cipherLen < ENCRYPTION_OVERHEAD) {
            outPlain.clear();
            return false;
        }

        const uint8_t* nonce = ciphertext;
        size_t plainLen = cipherLen - ENCRYPTION_OVERHEAD;
        const uint8_t* cipherPayload = ciphertext + NONCE_SIZE;
        const uint8_t* receivedTag = ciphertext + NONCE_SIZE + plainLen;

        // 1. Generate one-time Poly1305 key using ChaCha20 block 0
        uint8_t block0[64];
        ChaCha20Block(key, nonce, 0, block0);
        uint8_t polyKey[32];
        std::memcpy(polyKey, block0, 32);

        // 2. Compute expected authentication tag
        uint8_t expectedTag[TAG_SIZE];
        ComputeAeadTag(polyKey, aad, aadLen, cipherPayload, plainLen, expectedTag);

        // 3. Verify tag in constant time
        if (!ConstantTimeCompare(expectedTag, receivedTag, TAG_SIZE)) {
            outPlain.clear();
            return false;
        }

        // 4. Decrypt payload into output vector
        outPlain.resize(plainLen);
        if (plainLen > 0) {
            ChaCha20Process(key, nonce, 1, cipherPayload, outPlain.data(), plainLen);
        }

        return true;
    }

    bool Decrypt(const uint8_t* key, const uint8_t* ciphertext, size_t cipherLen, std::vector<uint8_t>& outPlain) {
        return DecryptWithAd(key, nullptr, 0, ciphertext, cipherLen, outPlain);
    }

} // namespace Crypto
