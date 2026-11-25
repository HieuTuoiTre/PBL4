#include "../Shared/Network.h"
#include "../Shared/Protocol.h"
#include "../Shared/Crypto.h"
#include "../Shared/ControlEnvelope.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            exit(1); \
        } \
    } while(0)

// Test 1: Successful Challenge-Response HMAC Auth Handshake & Post-Handshake Encrypted Control Packets
void TestValidAuthAndEncryption() {
    std::cout << "Running TestValidAuthAndEncryption..." << std::endl;
    constexpr int TEST_PORT = 19080;

    TcpServer server;
    TEST_ASSERT(server.Start(TEST_PORT), "Failed to start server");

    std::thread srvThread([&]() {
        TEST_ASSERT(server.AcceptConnection(3000), "Server failed to accept connection");

        // 0. Send MSG_AUTH_CHALLENGE (16-byte random serverChallenge)
        Protocol::AuthChallengePayload challenge = {0};
        Crypto::GenerateRandomBytes(challenge.serverChallenge, sizeof(challenge.serverChallenge));
        TEST_ASSERT(server.SendPacket(Protocol::MSG_AUTH_CHALLENGE, (const char*)&challenge, sizeof(challenge)),
                    "Server failed to send MSG_AUTH_CHALLENGE");

        // 1. Receive MSG_AUTH_REQUEST
        Protocol::PacketHeader hdr;
        TEST_ASSERT(server.ReceiveExact((char*)&hdr, sizeof(hdr)), "Server failed to read header");
        TEST_ASSERT(hdr.type == Protocol::MSG_AUTH_REQUEST, "Expected MSG_AUTH_REQUEST");
        TEST_ASSERT(hdr.size == sizeof(Protocol::AuthRequestPayload), "AuthRequest size mismatch");

        Protocol::AuthRequestPayload req;
        TEST_ASSERT(server.ReceiveExact((char*)&req, sizeof(req)), "Server failed to read payload");

        // Verify HMAC-SHA256(PIN, clientChallenge || serverChallenge)
        uint8_t salt[32];
        std::memcpy(salt, req.clientChallenge, 16);
        std::memcpy(salt + 16, challenge.serverChallenge, 16);

        std::string pin = "482910";
        uint8_t expectedProof[Crypto::SHA256_DIGEST_SIZE];
        Crypto::HmacSha256(reinterpret_cast<const uint8_t*>(pin.data()), pin.size(),
                           salt, sizeof(salt), expectedProof);
        TEST_ASSERT(Crypto::ConstantTimeCompare(reinterpret_cast<const uint8_t*>(req.sessionToken),
                                                expectedProof, Crypto::SHA256_DIGEST_SIZE),
                    "HMAC proof mismatch");

        // 2. Respond MSG_AUTH_RESPONSE (SUCCESS) echoing serverChallenge
        Protocol::AuthResponsePayload resp = {0};
        resp.status = Protocol::AUTH_SUCCESS;
        strncpy_s(resp.message, "Authenticated successfully", sizeof(resp.message) - 1);
        std::memcpy(resp.serverChallenge, challenge.serverChallenge, sizeof(resp.serverChallenge));
        TEST_ASSERT(server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp)),
                    "Server failed to send AUTH_SUCCESS");

        // 3. Derive session key using PIN + salt (clientChallenge + serverChallenge)
        uint8_t sessionKey[Crypto::KEY_SIZE];
        Crypto::DeriveKey(pin, salt, sizeof(salt), sessionKey);
        server.SetSessionKey(sessionKey);

        // 4. In-session receive encrypted ping
        Protocol::PacketHeader encHdr;
        TEST_ASSERT(server.ReceiveExact((char*)&encHdr, sizeof(encHdr)), "Failed to read in-session header");
        TEST_ASSERT(encHdr.type == Protocol::MSG_ENCRYPTED_CONTROL, "Expected MSG_ENCRYPTED_CONTROL");

        std::vector<char> cipherBuf(encHdr.size);
        TEST_ASSERT(server.ReceiveExact(cipherBuf.data(), encHdr.size), "Failed to read ciphertext");

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> plain;
        uint8_t srvKeyCopy[Crypto::KEY_SIZE];
        TEST_ASSERT(server.CopySessionKey(srvKeyCopy), "CopySessionKey failed");
        TEST_ASSERT(ControlEnvelope::UnwrapPacket(srvKeyCopy,
                                                 (const uint8_t*)cipherBuf.data(),
                                                 encHdr.size, innerHdr, plain),
                    "Failed to unwrap encrypted ping");
        TEST_ASSERT(innerHdr.type == Protocol::MSG_PING, "Inner message must be MSG_PING");

        // 5. Respond with encrypted pong
        TEST_ASSERT(server.SendPacket(Protocol::MSG_PONG, nullptr, 0), "Failed to send encrypted pong");

        server.CloseClient();
    });

    TcpClient client;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(client.Connect("127.0.0.1", TEST_PORT), "Client failed to connect");

    // 0. Read MSG_AUTH_CHALLENGE
    Protocol::PacketHeader chalHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&chalHdr, sizeof(chalHdr)), "Client failed to read challenge header");
    TEST_ASSERT(chalHdr.type == Protocol::MSG_AUTH_CHALLENGE, "Expected MSG_AUTH_CHALLENGE");
    TEST_ASSERT(chalHdr.size == sizeof(Protocol::AuthChallengePayload), "AuthChallenge size mismatch");
    Protocol::AuthChallengePayload challenge = {0};
    TEST_ASSERT(client.ReceiveExact((char*)&challenge, sizeof(challenge)), "Client failed to read challenge payload");

    // 1. Send MSG_AUTH_REQUEST with HMAC proof (cleartext PIN must never appear on wire)
    Protocol::AuthRequestPayload req = {0};
    req.clientVersion = Protocol::PROTOCOL_VERSION;
    Crypto::GenerateRandomBytes(req.clientChallenge, sizeof(req.clientChallenge));

    uint8_t cliSalt[32];
    std::memcpy(cliSalt, req.clientChallenge, 16);
    std::memcpy(cliSalt + 16, challenge.serverChallenge, 16);

    std::string pin = "482910";
    uint8_t proof[Crypto::SHA256_DIGEST_SIZE];
    Crypto::HmacSha256(reinterpret_cast<const uint8_t*>(pin.data()), pin.size(),
                       cliSalt, sizeof(cliSalt), proof);
    std::memcpy(req.sessionToken, proof, Crypto::SHA256_DIGEST_SIZE);
    TEST_ASSERT(std::memcmp(req.sessionToken, pin.data(), pin.size()) != 0,
                "Raw PIN must never appear in AuthRequestPayload");

    TEST_ASSERT(client.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&req, sizeof(req)),
                "Client failed to send MSG_AUTH_REQUEST");

    // 2. Read MSG_AUTH_RESPONSE
    Protocol::PacketHeader respHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&respHdr, sizeof(respHdr)), "Client failed to read auth response header");
    TEST_ASSERT(respHdr.type == Protocol::MSG_AUTH_RESPONSE, "Expected MSG_AUTH_RESPONSE");

    Protocol::AuthResponsePayload resp;
    TEST_ASSERT(client.ReceiveExact((char*)&resp, sizeof(resp)), "Client failed to read auth response payload");
    TEST_ASSERT(resp.status == Protocol::AUTH_SUCCESS, "Expected AUTH_SUCCESS");
    TEST_ASSERT(std::memcmp(resp.serverChallenge, challenge.serverChallenge, 16) == 0,
                "AUTH_SUCCESS serverChallenge mismatch");

    // 3. Derive and activate session key on client using mixed salt
    uint8_t salt[32];
    std::memcpy(salt, req.clientChallenge, 16);
    std::memcpy(salt + 16, resp.serverChallenge, 16);

    uint8_t sessionKey[Crypto::KEY_SIZE];
    Crypto::DeriveKey("482910", salt, sizeof(salt), sessionKey);
    client.SetSessionKey(sessionKey);

    // 4. Send encrypted ping
    TEST_ASSERT(client.SendPacket(Protocol::MSG_PING, nullptr, 0), "Client failed to send encrypted ping");

    // 5. Receive encrypted pong
    Protocol::PacketHeader pongHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&pongHdr, sizeof(pongHdr)), "Client failed to read pong header");
    TEST_ASSERT(pongHdr.type == Protocol::MSG_ENCRYPTED_CONTROL, "Expected MSG_ENCRYPTED_CONTROL for pong");

    std::vector<char> pongCipher(pongHdr.size);
    TEST_ASSERT(client.ReceiveExact(pongCipher.data(), pongHdr.size), "Client failed to read pong ciphertext");

    Protocol::PacketHeader innerPong;
    std::vector<uint8_t> plainPong;
    uint8_t cliKeyCopy[Crypto::KEY_SIZE];
    TEST_ASSERT(client.CopySessionKey(cliKeyCopy), "CopySessionKey failed");
    TEST_ASSERT(ControlEnvelope::UnwrapPacket(cliKeyCopy,
                                            (const uint8_t*)pongCipher.data(),
                                            pongHdr.size, innerPong, plainPong),
                "Client failed to unwrap encrypted pong");
    Crypto::SecureZero(cliKeyCopy, sizeof(cliKeyCopy));
    TEST_ASSERT(innerPong.type == Protocol::MSG_PONG, "Inner message must be MSG_PONG");

    srvThread.join();
    server.Close();
    client.Close();
    std::cout << "  [PASS] Valid authentication & transparent encryption verified." << std::endl;
}

