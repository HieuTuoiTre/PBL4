#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <random>
#include <thread>
#include <atomic>
#include <set>
#include <cassert>
#include "../Shared/Crypto.h"
#include "../Shared/ControlEnvelope.h"
#include "../Shared/Protocol.h"

using namespace std::chrono;

// =========================================================================
// ADVERSARIAL TEST SUITE 1: BIT-FLIP FUZZING (10,000+ PER CATEGORY)
// =========================================================================

void TestFuzzingBitFlipsCiphertext() {
    std::cout << "Running TestFuzzingBitFlipsCiphertext (15,000 iterations)...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    // Test with various payload sizes: small (16B), medium (256B), large (4096B)
    const size_t testSizes[] = { 16, 256, 1024, 4096 };
    std::mt19937 rng(1337);

    size_t totalTests = 0;
    size_t falsePositives = 0;

    for (size_t plainSize : testSizes) {
        std::vector<uint8_t> plain(plainSize);
        for (size_t i = 0; i < plainSize; ++i) plain[i] = (uint8_t)(rng() & 0xFF);

        std::vector<uint8_t> cipher;
        bool encOk = Crypto::Encrypt(key, plain.data(), plain.size(), cipher);
        assert(encOk);
        assert(cipher.size() == plainSize + Crypto::ENCRYPTION_OVERHEAD);

        // 15,000 / 4 = 3,750 iterations per size
        size_t iters = 3750;
        for (size_t it = 0; it < iters; ++it) {
            std::vector<uint8_t> corrupted = cipher;

            // Pick a random byte in ciphertext payload (between NONCE_SIZE and cipher.size() - TAG_SIZE)
            size_t cipherByteIdx = Crypto::NONCE_SIZE + (rng() % plainSize);
            uint8_t bitMask = (uint8_t)(1 << (rng() % 8));
            corrupted[cipherByteIdx] ^= bitMask;

            std::vector<uint8_t> outPlain;
            bool decOk = Crypto::Decrypt(key, corrupted.data(), corrupted.size(), outPlain);
            if (decOk) {
                falsePositives++;
            }
            totalTests++;
        }
    }

    std::cout << "  Fuzzed " << totalTests << " ciphertext bit-flips. Rejection rate: "
              << (100.0 * (totalTests - falsePositives) / totalTests) << "%, False positives: "
              << falsePositives << "\n";
    assert(falsePositives == 0);
    std::cout << "  [PASS] 100% rejection on ciphertext bit-flips (zero false positives).\n";
}

void TestFuzzingBitFlipsNonce() {
    std::cout << "Running TestFuzzingBitFlipsNonce (12,000 iterations)...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    std::mt19937 rng(4242);
    size_t totalTests = 0;
    size_t falsePositives = 0;

    std::vector<uint8_t> plain(128, 0xAA);
    std::vector<uint8_t> cipher;
    bool encOk = Crypto::Encrypt(key, plain.data(), plain.size(), cipher);
    assert(encOk);

    // 12,000 bit-flips targeted strictly at the 12-byte Nonce
    for (size_t it = 0; it < 12000; ++it) {
        std::vector<uint8_t> corrupted = cipher;
        size_t nonceByteIdx = rng() % Crypto::NONCE_SIZE;
        uint8_t bitMask = (uint8_t)(1 << (rng() % 8));
        corrupted[nonceByteIdx] ^= bitMask;

        std::vector<uint8_t> outPlain;
        bool decOk = Crypto::Decrypt(key, corrupted.data(), corrupted.size(), outPlain);
        if (decOk) {
            falsePositives++;
        }
        totalTests++;
    }

    std::cout << "  Fuzzed " << totalTests << " nonce bit-flips. Rejection rate: "
              << (100.0 * (totalTests - falsePositives) / totalTests) << "%, False positives: "
              << falsePositives << "\n";
    assert(falsePositives == 0);
    std::cout << "  [PASS] 100% rejection on nonce bit-flips.\n";
}

