#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace Crypto {

    // ChaCha20 256-bit key size in bytes
    constexpr size_t KEY_SIZE = 32;

    // RFC 8439 96-bit Nonce / IV size in bytes
    constexpr size_t NONCE_SIZE = 12;

    // RFC 8439 Poly1305 128-bit Authentication Tag size in bytes
    constexpr size_t TAG_SIZE = 16;

    // Total overhead added by Encrypt: 12-byte Nonce + 16-byte Tag = 28 bytes
    constexpr size_t ENCRYPTION_OVERHEAD = NONCE_SIZE + TAG_SIZE;

    // SHA-256 digest size in bytes
    constexpr size_t SHA256_DIGEST_SIZE = 32;

    /**
     * @brief Generates cryptographically secure pseudo-random bytes.
     * @param out Destination buffer of at least `len` bytes.
     * @param len Number of random bytes to generate.
     */
    void GenerateRandomBytes(uint8_t* out, size_t len);

    /**
     * @brief Performs a constant-time comparison of two byte buffers to prevent timing side-channel attacks.
     * @param a Pointer to first buffer.
     * @param b Pointer to second buffer.
     * @param len Number of bytes to compare.
     * @return true if buffers are byte-for-byte identical, false otherwise.
     */
    bool ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len);

    /**
     * @brief Computes FIPS 180-4 SHA-256 hash of arbitrary binary data.
     * @param data Pointer to input data buffer.
     * @param len Length of input data buffer in bytes.
     * @param outHash Output 32-byte hash buffer.
     */
    void Sha256(const uint8_t* data, size_t len, uint8_t outHash[SHA256_DIGEST_SIZE]);

    /**
     * @brief Computes SHA-256 hash of a string and formats output as 64 lowercase hexadecimal characters.
     * @param input Input text string.
     * @return 64-character lowercase hex string.
     */
    std::string Sha256Hex(const std::string& input);

    /**
     * @brief Computes SHA-256 hash of a binary buffer and formats output as 64 lowercase hexadecimal characters.
     * @param data Pointer to input data.
     * @param len Length of data in bytes.
     * @return 64-character lowercase hex string.
     */
    std::string Sha256Hex(const uint8_t* data, size_t len);

    /**
     * @brief Derives a 32-byte cryptographic key from a password or session token and optional salt.
     * @param passwordOrToken User password or pre-shared key token string.
     * @param salt Optional salt buffer (can be nullptr if saltLen == 0).
     * @param saltLen Length of salt buffer in bytes.
     * @param outKey Output 32-byte buffer for the derived symmetric key.
     */
    void DeriveKey(const std::string& passwordOrToken, const uint8_t* salt, size_t saltLen, uint8_t outKey[KEY_SIZE]);

    /**
     * @brief Encrypts plaintext buffer using ChaCha20-Poly1305 AEAD (RFC 8439) with a generated 12-byte random Nonce/IV.
     *
     * Wire format of `outCipher`:
     *   [12-byte Nonce/IV] + [Ciphertext (plainLen bytes)] + [16-byte Poly1305 Tag]
     *   Total size = plainLen + 28 bytes.
     *
     * @param key 32-byte symmetric key (ChaCha20 key).
     * @param plaintext Pointer to plaintext data (can be nullptr if plainLen == 0).
     * @param plainLen Length of plaintext in bytes.
     * @param outCipher Vector populated with [12-byte Nonce] + [Ciphertext] + [16-byte Tag].
     * @return true on success, false if key is null or invalid.
     */
    bool Encrypt(const uint8_t* key, const uint8_t* plaintext, size_t plainLen, std::vector<uint8_t>& outCipher);

    /**
     * @brief Deterministic encryption with a caller-supplied 12-byte Nonce (primarily for test vectors).
     */
    bool EncryptWithNonce(const uint8_t* key, const uint8_t nonce[NONCE_SIZE],
                          const uint8_t* plaintext, size_t plainLen,
                          std::vector<uint8_t>& outCipher);

    /**
     * @brief RFC 8439 AEAD Encryption with Additional Authenticated Data (AAD).
     */
    bool EncryptWithAd(const uint8_t* key, const uint8_t nonce[NONCE_SIZE],
                       const uint8_t* aad, size_t aadLen,
                       const uint8_t* plaintext, size_t plainLen,
                       std::vector<uint8_t>& outCipher);

    /**
     * @brief Decrypts an authenticated ciphertext buffer using ChaCha20-Poly1305 AEAD (RFC 8439).
     *
     * Verifies the 16-byte Poly1305 authentication tag BEFORE decrypting the payload.
     * If the tag does not match or if the data has been corrupted/tampered with,
     * decryption fails immediately, `outPlain` is cleared, and false is returned.
     *
     * Expected wire format:
     *   [12-byte Nonce/IV] + [Ciphertext] + [16-byte Tag]
     *   Minimum cipherLen is 28 bytes.
     *
     * @param key 32-byte symmetric key.
     * @param ciphertext Pointer to encrypted buffer (starts with 12-byte Nonce, ends with 16-byte Tag).
     * @param cipherLen Total size of encrypted buffer (must be >= 28 bytes).
     * @param outPlain Vector populated with decrypted plaintext on success, cleared on failure.
     * @return true on successful authentication and decryption, false if verification fails.
     */
    bool Decrypt(const uint8_t* key, const uint8_t* ciphertext, size_t cipherLen, std::vector<uint8_t>& outPlain);

    /**
     * @brief RFC 8439 AEAD Decryption with Additional Authenticated Data (AAD).
     */
    bool DecryptWithAd(const uint8_t* key, const uint8_t* aad, size_t aadLen,
                       const uint8_t* ciphertext, size_t cipherLen, std::vector<uint8_t>& outPlain);

}