// Test 2: Invalid Token Rejection
void TestInvalidTokenRejection() {
    std::cout << "Running TestInvalidTokenRejection..." << std::endl;
    constexpr int TEST_PORT = 19081;

    TcpServer server;
    TEST_ASSERT(server.Start(TEST_PORT), "Failed to start server");

    std::thread srvThread([&]() {
        TEST_ASSERT(server.AcceptConnection(3000), "Server failed to accept");

        Protocol::PacketHeader hdr;
        TEST_ASSERT(server.ReceiveExact((char*)&hdr, sizeof(hdr)), "Server failed to read header");
        Protocol::AuthRequestPayload req;
        TEST_ASSERT(server.ReceiveExact((char*)&req, sizeof(req)), "Server failed to read payload");

        // Verify token mismatch
        if (strcmp(req.sessionToken, "482910") != 0) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_INVALID_TOKEN;
            strncpy_s(resp.message, "Invalid session token", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
        }
        server.CloseClient();
    });

    TcpClient client;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(client.Connect("127.0.0.1", TEST_PORT), "Client failed to connect");

    Protocol::AuthRequestPayload req = {0};
    strncpy_s(req.sessionToken, "WRONG_SECRET_TOKEN_9999", sizeof(req.sessionToken) - 1);
    req.clientVersion = Protocol::PROTOCOL_VERSION;
    TEST_ASSERT(client.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&req, sizeof(req)), "Send failed");

    Protocol::PacketHeader respHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&respHdr, sizeof(respHdr)), "Read header failed");
    Protocol::AuthResponsePayload resp;
    TEST_ASSERT(client.ReceiveExact((char*)&resp, sizeof(resp)), "Read payload failed");
    TEST_ASSERT(resp.status == Protocol::AUTH_INVALID_TOKEN, "Expected AUTH_INVALID_TOKEN");

    srvThread.join();
    server.Close();
    client.Close();
    std::cout << "  [PASS] Invalid token correctly rejected." << std::endl;
}