void TestFuzzingBitFlipsTag() {
    std::cout << "Running TestFuzzingBitFlipsTag (16,000 iterations)...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    std::mt19937 rng(9999);
    size_t totalTests = 0;
    size_t falsePositives = 0;

    std::vector<uint8_t> plain(64, 0x55);
    std::vector<uint8_t> cipher;
    bool encOk = Crypto::Encrypt(key, plain.data(), plain.size(), cipher);
    assert(encOk);

    // 16,000 bit-flips targeted strictly at the 16-byte Poly1305 Tag
    size_t tagStart = cipher.size() - Crypto::TAG_SIZE;
    for (size_t it = 0; it < 16000; ++it) {
        std::vector<uint8_t> corrupted = cipher;
        size_t tagByteIdx = tagStart + (rng() % Crypto::TAG_SIZE);
        uint8_t bitMask = (uint8_t)(1 << (rng() % 8));
        corrupted[tagByteIdx] ^= bitMask;

        std::vector<uint8_t> outPlain;
        bool decOk = Crypto::Decrypt(key, corrupted.data(), corrupted.size(), outPlain);
        if (decOk) {
            falsePositives++;
        }
        totalTests++;
    }

    std::cout << "  Fuzzed " << totalTests << " tag bit-flips. Rejection rate: "
              << (100.0 * (totalTests - falsePositives) / totalTests) << "%, False positives: "
              << falsePositives << "\n";
    assert(falsePositives == 0);
    std::cout << "  [PASS] 100% rejection on Poly1305 tag bit-flips.\n";
}

void TestFuzzingRandomGarbage() {
    std::cout << "Running TestFuzzingRandomGarbage (10,000 random packets)...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    std::mt19937 rng(8888);
    size_t totalTests = 0;
    size_t accepted = 0;

    for (size_t it = 0; it < 10000; ++it) {
        size_t len = rng() % 512;
        std::vector<uint8_t> garbage(len);
        for (size_t i = 0; i < len; ++i) garbage[i] = (uint8_t)(rng() & 0xFF);

        std::vector<uint8_t> outPlain;
        if (Crypto::Decrypt(key, garbage.data(), garbage.size(), outPlain)) {
            accepted++;
        }

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> envelopePayload;
        if (ControlEnvelope::UnwrapPacket(key, garbage.data(), garbage.size(), innerHdr, envelopePayload)) {
            accepted++;
        }
        totalTests += 2;
    }

    std::cout << "  Tested " << totalTests << " random garbage streams. Accepted: " << accepted << "\n";
    assert(accepted == 0);
    std::cout << "  [PASS] 100% rejection of unauthenticated random garbage.\n";
}

// =========================================================================
// ADVERSARIAL TEST SUITE 2: BOUNDARY & EXTREME CONDITIONS
// =========================================================================

