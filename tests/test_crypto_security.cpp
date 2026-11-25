#include "../Shared/Crypto.h"
#include "../Shared/Protocol.h"
#include "../Shared/ControlEnvelope.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <chrono>

// Helper macro for assertions with descriptive output
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            exit(1); \
        } \
    } while(0)

void TestSha256() {
    std::cout << "Running TestSha256..." << std::endl;

    // FIPS 180-4 Test Vectors
    std::string hEmpty = Crypto::Sha256Hex("");
    TEST_ASSERT(hEmpty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                "SHA-256 empty string test vector mismatch");

    std::string hAbc = Crypto::Sha256Hex("abc");
    TEST_ASSERT(hAbc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "SHA-256 'abc' test vector mismatch");

    std::string hHello = Crypto::Sha256Hex("hello world");
    TEST_ASSERT(hHello == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9",
                "SHA-256 'hello world' test vector mismatch");

    std::cout << "  [PASS] SHA-256 test vectors match specification." << std::endl;
}

void TestConstantTimeCompare() {
    std::cout << "Running TestConstantTimeCompare..." << std::endl;

    uint8_t a[32], b[32];
    for (int i = 0; i < 32; ++i) { a[i] = (uint8_t)i; b[i] = (uint8_t)i; }

    TEST_ASSERT(Crypto::ConstantTimeCompare(a, b, 32), "Identical buffers must compare equal");

    // Single bit flip
    b[15] ^= 0x01;
    TEST_ASSERT(!Crypto::ConstantTimeCompare(a, b, 32), "Modified buffer must compare unequal");

    // First byte flip
    b[15] ^= 0x01;
    b[0] ^= 0x80;
    TEST_ASSERT(!Crypto::ConstantTimeCompare(a, b, 32), "Modified buffer must compare unequal");

    // Last byte flip
    b[0] ^= 0x80;
    b[31] ^= 0x01;
    TEST_ASSERT(!Crypto::ConstantTimeCompare(a, b, 32), "Modified buffer must compare unequal");

    std::cout << "  [PASS] ConstantTimeCompare correctly verifies equality and detects bit flips." << std::endl;
}

void TestKeyDerivation() {
    std::cout << "Running TestKeyDerivation..." << std::endl;

    uint8_t key1[32] = {0};
    uint8_t key2[32] = {0};
    uint8_t key3[32] = {0};

    Crypto::DeriveKey("Password123!", nullptr, 0, key1);
    Crypto::DeriveKey("Password123!", nullptr, 0, key2);
    Crypto::DeriveKey("DifferentPassword", nullptr, 0, key3);

    TEST_ASSERT(Crypto::ConstantTimeCompare(key1, key2, 32), "Deterministic key derivation failed");
    TEST_ASSERT(!Crypto::ConstantTimeCompare(key1, key3, 32), "Distinct passwords must yield distinct keys");

    // With salt
    uint8_t salt[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t keySalted[32] = {0};
    Crypto::DeriveKey("Password123!", salt, sizeof(salt), keySalted);
    TEST_ASSERT(!Crypto::ConstantTimeCompare(key1, keySalted, 32), "Salt must alter derived key");

    std::cout << "  [PASS] Key derivation is deterministic and sensitive to input & salt." << std::endl;
}

void TestRfc8439AeadTestVector() {
    std::cout << "Running TestRfc8439AeadTestVector..." << std::endl;

    // RFC 8439 Section 2.8.2 Test Vector
    const uint8_t key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };

    const uint8_t nonce[12] = {
        0x07, 0x00, 0x00, 0x00,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47
    };

    const uint8_t aad[12] = {
        0x50, 0x51, 0x52, 0x53,
        0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7
    };

    const char* plainTextStr = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    size_t plainLen = strlen(plainTextStr);

    std::vector<uint8_t> cipher;
    bool ok = Crypto::EncryptWithAd(key, nonce, aad, sizeof(aad), (const uint8_t*)plainTextStr, plainLen, cipher);
    TEST_ASSERT(ok, "EncryptWithAd failed for RFC 8439 test vector");
    TEST_ASSERT(cipher.size() == plainLen + Crypto::ENCRYPTION_OVERHEAD, "Ciphertext size mismatch");

    // Verify tag matches RFC 8439 Section 2.8.2 tag:
    // 1a:e1:0b:59:4f:09:e2:6a:7e:90:2e:cb:d0:60:06:91
    const uint8_t expectedTag[16] = {
        0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a,
        0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91
    };

    const uint8_t* producedTag = cipher.data() + Crypto::NONCE_SIZE + plainLen;
    TEST_ASSERT(Crypto::ConstantTimeCompare(producedTag, expectedTag, 16),
                "RFC 8439 Section 2.8.2 Poly1305 AEAD Tag mismatch");

    // Decrypt and verify round-trip
    std::vector<uint8_t> decrypted;
    ok = Crypto::DecryptWithAd(key, aad, sizeof(aad), cipher.data(), cipher.size(), decrypted);
    TEST_ASSERT(ok, "DecryptWithAd failed for RFC 8439 test vector");
    TEST_ASSERT(decrypted.size() == plainLen, "Decrypted length mismatch");
    TEST_ASSERT(memcmp(decrypted.data(), plainTextStr, plainLen) == 0, "Decrypted text mismatch");

    std::cout << "  [PASS] RFC 8439 Section 2.8.2 bit-exact match verified." << std::endl;
}