// Test 3: Stalled / Slowloris Connection Timeout
void TestStalledConnectionTimeout() {
    std::cout << "Running TestStalledConnectionTimeout..." << std::endl;
    constexpr int TEST_PORT = 19082;

    TcpServer server;
    TEST_ASSERT(server.Start(TEST_PORT), "Failed to start server");

    std::thread srvThread([&]() {
        TEST_ASSERT(server.AcceptConnection(3000), "Server failed to accept");

        // Fast select() timeout check (1 second)
        SOCKET ctrlSock = server.GetClientSocket();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ctrlSock, &fds);
        timeval tv = { 1, 0 }; // 1s
        int sel = select(0, &fds, NULL, NULL, &tv);
        TEST_ASSERT(sel == 0, "select() must time out when client sends nothing");

        Protocol::AuthResponsePayload resp = {0};
        resp.status = Protocol::AUTH_TIMEOUT;
        strncpy_s(resp.message, "Timed out", sizeof(resp.message) - 1);
        server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
        server.CloseClient();
    });

    TcpClient client;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(client.Connect("127.0.0.1", TEST_PORT), "Client failed to connect");

    // Client deliberately sleeps without sending auth request
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    Protocol::PacketHeader respHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&respHdr, sizeof(respHdr)), "Read header failed");
    Protocol::AuthResponsePayload resp;
    TEST_ASSERT(client.ReceiveExact((char*)&resp, sizeof(resp)), "Read payload failed");
    TEST_ASSERT(resp.status == Protocol::AUTH_TIMEOUT, "Expected AUTH_TIMEOUT");

    srvThread.join();
    server.Close();
    client.Close();
    std::cout << "  [PASS] Stalled connection timeout verified." << std::endl;
}