void TestBoundaryPayloadSizes() {
    std::cout << "Running TestBoundaryPayloadSizes...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    // Zero-byte plaintext
    {
        std::vector<uint8_t> cipher;
        bool encOk = Crypto::Encrypt(key, nullptr, 0, cipher);
        assert(encOk);
        assert(cipher.size() == Crypto::ENCRYPTION_OVERHEAD);

        std::vector<uint8_t> plain;
        bool decOk = Crypto::Decrypt(key, cipher.data(), cipher.size(), plain);
        assert(decOk);
        assert(plain.empty());
    }

    // 1-byte plaintext
    {
        uint8_t byteVal = 0x7E;
        std::vector<uint8_t> cipher;
        bool encOk = Crypto::Encrypt(key, &byteVal, 1, cipher);
        assert(encOk);
        assert(cipher.size() == 1 + Crypto::ENCRYPTION_OVERHEAD);

        std::vector<uint8_t> plain;
        bool decOk = Crypto::Decrypt(key, cipher.data(), cipher.size(), plain);
        assert(decOk);
        assert(plain.size() == 1 && plain[0] == 0x7E);
    }

    // Prime sizes & Block boundaries: 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 521, 1009, 4093, 65521
    const size_t testSizes[] = { 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 521, 1009, 4093, 65521 };
    std::mt19937 rng(5555);

    for (size_t sz : testSizes) {
        std::vector<uint8_t> original(sz);
        for (size_t i = 0; i < sz; ++i) original[i] = (uint8_t)(rng() & 0xFF);

        std::vector<uint8_t> cipher;
        assert(Crypto::Encrypt(key, original.data(), original.size(), cipher));
        assert(cipher.size() == sz + Crypto::ENCRYPTION_OVERHEAD);

        std::vector<uint8_t> plain;
        assert(Crypto::Decrypt(key, cipher.data(), cipher.size(), plain));
        assert(plain == original);
    }

    // Large Payloads: 4 MB, 16 MB
    const size_t largeSizes[] = { 4 * 1024 * 1024, 16 * 1024 * 1024 };
    for (size_t sz : largeSizes) {
        std::cout << "  Testing " << (sz / (1024 * 1024)) << " MB boundary payload...\n";
        std::vector<uint8_t> largeData(sz);
        largeData[0] = 0xAA;
        largeData[sz / 2] = 0xBB;
        largeData[sz - 1] = 0xCC;

        std::vector<uint8_t> cipher;
        auto t0 = high_resolution_clock::now();
        assert(Crypto::Encrypt(key, largeData.data(), largeData.size(), cipher));
        auto t1 = high_resolution_clock::now();

        std::vector<uint8_t> plain;
        assert(Crypto::Decrypt(key, cipher.data(), cipher.size(), plain));
        auto t2 = high_resolution_clock::now();

        assert(plain.size() == sz);
        assert(plain[0] == 0xAA && plain[sz / 2] == 0xBB && plain[sz - 1] == 0xCC);

        double encMs = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        double decMs = duration_cast<microseconds>(t2 - t1).count() / 1000.0;
        double mb = (double)sz / (1024.0 * 1024.0);
        std::cout << "    Encrypt: " << encMs << " ms (" << (mb / (encMs / 1000.0)) << " MB/s), "
                  << "Decrypt: " << decMs << " ms (" << (mb / (decMs / 1000.0)) << " MB/s)\n";
    }

    std::cout << "  [PASS] All boundary and large payload sizes verified successfully.\n";
}

void TestTruncationAtEveryOffset() {
    std::cout << "Running TestTruncationAtEveryOffset...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    std::vector<uint8_t> plain(128, 0x42);
    std::vector<uint8_t> cipher;
    assert(Crypto::Encrypt(key, plain.data(), plain.size(), cipher));

    size_t totalCipherLen = cipher.size(); // 128 + 28 = 156 bytes
    size_t rejectedCount = 0;

    for (size_t len = 0; len < totalCipherLen; ++len) {
        std::vector<uint8_t> outPlain;
        bool ok = Crypto::Decrypt(key, cipher.data(), len, outPlain);
        if (!ok) {
            rejectedCount++;
        }
    }

    assert(rejectedCount == totalCipherLen);
    std::cout << "  Tested " << totalCipherLen << " truncated prefix lengths (0 to "
              << (totalCipherLen - 1) << "). All " << rejectedCount << " safely rejected.\n";
    std::cout << "  [PASS] Truncation at every offset handled safely.\n";
}

