#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

#include "../Shared/Network.h"
#include "../Shared/RFB/RfbDefines.h"
#include "../Client/Core/RfbServerSession.h"
#include "../Client/Core/SessionManager.h"

int main() {
    std::cout << "============================================================\n";
    std::cout << "  LANREMOTE - RFB PORT 5900 GATE & AUTHORIZATION SUITE (CR-1)\n";
    std::cout << "============================================================\n";

    Network::Initialize();

    const int TEST_PORT = 5904;
    // Clear any previous authorization
    SessionManager::RevokeAuthorizedPeer();

    if (!RfbServer::Start(TEST_PORT, "Authorization Test")) {
        std::cerr << "[FAIL] Cannot start RFB server\n";
        Network::Cleanup();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // -------------------------------------------------------------
    // Test 1: Unauthorized RFB peer must be rejected immediately
    // -------------------------------------------------------------
    std::cout << "Running Test 1: Unauthorized RFB connection rejection...\n";
    {
        TcpClient unauthorizedClient;
        if (!unauthorizedClient.Connect("127.0.0.1", TEST_PORT)) {
            std::cerr << "[FAIL] Cannot connect unauthorized RFB peer\n";
            RfbServer::Stop();
            Network::Cleanup();
            return 1;
        }

        DWORD receiveTimeout = 500;
        SOCKET sock = unauthorizedClient.GetConnectSocket();
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receiveTimeout, sizeof(receiveTimeout));

        char protocolVersion[13] = {0};
        bool receivedHandshake = unauthorizedClient.ReceiveExact(protocolVersion, (int)Rfb::VERSION_STRING_LENGTH);
        unauthorizedClient.Close();

        if (receivedHandshake) {
            std::cerr << "[FAIL] Unauthorized RFB peer received ProtocolVersion!\n";
            RfbServer::Stop();
            Network::Cleanup();
            return 1;
        }
        std::cout << "  [PASS] Unauthorized RFB peer was rejected before handshake (CR-1)\n";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // -------------------------------------------------------------
    // Test 2: Authorized RFB peer (via 8080 session) must be accepted
    // -------------------------------------------------------------
    std::cout << "Running Test 2: Authorized RFB peer connection acceptance...\n";
    {
        // Grant authorization to 127.0.0.1 for 15 seconds
        SessionManager::AuthorizePeer("127.0.0.1", 15000);

        TcpClient authorizedClient;
        if (!authorizedClient.Connect("127.0.0.1", TEST_PORT)) {
            std::cerr << "[FAIL] Cannot connect authorized RFB peer\n";
            RfbServer::Stop();
            Network::Cleanup();
            return 1;
        }

        DWORD receiveTimeout = 1000;
        SOCKET sock = authorizedClient.GetConnectSocket();
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receiveTimeout, sizeof(receiveTimeout));

        char protocolVersion[13] = {0};
        bool receivedHandshake = authorizedClient.ReceiveExact(protocolVersion, (int)Rfb::VERSION_STRING_LENGTH);

        if (!receivedHandshake) {
            std::cerr << "[FAIL] Authorized RFB peer did not receive ProtocolVersion!\n";
            authorizedClient.Close();
            RfbServer::Stop();
            Network::Cleanup();
            return 1;
        }

        // Must match "RFB 003.008\n"
        assert(std::string(protocolVersion, (int)Rfb::VERSION_STRING_LENGTH) == Rfb::VERSION_3_8_STRING);
        authorizedClient.Close();
        std::cout << "  [PASS] Authorized RFB peer received ProtocolVersion and connected successfully (CR-1)\n";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // -------------------------------------------------------------
    // Test 3: Revoked RFB peer must be rejected after session ends
    // -------------------------------------------------------------
    std::cout << "Running Test 3: Revoked RFB peer rejected upon session termination...\n";
    {
        SessionManager::RevokeAuthorizedPeer();

        TcpClient revokedClient;
        if (!revokedClient.Connect("127.0.0.1", TEST_PORT)) {
            std::cerr << "[FAIL] Cannot connect revoked RFB peer\n";
            RfbServer::Stop();
            Network::Cleanup();
            return 1;
        }

        DWORD receiveTimeout = 500;
        SOCKET sock = revokedClient.GetConnectSocket();
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receiveTimeout, sizeof(receiveTimeout));

        char protocolVersion[13] = {0};
        bool receivedHandshake = revokedClient.ReceiveExact(protocolVersion, (int)Rfb::VERSION_STRING_LENGTH);
        revokedClient.Close();

        if (receivedHandshake) {
            std::cerr << "[FAIL] Revoked RFB peer received ProtocolVersion!\n";
            RfbServer::Stop();
            Network::Cleanup();
            return 1;
        }
        std::cout << "  [PASS] Revoked RFB peer was rejected after session termination (CR-1)\n";
    }

    RfbServer::Stop();
    Network::Cleanup();

    std::cout << "============================================================\n";
    std::cout << " ALL RFB AUTHORIZATION TESTS PASSED (100%)\n";
    std::cout << "============================================================\n";
    return 0;
}