void TestAeadRoundtripAndTamper() {
    std::cout << "Running TestAeadRoundtripAndTamper..." << std::endl;

    uint8_t key[32];
    Crypto::GenerateRandomBytes(key, 32);

    // Test multiple payload sizes: 0, 1, 15, 16, 17, 1024, 65536
    const std::vector<size_t> testSizes = { 0, 1, 15, 16, 17, 256, 1024, 65536 };

    for (size_t sz : testSizes) {
        std::vector<uint8_t> plain(sz);
        if (sz > 0) {
            Crypto::GenerateRandomBytes(plain.data(), sz);
        }

        std::vector<uint8_t> cipher;
        bool ok = Crypto::Encrypt(key, plain.data(), sz, cipher);
        TEST_ASSERT(ok, "Encrypt failed");
        TEST_ASSERT(cipher.size() == sz + Crypto::ENCRYPTION_OVERHEAD, "Cipher size formula mismatch");

        std::vector<uint8_t> dec;
        ok = Crypto::Decrypt(key, cipher.data(), cipher.size(), dec);
        TEST_ASSERT(ok, "Decrypt failed");
        TEST_ASSERT(dec.size() == sz, "Decrypted size mismatch");
        if (sz > 0) {
            TEST_ASSERT(memcmp(plain.data(), dec.data(), sz) == 0, "Plaintext mismatch after roundtrip");
        }

        // --- TAMPER TESTS ---
        // 1. Bit flip in Nonce
        cipher[0] ^= 0x01;
        TEST_ASSERT(!Crypto::Decrypt(key, cipher.data(), cipher.size(), dec),
                    "Decryption must fail when Nonce is tampered");
        TEST_ASSERT(dec.empty(), "Output vector must be cleared on decryption failure");
        cipher[0] ^= 0x01; // Restore

        // 2. Bit flip in Tag
        cipher[cipher.size() - 1] ^= 0x80;
        TEST_ASSERT(!Crypto::Decrypt(key, cipher.data(), cipher.size(), dec),
                    "Decryption must fail when Tag is tampered");
        TEST_ASSERT(dec.empty(), "Output vector must be cleared on decryption failure");
        cipher[cipher.size() - 1] ^= 0x80; // Restore

        // 3. Bit flip in Ciphertext (if sz > 0)
        if (sz > 0) {
            cipher[Crypto::NONCE_SIZE] ^= 0x40;
            TEST_ASSERT(!Crypto::Decrypt(key, cipher.data(), cipher.size(), dec),
                        "Decryption must fail when Ciphertext payload is tampered");
            TEST_ASSERT(dec.empty(), "Output vector must be cleared on decryption failure");
            cipher[Crypto::NONCE_SIZE] ^= 0x40; // Restore
        }

        // 4. Truncation
        TEST_ASSERT(!Crypto::Decrypt(key, cipher.data(), Crypto::ENCRYPTION_OVERHEAD - 1, dec),
                    "Decryption must fail on truncated buffer (<28 bytes)");

        // 5. Wrong key
        uint8_t wrongKey[32];
        Crypto::GenerateRandomBytes(wrongKey, 32);
        TEST_ASSERT(!Crypto::Decrypt(wrongKey, cipher.data(), cipher.size(), dec),
                    "Decryption must fail with incorrect key");
    }

    std::cout << "  [PASS] AEAD roundtrip & tamper rejection passed across all tested payload sizes." << std::endl;
}

void TestNonceUniqueness() {
    std::cout << "Running TestNonceUniqueness..." << std::endl;
    uint8_t key[32];
    Crypto::GenerateRandomBytes(key, 32);

    const char* msg = "Test Nonce Uniqueness Payload";
    std::vector<uint8_t> c1, c2;
    bool ok1 = Crypto::Encrypt(key, (const uint8_t*)msg, strlen(msg), c1);
    bool ok2 = Crypto::Encrypt(key, (const uint8_t*)msg, strlen(msg), c2);
    TEST_ASSERT(ok1 && ok2, "Encryption failed in TestNonceUniqueness");

    // Nonce is the first 12 bytes of cipher
    TEST_ASSERT(memcmp(c1.data(), c2.data(), Crypto::NONCE_SIZE) != 0,
                "Consecutive encryptions must never reuse the same nonce");
    std::cout << "  [PASS] Nonce uniqueness verified (CSPRNG produces distinct nonces)." << std::endl;
}