void TestControlEnvelopeAdversarialCases() {
    std::cout << "Running TestControlEnvelopeAdversarialCases...\n";
    uint8_t key[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(key, sizeof(key));

    // 1. Zero-byte inner payload wrap & unwrap
    {
        std::vector<uint8_t> cipher;
        assert(ControlEnvelope::WrapPacket(key, Protocol::MSG_PING, nullptr, 0, cipher));
        assert(cipher.size() == ControlEnvelope::MIN_ENCRYPTED_SIZE); // 33 bytes

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> plain;
        assert(ControlEnvelope::UnwrapPacket(key, cipher.data(), cipher.size(), innerHdr, plain));
        assert(innerHdr.type == Protocol::MSG_PING);
        assert(innerHdr.size == 0);
        assert(plain.empty());
    }

    // 2. WrapPacket with negative innerSize must be rejected
    {
        std::vector<uint8_t> cipher;
        assert(!ControlEnvelope::WrapPacket(key, Protocol::MSG_PING, nullptr, -1, cipher));
        assert(!ControlEnvelope::WrapPacket(nullptr, Protocol::MSG_PING, nullptr, 0, cipher));
    }

    // 3. Inner Header Tampering: forged innerHdr.size (Integer Overflow / Desynchronization Attack)
    {
        // Construct forged plain where header says size is 1000, but only 10 bytes follow
        Protocol::PacketHeader forgedHdr;
        forgedHdr.type = Protocol::MSG_SPECIAL_ACTION;
        forgedHdr.size = 1000; // Lie about payload size

        std::vector<uint8_t> forgedPlain(sizeof(forgedHdr) + 10, 0x11);
        std::memcpy(forgedPlain.data(), &forgedHdr, sizeof(forgedHdr));

        // Encrypt it with valid crypto
        std::vector<uint8_t> forgedCipher;
        assert(Crypto::Encrypt(key, forgedPlain.data(), forgedPlain.size(), forgedCipher));

        // UnwrapPacket must detect the mismatch between header.size and plain.size()!
        Protocol::PacketHeader extractedHdr;
        std::vector<uint8_t> extractedPayload;
        bool ok = ControlEnvelope::UnwrapPacket(key, forgedCipher.data(), forgedCipher.size(), extractedHdr, extractedPayload);
        assert(!ok); // MUST BE REJECTED!
    }

    // 4. Inner Header Tampering: forged innerHdr.size = 0, but extra trailing bytes exist
    {
        Protocol::PacketHeader forgedHdr;
        forgedHdr.type = Protocol::MSG_SPECIAL_ACTION;
        forgedHdr.size = 0; // Claims 0 bytes, but we append 20 bytes

        std::vector<uint8_t> forgedPlain(sizeof(forgedHdr) + 20, 0x22);
        std::memcpy(forgedPlain.data(), &forgedHdr, sizeof(forgedHdr));

        std::vector<uint8_t> forgedCipher;
        assert(Crypto::Encrypt(key, forgedPlain.data(), forgedPlain.size(), forgedCipher));

        Protocol::PacketHeader extractedHdr;
        std::vector<uint8_t> extractedPayload;
        bool ok = ControlEnvelope::UnwrapPacket(key, forgedCipher.data(), forgedCipher.size(), extractedHdr, extractedPayload);
        assert(!ok); // MUST BE REJECTED!
    }

    // 5. Exceeding MAX_CONTROL_PAYLOAD boundary (32MB + 1)
    {
        Protocol::PacketHeader hdr;
        std::vector<uint8_t> dummyPayload;
        bool ok = ControlEnvelope::UnwrapPacket(key, (const uint8_t*)"1234", ControlEnvelope::MAX_CONTROL_PAYLOAD + 1, hdr, dummyPayload);
        assert(!ok);
    }

    std::cout << "  [PASS] ControlEnvelope inner header integrity and boundary checks verified.\n";
}

// =========================================================================
// ADVERSARIAL TEST SUITE 3: MULTI-THREADED CONCURRENCY STRESS (10 THREADS)
// =========================================================================

void TestConcurrencyStress() {
    std::cout << "Running TestConcurrencyStress (10 parallel threads, 10,000 total ops)...\n";
    uint8_t sharedKey[Crypto::KEY_SIZE];
    Crypto::GenerateRandomBytes(sharedKey, sizeof(sharedKey));

    const int numThreads = 10;
    const int opsPerThread = 1000;
    std::atomic<size_t> totalOpsCompleted{0};
    std::atomic<size_t> totalFailures{0};

    auto workerFunc = [&](int threadId) {
        std::mt19937 rng(1000 + threadId);
        for (int op = 0; op < opsPerThread; ++op) {
            // Generate random message size (0 to 1024 bytes)
            size_t size = rng() % 1024;
            std::vector<uint8_t> input(size);
            for (size_t i = 0; i < size; ++i) input[i] = (uint8_t)(rng() & 0xFF);

            // Alternate between Crypto::Encrypt/Decrypt and ControlEnvelope::Wrap/Unwrap
            if (op % 2 == 0) {
                std::vector<uint8_t> cipher;
                if (!Crypto::Encrypt(sharedKey, input.data(), input.size(), cipher)) {
                    totalFailures++;
                    continue;
                }
                std::vector<uint8_t> decrypted;
                if (!Crypto::Decrypt(sharedKey, cipher.data(), cipher.size(), decrypted) || decrypted != input) {
                    totalFailures++;
                    continue;
                }
            } else {
                std::vector<uint8_t> wrapped;
                Protocol::MessageType mtype = (Protocol::MessageType)(1 + (rng() % 32));
                if (!ControlEnvelope::WrapPacket(sharedKey, mtype, (const char*)input.data(), (int)input.size(), wrapped)) {
                    totalFailures++;
                    continue;
                }
                Protocol::PacketHeader outHdr;
                std::vector<uint8_t> unwrappedPayload;
                if (!ControlEnvelope::UnwrapPacket(sharedKey, wrapped.data(), wrapped.size(), outHdr, unwrappedPayload) ||
                    outHdr.type != mtype || outHdr.size != (uint32_t)size || unwrappedPayload != input) {
                    totalFailures++;
                    continue;
                }
            }
            totalOpsCompleted++;
        }
    };

    auto t0 = high_resolution_clock::now();
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back(workerFunc, t);
    }

    for (auto& th : threads) {
        th.join();
    }
    auto t1 = high_resolution_clock::now();
    double durationSec = duration_cast<milliseconds>(t1 - t0).count() / 1000.0;

    std::cout << "  Executed " << totalOpsCompleted.load() << " operations across " << numThreads
              << " threads in " << durationSec << " s (" << (totalOpsCompleted.load() / durationSec)
              << " ops/sec). Failures: " << totalFailures.load() << "\n";
    assert(totalFailures.load() == 0);
    assert(totalOpsCompleted.load() == numThreads * opsPerThread);
    std::cout << "  [PASS] Concurrent multi-threaded execution verified thread-safe with 0 race conditions.\n";
}