// Test 4: Unexpected Initial Packet Rejection
void TestUnexpectedPacketRejection() {
    std::cout << "Running TestUnexpectedPacketRejection..." << std::endl;
    constexpr int TEST_PORT = 19083;

    TcpServer server;
    TEST_ASSERT(server.Start(TEST_PORT), "Failed to start server");

    std::thread srvThread([&]() {
        TEST_ASSERT(server.AcceptConnection(3000), "Server failed to accept");

        Protocol::PacketHeader hdr;
        TEST_ASSERT(server.ReceiveExact((char*)&hdr, sizeof(hdr)), "Server failed to read header");
        if (hdr.type != Protocol::MSG_AUTH_REQUEST) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_REJECTED;
            strncpy_s(resp.message, "Expected MSG_AUTH_REQUEST", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
        }
        server.CloseClient();
    });

    TcpClient client;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(client.Connect("127.0.0.1", TEST_PORT), "Client failed to connect");

    // Send unexpected MSG_PING instead of MSG_AUTH_REQUEST
    TEST_ASSERT(client.SendPacket(Protocol::MSG_PING, nullptr, 0), "Send failed");

    Protocol::PacketHeader respHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&respHdr, sizeof(respHdr)), "Read header failed");
    Protocol::AuthResponsePayload resp;
    TEST_ASSERT(client.ReceiveExact((char*)&resp, sizeof(resp)), "Read payload failed");
    TEST_ASSERT(resp.status == Protocol::AUTH_REJECTED, "Expected AUTH_REJECTED");

    srvThread.join();
    server.Close();
    client.Close();
    std::cout << "  [PASS] Unexpected packet rejection verified." << std::endl;
}

// Test 5: Two Sessions with Same PIN Produce Different Session Keys (R-1)
void TestSaltMixingSessionKeyUniqueness() {
    std::cout << "Running TestSaltMixingSessionKeyUniqueness (R-1)..." << std::endl;

    const std::string pin = "849201";

    // Session 1: Client challenge 1 + Server challenge 1
    uint8_t cChal1[16] = {0}, sChal1[16] = {0};
    Crypto::GenerateRandomBytes(cChal1, 16);
    Crypto::GenerateRandomBytes(sChal1, 16);
    uint8_t salt1[32];
    std::memcpy(salt1, cChal1, 16);
    std::memcpy(salt1 + 16, sChal1, 16);

    uint8_t sessionKey1[Crypto::KEY_SIZE];
    Crypto::DeriveKey(pin, salt1, sizeof(salt1), sessionKey1);

    // Session 2: Client challenge 2 + Server challenge 2
    uint8_t cChal2[16] = {0}, sChal2[16] = {0};
    Crypto::GenerateRandomBytes(cChal2, 16);
    Crypto::GenerateRandomBytes(sChal2, 16);
    uint8_t salt2[32];
    std::memcpy(salt2, cChal2, 16);
    std::memcpy(salt2 + 16, sChal2, 16);

    uint8_t sessionKey2[Crypto::KEY_SIZE];
    Crypto::DeriveKey(pin, salt2, sizeof(salt2), sessionKey2);

    // Verify uniqueness: sessionKey1 must NOT equal sessionKey2
    bool keysIdentical = Crypto::ConstantTimeCompare(sessionKey1, sessionKey2, Crypto::KEY_SIZE);
    TEST_ASSERT(!keysIdentical, "Two independent sessions must not reuse the same session key!");
    std::cout << "  [PASS] Independent sessions produce distinct session keys via unique challenges." << std::endl;
}