void TestControlEnvelope() {
    std::cout << "Running TestControlEnvelope..." << std::endl;

    uint8_t sessionKey[32];
    Crypto::DeriveKey("482910", nullptr, 0, sessionKey);

    // 1. Zero-payload message: MSG_PING
    {
        std::vector<uint8_t> cipher;
        bool ok = ControlEnvelope::WrapPacket(sessionKey, Protocol::MSG_PING, nullptr, 0, cipher);
        TEST_ASSERT(ok, "WrapPacket failed for MSG_PING");
        TEST_ASSERT(cipher.size() == ControlEnvelope::MIN_ENCRYPTED_SIZE, "MSG_PING encrypted size must be 33 bytes");

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> innerPayload;
        ok = ControlEnvelope::UnwrapPacket(sessionKey, cipher.data(), cipher.size(), innerHdr, innerPayload);
        TEST_ASSERT(ok, "UnwrapPacket failed for MSG_PING");
        TEST_ASSERT(innerHdr.type == Protocol::MSG_PING, "Inner message type mismatch");
        TEST_ASSERT(innerHdr.size == 0, "Inner payload size must be 0");
        TEST_ASSERT(innerPayload.empty(), "Inner payload buffer must be empty");
    }

    // 2. Struct-payload message: MSG_SPECIAL_ACTION
    {
        Protocol::SpecialActionPayload actPayload;
        actPayload.actionType = Protocol::ACTION_CTRL_ALT_DEL;

        std::vector<uint8_t> cipher;
        bool ok = ControlEnvelope::WrapPacket(sessionKey, Protocol::MSG_SPECIAL_ACTION,
                                             (const char*)&actPayload, sizeof(actPayload), cipher);
        TEST_ASSERT(ok, "WrapPacket failed for MSG_SPECIAL_ACTION");
        TEST_ASSERT(cipher.size() == sizeof(Protocol::PacketHeader) + sizeof(actPayload) + Crypto::ENCRYPTION_OVERHEAD,
                    "MSG_SPECIAL_ACTION encrypted size mismatch");

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> innerPayload;
        ok = ControlEnvelope::UnwrapPacket(sessionKey, cipher.data(), cipher.size(), innerHdr, innerPayload);
        TEST_ASSERT(ok, "UnwrapPacket failed for MSG_SPECIAL_ACTION");
        TEST_ASSERT(innerHdr.type == Protocol::MSG_SPECIAL_ACTION, "Inner type mismatch");
        TEST_ASSERT(innerHdr.size == sizeof(actPayload), "Inner size mismatch");
        TEST_ASSERT(innerPayload.size() == sizeof(actPayload), "Payload buffer size mismatch");

        Protocol::SpecialActionPayload unpacked;
        memcpy(&unpacked, innerPayload.data(), sizeof(unpacked));
        TEST_ASSERT(unpacked.actionType == Protocol::ACTION_CTRL_ALT_DEL, "actionType mismatch");
    }

    // 3. String-payload message: MSG_CHAT_MESSAGE
    {
        Protocol::ChatMessagePayload chat = {};
        strncpy_s(chat.sender, "Manager", sizeof(chat.sender) - 1);
        strncpy_s(chat.message, "Hello from encrypted control channel!", sizeof(chat.message) - 1);
        chat.timestamp = 123456789;

        std::vector<uint8_t> cipher;
        bool ok = ControlEnvelope::WrapPacket(sessionKey, Protocol::MSG_CHAT_MESSAGE,
                                             (const char*)&chat, sizeof(chat), cipher);
        TEST_ASSERT(ok, "WrapPacket failed for MSG_CHAT_MESSAGE");

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> innerPayload;
        ok = ControlEnvelope::UnwrapPacket(sessionKey, cipher.data(), cipher.size(), innerHdr, innerPayload);
        TEST_ASSERT(ok, "UnwrapPacket failed for MSG_CHAT_MESSAGE");
        TEST_ASSERT(innerHdr.type == Protocol::MSG_CHAT_MESSAGE, "Inner type mismatch");

        Protocol::ChatMessagePayload receivedChat = {};
        memcpy(&receivedChat, innerPayload.data(), sizeof(receivedChat));
        TEST_ASSERT(strcmp(receivedChat.sender, "Manager") == 0, "Chat sender mismatch");
        TEST_ASSERT(strcmp(receivedChat.message, "Hello from encrypted control channel!") == 0, "Chat message mismatch");
    }

    // 4. Adversarial Envelope Tests
    {
        std::vector<uint8_t> cipher;
        ControlEnvelope::WrapPacket(sessionKey, Protocol::MSG_PING, nullptr, 0, cipher);

        // Truncated packet
        Protocol::PacketHeader h;
        std::vector<uint8_t> p;
        TEST_ASSERT(!ControlEnvelope::UnwrapPacket(sessionKey, cipher.data(), 32, h, p),
                    "Unwrap must fail on size < 33 bytes");

        // Tampered ciphertext
        cipher[15] ^= 0x01;
        TEST_ASSERT(!ControlEnvelope::UnwrapPacket(sessionKey, cipher.data(), cipher.size(), h, p),
                    "Unwrap must fail on tampered data");
        cipher[15] ^= 0x01;

        // Wrong session key
        uint8_t wrongKey[32] = {0};
        TEST_ASSERT(!ControlEnvelope::UnwrapPacket(wrongKey, cipher.data(), cipher.size(), h, p),
                    "Unwrap must fail with wrong session key");
    }

    std::cout << "  [PASS] ControlEnvelope Wrap & Unwrap verified for all message structures." << std::endl;
}