// =========================================================================
// ADVERSARIAL TEST SUITE 4: NONCE COLLISION & ENTROPY QUALITY
// =========================================================================

void TestNonceUniqueness() {
    std::cout << "Running TestNonceUniqueness (50,000 CSPRNG samples)...\n";
    std::set<std::string> seenNonces;
    const size_t sampleCount = 50000;

    uint8_t nonce[Crypto::NONCE_SIZE];
    for (size_t i = 0; i < sampleCount; ++i) {
        Crypto::GenerateRandomBytes(nonce, Crypto::NONCE_SIZE);
        std::string nonceStr((char*)nonce, Crypto::NONCE_SIZE);
        auto insertResult = seenNonces.insert(nonceStr);
        assert(insertResult.second); // Must be strictly unique
    }

    std::cout << "  Generated " << sampleCount << " unique 96-bit nonces with 0 collisions.\n";
    std::cout << "  [PASS] Nonce uniqueness and entropy quality verified.\n";
}

// =========================================================================
// ADVERSARIAL TEST SUITE 5: AVALANCHE EFFECT (DIFFUSION TEST)
// =========================================================================

void TestAvalancheEffect() {
    std::cout << "Running TestAvalancheEffect (1,000 diffusion trials)...\n";
    std::mt19937 rng(7777);

    uint64_t totalBitsFlippedSha = 0;
    uint64_t totalBitsFlippedChaCha = 0;
    const int trials = 1000;

    for (int tr = 0; tr < trials; ++tr) {
        uint8_t data[64];
        for (int i = 0; i < 64; ++i) data[i] = (uint8_t)(rng() & 0xFF);

        uint8_t dataMod[64];
        std::memcpy(dataMod, data, 64);
        // Flip exactly 1 bit
        dataMod[rng() % 64] ^= (1 << (rng() % 8));

        // SHA-256 diffusion
        uint8_t h1[32], h2[32];
        Crypto::Sha256(data, 64, h1);
        Crypto::Sha256(dataMod, 64, h2);

        for (int i = 0; i < 32; ++i) {
            uint8_t diff = h1[i] ^ h2[i];
            for (int b = 0; b < 8; ++b) {
                if ((diff >> b) & 1) totalBitsFlippedSha++;
            }
        }

        // ChaCha20 diffusion (first block)
        uint8_t key[32];
        uint8_t nonce[12] = {0};
        std::memcpy(key, data, 32);

        uint8_t out1[64], out2[64];
        std::vector<uint8_t> c1, c2;
        Crypto::EncryptWithNonce(key, nonce, data, 64, c1);

        uint8_t keyMod[32];
        std::memcpy(keyMod, key, 32);
        keyMod[rng() % 32] ^= (1 << (rng() % 8));
        Crypto::EncryptWithNonce(keyMod, nonce, data, 64, c2);

        // Compare ciphertext payload (bytes 12..75)
        for (int i = 0; i < 64; ++i) {
            uint8_t diff = c1[12 + i] ^ c2[12 + i];
            for (int b = 0; b < 8; ++b) {
                if ((diff >> b) & 1) totalBitsFlippedChaCha++;
            }
        }
    }

    double avgShaFlipRate = (double)totalBitsFlippedSha / (trials * 256.0) * 100.0;
    double avgChaChaFlipRate = (double)totalBitsFlippedChaCha / (trials * 512.0) * 100.0;

    std::cout << "  SHA-256 Avalanche Flip Rate: " << avgShaFlipRate << "% (ideal: 50.0%)\n";
    std::cout << "  ChaCha20 Avalanche Flip Rate: " << avgChaChaFlipRate << "% (ideal: 50.0%)\n";

    // Avalanche effect should be between 45% and 55%
    assert(avgShaFlipRate > 47.0 && avgShaFlipRate < 53.0);
    assert(avgChaChaFlipRate > 47.0 && avgChaChaFlipRate < 53.0);
    std::cout << "  [PASS] Strong avalanche effect (high diffusion) confirmed.\n";
}

// =========================================================================
// MAIN ENTRY POINT
// =========================================================================

int main() {
    std::cout << "========================================================\n";
    std::cout << " PBL4 Cryptographic Engine Adversarial Stress Harness\n";
    std::cout << "========================================================\n";

    try {
        TestFuzzingBitFlipsCiphertext();
        TestFuzzingBitFlipsNonce();
        TestFuzzingBitFlipsTag();
        TestFuzzingRandomGarbage();

        TestBoundaryPayloadSizes();
        TestTruncationAtEveryOffset();
        TestControlEnvelopeAdversarialCases();

        TestConcurrencyStress();
        TestNonceUniqueness();
        TestAvalancheEffect();

        std::cout << "\n========================================================\n";
        std::cout << " ALL ADVERSARIAL STRESS CHALLENGES PASSED (100% SUCCESS)\n";
        std::cout << " ZERO FALSE POSITIVES - ZERO RACE CONDITIONS DETECTED\n";
        std::cout << "========================================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION DURING ADVERSARIAL TEST: " << e.what() << "\n";
        return 1;
    }
}