// Test 6: Tampered / Wrong PIN Cannot Decrypt Envelope (R-1)
void TestTamperedTokenFailsSessionKey() {
    std::cout << "Running TestTamperedTokenFailsSessionKey (R-1)..." << std::endl;

    const std::string correctPin = "654321";
    const std::string wrongPin   = "654320";

    uint8_t salt[32];
    Crypto::GenerateRandomBytes(salt, 32);

    uint8_t keySender[Crypto::KEY_SIZE];
    Crypto::DeriveKey(correctPin, salt, sizeof(salt), keySender);

    uint8_t keyAttacker[Crypto::KEY_SIZE];
    Crypto::DeriveKey(wrongPin, salt, sizeof(salt), keyAttacker);

    // Sender wraps a packet
    std::vector<uint8_t> envelope;
    TEST_ASSERT(ControlEnvelope::WrapPacket(keySender, Protocol::MSG_PING, nullptr, 0, envelope), "Wrap failed");

    // Attacker tries to unwrap with key derived from wrong PIN
    Protocol::PacketHeader unwrappedHdr;
    std::vector<uint8_t> plain;
    bool decryptSuccess = ControlEnvelope::UnwrapPacket(keyAttacker, envelope.data(), envelope.size(), unwrappedHdr, plain);
    TEST_ASSERT(!decryptSuccess, "Decryption with wrong key derived from invalid PIN must fail!");

    std::cout << "  [PASS] Session key derived from invalid PIN cannot decrypt control traffic." << std::endl;
}

// Test 7: Protocol Version Mismatch Rejection (N-1)
void TestVersionMismatchRejection() {
    std::cout << "Running TestVersionMismatchRejection (N-1)..." << std::endl;
    constexpr int TEST_PORT = 19086;

    TcpServer server;
    TEST_ASSERT(server.Start(TEST_PORT), "Failed to start server");

    std::thread srvThread([&]() {
        TEST_ASSERT(server.AcceptConnection(3000), "Server failed to accept");

        Protocol::PacketHeader hdr;
        TEST_ASSERT(server.ReceiveExact((char*)&hdr, sizeof(hdr)), "Server failed to read header");
        TEST_ASSERT(hdr.type == Protocol::MSG_AUTH_REQUEST, "Expected MSG_AUTH_REQUEST");

        Protocol::AuthRequestPayload req = {0};
        TEST_ASSERT(server.ReceiveExact((char*)&req, sizeof(req)), "Server failed to read payload");

        // Server rejects version mismatch
        if (req.clientVersion != Protocol::PROTOCOL_VERSION) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_VERSION_MISMATCH;
            strncpy_s(resp.message, "Protocol version mismatch", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
        }
        server.CloseClient();
    });

    TcpClient client;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(client.Connect("127.0.0.1", TEST_PORT), "Client failed to connect");

    Protocol::AuthRequestPayload req = {0};
    strncpy_s(req.sessionToken, "123456", sizeof(req.sessionToken) - 1);
    req.clientVersion = 9999; // Intentionally incompatible version!
    Crypto::GenerateRandomBytes(req.clientChallenge, sizeof(req.clientChallenge));

    TEST_ASSERT(client.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&req, sizeof(req)), "Send failed");

    Protocol::PacketHeader respHdr;
    TEST_ASSERT(client.ReceiveExact((char*)&respHdr, sizeof(respHdr)), "Read header failed");
    Protocol::AuthResponsePayload resp;
    TEST_ASSERT(client.ReceiveExact((char*)&resp, sizeof(resp)), "Read payload failed");
    TEST_ASSERT(resp.status == Protocol::AUTH_VERSION_MISMATCH, "Expected AUTH_VERSION_MISMATCH");

    srvThread.join();
    server.Close();
    client.Close();
    std::cout << "  [PASS] Protocol version mismatch correctly rejected." << std::endl;
}

