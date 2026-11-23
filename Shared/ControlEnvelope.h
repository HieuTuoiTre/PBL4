#pragma once

#include "Protocol.h"
#include "Crypto.h"
#include <vector>
#include <cstdint>
#include <cstring>

namespace ControlEnvelope {

    constexpr size_t MIN_ENCRYPTED_SIZE = Crypto::NONCE_SIZE + sizeof(Protocol::PacketHeader) + Crypto::TAG_SIZE; // 12 + 5 + 16 = 33 bytes
    constexpr size_t MAX_CONTROL_PAYLOAD = 32 * 1024 * 1024; // 32 MB

    // Encapsulate an inner control message into a MSG_ENCRYPTED_CONTROL packet
    inline bool WrapPacket(const uint8_t sessionKey[32], Protocol::MessageType innerType,
                           const char* innerPayload, int innerSize,
                           std::vector<uint8_t>& outCipher) {
        if (!sessionKey || innerSize < 0) return false;

        // 1. Pack Plaintext: [inner PacketHeader (5B)] + [innerPayload]
        size_t plainSize = sizeof(Protocol::PacketHeader) + innerSize;
        std::vector<uint8_t> plain(plainSize);

        Protocol::PacketHeader innerHdr;
        innerHdr.type = innerType;
        innerHdr.size = (uint32_t)innerSize;
        std::memcpy(plain.data(), &innerHdr, sizeof(innerHdr));

        if (innerSize > 0 && innerPayload != nullptr) {
            std::memcpy(plain.data() + sizeof(innerHdr), innerPayload, innerSize);
        }

        // 2. Encrypt: [12B Nonce] + [Ciphertext] + [16B Poly1305 Tag]
        return Crypto::Encrypt(sessionKey, plain.data(), plain.size(), outCipher);
    }

    // Unwrap a received MSG_ENCRYPTED_CONTROL payload into inner header and payload
    inline bool UnwrapPacket(const uint8_t sessionKey[32], const uint8_t* cipherData, size_t cipherSize,
                             Protocol::PacketHeader& outInnerHdr, std::vector<uint8_t>& outPlainPayload) {
        if (!sessionKey || !cipherData || cipherSize < MIN_ENCRYPTED_SIZE || cipherSize > MAX_CONTROL_PAYLOAD) {
            return false;
        }

        std::vector<uint8_t> plain;
        if (!Crypto::Decrypt(sessionKey, cipherData, cipherSize, plain)) {
            return false; // Authentication tag mismatch or corrupted data
        }

        if (plain.size() < sizeof(Protocol::PacketHeader)) {
            return false;
        }

        std::memcpy(&outInnerHdr, plain.data(), sizeof(Protocol::PacketHeader));
        if (sizeof(Protocol::PacketHeader) + outInnerHdr.size != plain.size()) {
            return false; // Payload size tampering or truncation
        }

        if (outInnerHdr.size > 0) {
            outPlainPayload.assign(plain.begin() + sizeof(Protocol::PacketHeader), plain.end());
        } else {
            outPlainPayload.clear();
        }

        return true;
    }

    // Template helper to send an encrypted control packet over any socket (TcpClient or TcpServer)
    template <typename SocketT>
    inline bool SendEncryptedPacket(SocketT* sock, const uint8_t sessionKey[32],
                                   Protocol::MessageType innerType, const char* innerPayload, int innerSize) {
        if (!sock || !sessionKey) return false;

        std::vector<uint8_t> cipher;
        if (!WrapPacket(sessionKey, innerType, innerPayload, innerSize, cipher)) {
            return false;
        }

        return sock->SendPacket(Protocol::MSG_ENCRYPTED_CONTROL, (const char*)cipher.data(), (int)cipher.size());
    }
}