void TestPerformance() {
    std::cout << "Running TestPerformance..." << std::endl;

    uint8_t key[32];
    Crypto::GenerateRandomBytes(key, 32);

    size_t dataSize = 1024 * 1024; // 1 MB
    std::vector<uint8_t> bigBuffer(dataSize, 0x5a);

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> cipher;
    bool okEnc = Crypto::Encrypt(key, bigBuffer.data(), bigBuffer.size(), cipher);
    auto t1 = std::chrono::high_resolution_clock::now();

    std::vector<uint8_t> plain;
    bool okDec = Crypto::Decrypt(key, cipher.data(), cipher.size(), plain);
    auto t2 = std::chrono::high_resolution_clock::now();

    TEST_ASSERT(okEnc && okDec, "1MB encryption/decryption failed");
    TEST_ASSERT(plain.size() == dataSize, "1MB decrypted size mismatch");

    double encMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double decMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    double totalMs = encMs + decMs;
    double throughputMBps = (2.0 * dataSize / (1024.0 * 1024.0)) / (totalMs / 1000.0);

    std::cout << "  [PERF] 1 MB Encrypt: " << encMs << " ms, Decrypt: " << decMs
              << " ms (Throughput: " << throughputMBps << " MB/s)" << std::endl;
    TEST_ASSERT(throughputMBps > 50.0, "Throughput below expected threshold (50 MB/s)");
}

void TestHmacSha256AndCsprng() {
    std::cout << "Running TestHmacSha256AndCsprng (C-1 / C-2)..." << std::endl;

    // RFC 4231 Test Case 1
    {
        uint8_t key[20];
        std::memset(key, 0x0b, sizeof(key));
        const char* data = "Hi There";
        const uint8_t expected[32] = {
            0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
            0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
            0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
            0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
        };
        uint8_t out[32] = {0};
        Crypto::HmacSha256(key, sizeof(key), reinterpret_cast<const uint8_t*>(data), 8, out);
        TEST_ASSERT(std::memcmp(out, expected, 32) == 0, "RFC 4231 TC1 mismatch");
    }

    // RFC 4231 Test Case 2
    {
        const char* key = "Jefe";
        const char* data = "what do ya want for nothing?";
        const uint8_t expected[32] = {
            0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e,
            0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
            0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
            0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43
        };
        uint8_t out[32] = {0};
        Crypto::HmacSha256(reinterpret_cast<const uint8_t*>(key), 4,
                           reinterpret_cast<const uint8_t*>(data), std::strlen(data), out);
        TEST_ASSERT(std::memcmp(out, expected, 32) == 0, "RFC 4231 TC2 mismatch");
    }

    // CSPRNG uniqueness
    uint8_t r1[32] = {0}, r2[32] = {0};
    Crypto::GenerateRandomBytes(r1, sizeof(r1));
    Crypto::GenerateRandomBytes(r2, sizeof(r2));
    TEST_ASSERT(std::memcmp(r1, r2, sizeof(r1)) != 0, "CSPRNG produced identical 32-byte blocks");

    std::cout << "  [PASS] RFC 4231 HMAC-SHA256 vectors and OS CSPRNG verified." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " PBL4 Cryptographic Security Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestSha256();
    TestConstantTimeCompare();
    TestKeyDerivation();
    TestHmacSha256AndCsprng();
    TestRfc8439AeadTestVector();
    TestAeadRoundtripAndTamper();
    TestNonceUniqueness();
    TestControlEnvelope();
    TestPerformance();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL CRYPTO SECURITY TESTS PASSED (100%)" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