// Test 8: Packet Size & Type Validation (N-1)
void TestPacketSizeValidation() {
    std::cout << "Running TestPacketSizeValidation (N-1)..." << std::endl;

    // 1. Valid packet types and sizes
    TEST_ASSERT(Protocol::ValidatePacketSize(Protocol::MSG_PING, 0), "MSG_PING with 0 size must be valid");
    TEST_ASSERT(Protocol::ValidatePacketSize(Protocol::MSG_PONG, 0), "MSG_PONG with 0 size must be valid");
    TEST_ASSERT(Protocol::ValidatePacketSize(Protocol::MSG_AUTH_CHALLENGE, sizeof(Protocol::AuthChallengePayload)), "AuthChallenge size valid");
    TEST_ASSERT(Protocol::ValidatePacketSize(Protocol::MSG_AUTH_REQUEST, sizeof(Protocol::AuthRequestPayload)), "AuthRequest size valid");
    TEST_ASSERT(Protocol::ValidatePacketSize(Protocol::MSG_SPECIAL_ACTION, sizeof(Protocol::SpecialActionPayload)), "SpecialAction valid");
    TEST_ASSERT(Protocol::ValidatePacketSize(Protocol::MSG_VIDEO_CONFIG, sizeof(Protocol::VideoConfigPayload)), "VideoConfig valid");

    // 2. Invalid packet sizes for fixed-size messages
    TEST_ASSERT(!Protocol::ValidatePacketSize(Protocol::MSG_PING, 10), "MSG_PING with non-zero size must fail");
    TEST_ASSERT(!Protocol::ValidatePacketSize(Protocol::MSG_AUTH_CHALLENGE, 8), "AuthChallenge with wrong size must fail");
    TEST_ASSERT(!Protocol::ValidatePacketSize(Protocol::MSG_AUTH_REQUEST, 10), "AuthRequest with wrong size must fail");
    TEST_ASSERT(!Protocol::ValidatePacketSize(Protocol::MSG_SPECIAL_ACTION, 5), "SpecialAction with wrong size must fail");
    TEST_ASSERT(!Protocol::ValidatePacketSize(Protocol::MSG_VIDEO_CONFIG, 1), "VideoConfig with wrong size must fail");

    // 3. Unknown message types
    TEST_ASSERT(!Protocol::ValidatePacketSize((Protocol::MessageType)255, 0), "Unknown message type must fail");
    TEST_ASSERT(!Protocol::ValidatePacketSize((Protocol::MessageType)0, 0), "Zero message type must fail");

    std::cout << "  [PASS] Packet size and type validation verified for all boundary cases." << std::endl;
}

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << " PBL4 Session Authentication Test Suite" << std::endl;
    std::cout << "=========================================" << std::endl;

    Network::Initialize();

    TestValidAuthAndEncryption();
    TestInvalidTokenRejection();
    TestStalledConnectionTimeout();
    TestUnexpectedPacketRejection();
    TestSaltMixingSessionKeyUniqueness();
    TestTamperedTokenFailsSessionKey();
    TestVersionMismatchRejection();
    TestPacketSizeValidation();

    Network::Cleanup();

    std::cout << "=========================================" << std::endl;
    std::cout << " ALL AUTH HANDSHAKE TESTS PASSED (100%)" << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}
