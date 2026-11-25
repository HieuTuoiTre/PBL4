#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <psapi.h>

#include "../Client/Core/SessionManager.h"
#include "../Shared/Network.h"
#include "../Shared/Protocol.h"
#include "../Shared/Crypto.h"
#include "../Shared/ControlEnvelope.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <random>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <cstring>

using namespace std::chrono;

namespace {
    constexpr int TEST_BASE_PORT = 18080;
    constexpr int PORT_CTRL = TEST_BASE_PORT;      // 18080
    constexpr int PORT_VID  = TEST_BASE_PORT + 1;  // 18081
    constexpr int PORT_AUD  = TEST_BASE_PORT + 2;  // 18082

    int g_totalTests = 0;
    int g_passedTests = 0;
    int g_failedTests = 0;

    void PrintBanner(const std::string& title) {
        std::cout << "\n======================================================================\n";
        std::cout << "  " << title << "\n";
        std::cout << "======================================================================\n";
    }

    void ReportResult(const std::string& testName, bool passed, const std::string& detail) {
        g_totalTests++;
        if (passed) {
            g_passedTests++;
            std::cout << "  [PASS] " << std::left << std::setw(42) << testName 
                      << " | " << detail << "\n";
        } else {
            g_failedTests++;
            std::cout << "  [FAIL] " << std::left << std::setw(42) << testName 
                      << " | " << detail << "\n";
        }
    }

    size_t GetCurrentMemoryWorkingSet() {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize;
        }
        return 0;
    }

    // Helper to connect a raw socket with timeout
    SOCKET ConnectRawSocket(const char* ip, int port, int timeoutMs = 2000) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;

        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);

        int res = connect(s, (sockaddr*)&addr, sizeof(addr));
        if (res == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                fd_set wFds, eFds;
                FD_ZERO(&wFds); FD_SET(s, &wFds);
                FD_ZERO(&eFds); FD_SET(s, &eFds);
                timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
                int sel = select(0, NULL, &wFds, &eFds, &tv);
                if (sel <= 0) {
                    closesocket(s);
                    return INVALID_SOCKET;
                }
                int soErr = 0;
                int soLen = sizeof(soErr);
                getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &soLen);
                if (soErr != 0) {
                    closesocket(s);
                    return INVALID_SOCKET;
                }
            } else {
                closesocket(s);
                return INVALID_SOCKET;
            }
        }

        u_long blocking = 0;
        ioctlsocket(s, FIONBIO, &blocking);
        return s;
    }

    // Helper to poll socket read with timeout (ms)
    // Returns: 1 if readable, 0 if timed out, -1 on socket error
    int PollSocketReadable(SOCKET s, int timeoutMs) {
        fd_set rFds;
        FD_ZERO(&rFds);
        FD_SET(s, &rFds);
        timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        return select(0, &rFds, NULL, NULL, &tv);
    }

    bool RecvExact(SOCKET s, char* buf, int len) {
        int total = 0;
        while (total < len) {
            int r = recv(s, buf + total, len - total, 0);
            if (r <= 0) return false;
            total += r;
        }
        return true;
    }

    bool RecvPacket(SOCKET s, Protocol::PacketHeader& outHdr, std::vector<char>& outPayload) {
        if (!RecvExact(s, (char*)&outHdr, sizeof(outHdr))) return false;
        if (outHdr.size > 0 && outHdr.size <= 65536) {
            outPayload.resize(outHdr.size);
            if (!RecvExact(s, outPayload.data(), (int)outHdr.size)) return false;
        } else {
            outPayload.clear();
        }
        return true;
    }
}

// ======================================================================
// SUITE 1: Port Gating & Unauthenticated Media Probes (8081 & 8082)
// ======================================================================
void RunSuite1_PortGating() {
    PrintBanner("SUITE 1: Port Gating & Unauthenticated Media Probes (8081 / 8082)");

    // Case 1.1: Direct connect to Video port 8081 without authenticating on 8080
    {
        SOCKET vidSock = ConnectRawSocket("127.0.0.1", PORT_VID, 1000);
        bool connected = (vidSock != INVALID_SOCKET);

        bool leakedData = false;
        if (connected) {
            // Check if server ever sends any video packet
            char buf[256];
            int sel = PollSocketReadable(vidSock, 600); // 600ms poll
            if (sel > 0) {
                int r = recv(vidSock, buf, sizeof(buf), 0);
                if (r > 0) leakedData = true; // Video leaked!
            }
            closesocket(vidSock);
        }

        ReportResult("Case 1.1: Unauth Video Probe (8081)", !leakedData,
                     leakedData ? "FAIL: Video frame leaked without 8080 auth!" 
                                : "No video frames leaked; socket unaccepted/silent");
    }

    // Case 1.2: Direct connect to Audio port 8082 without authenticating on 8080
    {
        SOCKET audSock = ConnectRawSocket("127.0.0.1", PORT_AUD, 1000);
        bool connected = (audSock != INVALID_SOCKET);

        bool leakedData = false;
        if (connected) {
            char buf[256];
            int sel = PollSocketReadable(audSock, 600);
            if (sel > 0) {
                int r = recv(audSock, buf, sizeof(buf), 0);
                if (r > 0) leakedData = true; // Audio leaked!
            }
            closesocket(audSock);
        }

        ReportResult("Case 1.2: Unauth Audio Probe (8082)", !leakedData,
                     leakedData ? "FAIL: Audio data leaked without 8080 auth!" 
                                : "No audio chunks leaked; socket unaccepted/silent");
    }

    // Case 1.3: Simultaneous probe (connect 8081 & 8082, connect 8080 with INVALID token)
    {
        SOCKET vidSock = ConnectRawSocket("127.0.0.1", PORT_VID, 1000);
        SOCKET audSock = ConnectRawSocket("127.0.0.1", PORT_AUD, 1000);
        SOCKET ctrlSock = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);

        bool passed = false;
        if (ctrlSock != INVALID_SOCKET) {
            // Send invalid auth token
            Protocol::PacketHeader hdr;
            hdr.type = Protocol::MSG_AUTH_REQUEST;
            hdr.size = sizeof(Protocol::AuthRequestPayload);
            send(ctrlSock, (const char*)&hdr, sizeof(hdr), 0);

            Protocol::AuthRequestPayload req = {};
            strncpy_s(req.sessionToken, "ATTACKER_INVALID_TOKEN_XYZ", sizeof(req.sessionToken) - 1);
            req.clientVersion = Protocol::PROTOCOL_VERSION;
            send(ctrlSock, (const char*)&req, sizeof(req), 0);

            // Read response on control socket
            Protocol::PacketHeader respHdr = {};
            Protocol::AuthResponsePayload resp = {};
            recv(ctrlSock, (char*)&respHdr, sizeof(respHdr), 0);
            recv(ctrlSock, (char*)&resp, sizeof(resp), 0);

            // Verify 8080 rejected
            bool authRejected = (resp.status == Protocol::AUTH_INVALID_TOKEN);

            // Verify 8081 and 8082 received zero data
            char tempBuf[128];
            int selVid = PollSocketReadable(vidSock, 300);
            int selAud = PollSocketReadable(audSock, 300);
            bool vidSilent = (selVid <= 0 || recv(vidSock, tempBuf, sizeof(tempBuf), 0) <= 0);
            bool audSilent = (selAud <= 0 || recv(audSock, tempBuf, sizeof(tempBuf), 0) <= 0);

            passed = authRejected && vidSilent && audSilent;
        }

        if (vidSock != INVALID_SOCKET) closesocket(vidSock);
        if (audSock != INVALID_SOCKET) closesocket(audSock);
        if (ctrlSock != INVALID_SOCKET) closesocket(ctrlSock);

        ReportResult("Case 1.3: Invalid Auth Gates Media", passed,
                     passed ? "Port 8080 rejected; media ports 8081/8082 gated" 
                            : "FAIL: Media streams were opened after invalid auth");
    }

    // Case 1.4: Media Stream Hijack Resistance (Independent socket probes don't crash server)
    {
        SOCKET s1 = ConnectRawSocket("127.0.0.1", PORT_VID, 500);
        SOCKET s2 = ConnectRawSocket("127.0.0.1", PORT_AUD, 500);
        if (s1 != INVALID_SOCKET) {
            const char* garbage = "RANDOM_VIDEO_INJECTION_BYTES_123456789";
            send(s1, garbage, (int)strlen(garbage), 0);
            closesocket(s1);
        }
        if (s2 != INVALID_SOCKET) {
            const char* garbage = "RANDOM_AUDIO_INJECTION_BYTES_123456789";
            send(s2, garbage, (int)strlen(garbage), 0);
            closesocket(s2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Verify server is still completely responsive on 8080
        SOCKET testCtrl = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool serverAlive = (testCtrl != INVALID_SOCKET);
        if (testCtrl != INVALID_SOCKET) closesocket(testCtrl);

        ReportResult("Case 1.4: Media Port Garbage Resilience", serverAlive,
                     serverAlive ? "Server survived rogue writes to 8081/8082 intact" 
                                 : "FAIL: Server crashed or stopped listening");
    }
}

// ======================================================================
// SUITE 2: Slowloris & Socket Stall Resistance (Port 8080)
// ======================================================================
void RunSuite2_SlowlorisAndTimeout() {
    PrintBanner("SUITE 2: Slowloris & Connection Stall Resistance (Port 8080)");

    // Case 2.1: Full socket stall after TCP connect (0 bytes sent)
    {
        auto tStart = steady_clock::now();
        SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool passed = false;
        double elapsedSec = 0.0;

        if (s != INVALID_SOCKET) {
            Protocol::PacketHeader respHdr = {};
            std::vector<char> payload;
            bool recvd = RecvPacket(s, respHdr, payload);
            auto tEnd = steady_clock::now();
            elapsedSec = duration<double>(tEnd - tStart).count();

            // Expected timeout is ~3.0s (server select) <= 3.5s
            if (elapsedSec >= 2.8 && elapsedSec <= 3.6) {
                if (recvd && respHdr.type == Protocol::MSG_AUTH_RESPONSE &&
                    payload.size() >= sizeof(Protocol::AuthResponsePayload)) {
                    auto* resp = (Protocol::AuthResponsePayload*)payload.data();
                    passed = (resp->status == Protocol::AUTH_TIMEOUT);
                } else if (!recvd) {
                    // Socket dropped cleanly
                    passed = true;
                }
            }
            closesocket(s);
        }

        std::string detail = "Dropped in " + std::to_string(elapsedSec) + "s (Target: <= 3.5s)";
        ReportResult("Case 2.1: Full Socket Stall (0 bytes)", passed, detail);
    }

    // Case 2.2: Partial header stall (send 2 bytes of 5-byte header, then stall)
    {
        auto tStart = steady_clock::now();
        SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool passed = false;
        double elapsedSec = 0.0;

        if (s != INVALID_SOCKET) {
            char partialHeader[2] = { (char)Protocol::MSG_AUTH_REQUEST, 0x00 };
            send(s, partialHeader, 2, 0);

            char buf[256];
            int r = recv(s, buf, sizeof(buf), 0);
            auto tEnd = steady_clock::now();
            elapsedSec = duration<double>(tEnd - tStart).count();

            // SO_RCVTIMEO = 3000ms should fire
            if (elapsedSec >= 2.7 && elapsedSec <= 3.6 && r <= 0) {
                passed = true;
            }
            closesocket(s);
        }

        std::string detail = "Socket closed in " + std::to_string(elapsedSec) + "s (SO_RCVTIMEO fired)";
        ReportResult("Case 2.2: Partial Header Stall (2 bytes)", passed, detail);
    }

    // Case 2.3: Header sent, payload stalled (send 5-byte header, only 5 bytes of payload)
    {
        auto tStart = steady_clock::now();
        SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool passed = false;
        double elapsedSec = 0.0;

        if (s != INVALID_SOCKET) {
            Protocol::PacketHeader hdr;
            hdr.type = Protocol::MSG_AUTH_REQUEST;
            hdr.size = sizeof(Protocol::AuthRequestPayload);
            send(s, (const char*)&hdr, sizeof(hdr), 0);

            // Send partial payload: 5 bytes out of 68
            char partialPayload[5] = { 'P', 'B', 'L', '4', '_' };
            send(s, partialPayload, 5, 0);

            char buf[256];
            int r = recv(s, buf, sizeof(buf), 0);
            auto tEnd = steady_clock::now();
            elapsedSec = duration<double>(tEnd - tStart).count();

            if (elapsedSec >= 2.7 && elapsedSec <= 3.6 && r <= 0) {
                passed = true;
            }
            closesocket(s);
        }

        std::string detail = "Socket closed in " + std::to_string(elapsedSec) + "s (Payload stall aborted)";
        ReportResult("Case 2.3: Incomplete Payload Stall", passed, detail);
    }
}

// ======================================================================
// SUITE 3: Malformed & Garbage Packet Deluge
// ======================================================================
void RunSuite3_GarbageInjection() {
    PrintBanner("SUITE 3: Malformed & Garbage Packet Deluge (Port 8080)");

    // Case 3.1: 4096 bytes of random binary entropy deluge
    {
        SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool passed = false;

        if (s != INVALID_SOCKET) {
            std::vector<char> noise(4096);
            std::mt19937 rng(1337);
            for (auto& b : noise) b = (char)(rng() & 0xFF);

            send(s, noise.data(), (int)noise.size(), 0);
            shutdown(s, SD_SEND); // EOF so server finishes reading stream without 3s stall

            Protocol::PacketHeader respHdr = {};
            std::vector<char> payload;
            bool recvd = RecvPacket(s, respHdr, payload);

            if (recvd && respHdr.type == Protocol::MSG_AUTH_RESPONSE &&
                payload.size() >= sizeof(Protocol::AuthResponsePayload)) {
                auto* resp = (Protocol::AuthResponsePayload*)payload.data();
                passed = (resp->status == Protocol::AUTH_REJECTED);
            } else {
                // Connection closed or dropped cleanly by server
                passed = true;
            }
            closesocket(s);
        }

        ReportResult("Case 3.1: 4KB Random Binary Noise Deluge", passed,
                     passed ? "Server rejected with AUTH_REJECTED or clean drop" 
                            : "FAIL: Unexpected socket state or hang");
    }

    // Case 3.2: Giant Header Size Integer Overflow / Allocation Bomb (0xFFFFFFFF = 4GB)
    {
        SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool passed = false;

        if (s != INVALID_SOCKET) {
            Protocol::PacketHeader bombHdr;
            bombHdr.type = Protocol::MSG_AUTH_REQUEST;
            bombHdr.size = 0xFFFFFFFF; // 4 GB claim

            send(s, (const char*)&bombHdr, sizeof(bombHdr), 0);

            Protocol::PacketHeader respHdr = {};
            std::vector<char> payload;
            bool recvd = RecvPacket(s, respHdr, payload);

            if (recvd && respHdr.type == Protocol::MSG_AUTH_RESPONSE &&
                payload.size() >= sizeof(Protocol::AuthResponsePayload)) {
                auto* resp = (Protocol::AuthResponsePayload*)payload.data();
                passed = (resp->status == Protocol::AUTH_REJECTED);
            } else {
                passed = true; // Clean socket drop without OOM
            }
            closesocket(s);
        }

        ReportResult("Case 3.2: 4GB Header Size Allocation Bomb", passed,
                     passed ? "Safely rejected without heap exhaustion/OOM" 
                            : "FAIL: Server crashed or attempted huge alloc");
    }

    // Case 3.3: Rapid Truncated Packets & Immediate Abort (1B, 2B, 3B, 4B)
    {
        bool allSurvived = true;
        for (int truncLen = 1; truncLen <= 4; truncLen++) {
            SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
            if (s == INVALID_SOCKET) { allSurvived = false; break; }

            char junk[4] = { 0x01, 0x02, 0x03, 0x04 };
            send(s, junk, truncLen, 0);

            // Force abrupt TCP RST
            linger sl = { 1, 0 }; // linger on, timeout 0 -> sends RST
            setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
            closesocket(s);
        }

        // Verify server is alive
        SOCKET testSock = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        bool alive = (testSock != INVALID_SOCKET);
        if (testSock != INVALID_SOCKET) closesocket(testSock);

        ReportResult("Case 3.3: Rapid Truncated Packets + TCP RST", allSurvived && alive,
                     (allSurvived && alive) ? "Server handled EOF/RST without thread spin" 
                                            : "FAIL: Server hung or crashed");
    }

    // Case 3.4: Post-Handshake Tampered / Corrupted AEAD Packets
    {
        // 1. Establish valid session
        TcpClient client;
        bool sessionOk = client.Connect("127.0.0.1", PORT_CTRL);
        bool passedTamperTest = false;

        if (sessionOk) {
            // Handshake
            Protocol::AuthRequestPayload req = {};
            strncpy_s(req.sessionToken, "482910", sizeof(req.sessionToken) - 1);
            req.clientVersion = Protocol::PROTOCOL_VERSION;
            client.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&req, sizeof(req));

            Protocol::PacketHeader respHdr = {};
            Protocol::AuthResponsePayload resp = {};
            client.ReceiveExact((char*)&respHdr, sizeof(respHdr));
            client.ReceiveExact((char*)&resp, sizeof(resp));

            if (resp.status == Protocol::AUTH_SUCCESS) {
                uint8_t key[32];
                Crypto::DeriveKey("482910", nullptr, 0, key);
                client.SetSessionKey(key);

                // Now craft an intentionally tampered MSG_ENCRYPTED_CONTROL packet:
                // Normal wrap:
                std::vector<uint8_t> validCipher;
                ControlEnvelope::WrapPacket(key, Protocol::MSG_PING, nullptr, 0, validCipher);

                // Tamper with the MAC tag (last 16 bytes): flip 1 byte
                std::vector<uint8_t> tamperedCipher = validCipher;
                tamperedCipher.back() ^= 0xFF; // Invalidate Poly1305 MAC tag

                // Send tampered packet
                Protocol::PacketHeader badHdr;
                badHdr.type = Protocol::MSG_ENCRYPTED_CONTROL;
                badHdr.size = (uint32_t)tamperedCipher.size();
                send(client.GetConnectSocket(), (const char*)&badHdr, sizeof(badHdr), 0);
                send(client.GetConnectSocket(), (const char*)tamperedCipher.data(), (int)tamperedCipher.size(), 0);

                // The server unwrap should fail and immediately terminate connection
                char checkBuf[64];
                int r = recv(client.GetConnectSocket(), checkBuf, sizeof(checkBuf), 0);
                // recv should return <= 0 (connection closed by server)
                passedTamperTest = (r <= 0);
            }
            client.Close();
        }

        ReportResult("Case 3.4: Post-Auth AEAD Tag Tamper Rejection", passedTamperTest,
                     passedTamperTest ? "Poly1305 MAC failure caught; session dropped" 
                                      : "FAIL: Corrupted packet was processed or socket kept open");
    }
}

// ======================================================================
// SUITE 4: Repeated Rapid Reconnect Stress (50 iterations)
// ======================================================================
void RunSuite4_RapidReconnectStress() {
    PrintBanner("SUITE 4: Repeated Rapid Reconnect Stress (50 Iterations)");

    constexpr int ITERATIONS = 50;
    std::vector<double> latenciesMs;
    latenciesMs.reserve(ITERATIONS);

    size_t memBefore = GetCurrentMemoryWorkingSet();
    int rejectedCount = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        auto tStart = steady_clock::now();
        SOCKET s = ConnectRawSocket("127.0.0.1", PORT_CTRL, 1000);
        if (s == INVALID_SOCKET) break;

        Protocol::PacketHeader hdr;
        hdr.type = Protocol::MSG_AUTH_REQUEST;
        hdr.size = sizeof(Protocol::AuthRequestPayload);
        send(s, (const char*)&hdr, sizeof(hdr), 0);

        Protocol::AuthRequestPayload req = {};
        std::string badToken = "ATTACKER_BRUTE_TOKEN_" + std::to_string(i) + "_XYZ";
        strncpy_s(req.sessionToken, badToken.c_str(), sizeof(req.sessionToken) - 1);
        req.clientVersion = Protocol::PROTOCOL_VERSION;
        send(s, (const char*)&req, sizeof(req), 0);

        Protocol::PacketHeader respHdr = {};
        Protocol::AuthResponsePayload resp = {};
        int r1 = recv(s, (char*)&respHdr, sizeof(respHdr), 0);
        int r2 = recv(s, (char*)&resp, sizeof(resp), 0);

        auto tEnd = steady_clock::now();
        double ms = duration<double, std::milli>(tEnd - tStart).count();
        latenciesMs.push_back(ms);

        if (r1 > 0 && r2 > 0 && resp.status == Protocol::AUTH_INVALID_TOKEN) {
            rejectedCount++;
        }
        closesocket(s);
    }

    size_t memAfter = GetCurrentMemoryWorkingSet();
    double memDeltaMB = (double)((int64_t)memAfter - (int64_t)memBefore) / (1024.0 * 1024.0);

    // Compute metrics
    double sum = std::accumulate(latenciesMs.begin(), latenciesMs.end(), 0.0);
    double avgMs = latenciesMs.empty() ? 0.0 : sum / latenciesMs.size();
    double minMs = latenciesMs.empty() ? 0.0 : *std::min_element(latenciesMs.begin(), latenciesMs.end());
    double maxMs = latenciesMs.empty() ? 0.0 : *std::max_element(latenciesMs.begin(), latenciesMs.end());

    double sqSum = 0.0;
    for (double v : latenciesMs) sqSum += (v - avgMs) * (v - avgMs);
    double stdDev = latenciesMs.empty() ? 0.0 : std::sqrt(sqSum / latenciesMs.size());

    bool stressPassed = (rejectedCount == ITERATIONS);
    std::string detail = std::to_string(rejectedCount) + "/" + std::to_string(ITERATIONS) + " rejected. " +
                         "Latency: min=" + std::to_string((int)minMs) + "ms, " +
                         "avg=" + std::to_string((int)avgMs) + "ms, " +
                         "max=" + std::to_string((int)maxMs) + "ms, " +
                         "stddev=" + std::to_string((int)stdDev) + "ms";
    ReportResult("Case 4.1: 50 Back-to-Back Invalid Token Rejections", stressPassed, detail);

    bool memOk = (memDeltaMB < 2.0); // Less than 2 MB growth across 50 connects
    ReportResult("Case 4.2: Memory Leak Verification", memOk,
                 "WorkingSet delta: " + std::to_string(memDeltaMB) + " MB (Baseline: " +
                 std::to_string(memBefore / (1024 * 1024)) + " MB)");

    // Case 4.3: Legitimate Session Acceptance Immediately After 50-Burst Attack
    {
        SOCKET ctrlSock = ConnectRawSocket("127.0.0.1", PORT_CTRL, 2000);
        bool postStressOk = false;

        if (ctrlSock != INVALID_SOCKET) {
            Protocol::PacketHeader hdr;
            hdr.type = Protocol::MSG_AUTH_REQUEST;
            hdr.size = sizeof(Protocol::AuthRequestPayload);
            send(ctrlSock, (const char*)&hdr, sizeof(hdr), 0);

            Protocol::AuthRequestPayload req = {};
            strncpy_s(req.sessionToken, "482910", sizeof(req.sessionToken) - 1);
            req.clientVersion = Protocol::PROTOCOL_VERSION;
            send(ctrlSock, (const char*)&req, sizeof(req), 0);

            Protocol::PacketHeader respHdr = {};
            Protocol::AuthResponsePayload resp = {};
            recv(ctrlSock, (char*)&respHdr, sizeof(respHdr), 0);
            recv(ctrlSock, (char*)&resp, sizeof(resp), 0);

            if (resp.status == Protocol::AUTH_SUCCESS) {
                postStressOk = true;
            }
            closesocket(ctrlSock);
        }

        ReportResult("Case 4.3: Legitimate Session Post-Stress", postStressOk,
                     postStressOk ? "Server immediately accepted valid session post-attack" 
                                  : "FAIL: Server failed to accept legitimate session");
    }
}

int main() {
    std::cout << "======================================================================\n";
    std::cout << "  PBL4 Milestone M1 Empirical Adversarial Network Verification Suite  \n";
    std::cout << "======================================================================\n";

    // Initialize Winsock
    if (!Network::Initialize()) {
        std::cerr << "Failed to initialize Winsock!" << std::endl;
        return 1;
    }

    // Initialize GDI+ for SessionManager overlay support
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    // Launch SessionManager in background thread on test ports (18080, 18081, 18082)
    std::cout << "[INFO] Starting SessionManager on base port " << TEST_BASE_PORT << "...\n";
    std::thread serverThread([]() {
        SessionManager::StartServer(TEST_BASE_PORT);
    });

    // Give server thread 200ms to bind sockets
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Execute all 4 adversarial attack suites
    RunSuite1_PortGating();
    RunSuite2_SlowlorisAndTimeout();
    RunSuite3_GarbageInjection();
    RunSuite4_RapidReconnectStress();

    // Cleanly stop SessionManager
    std::cout << "\n[INFO] Stopping SessionManager...\n";
    SessionManager::Stop();

    if (serverThread.joinable()) {
        serverThread.join();
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    Network::Cleanup();

    // Summary
    PrintBanner("VERIFICATION SUMMARY & METRICS");
    std::cout << "  Total Empirical Test Cases: " << g_totalTests << "\n";
    std::cout << "  Passed Test Cases:          " << g_passedTests << " (" 
              << (g_totalTests > 0 ? (g_passedTests * 100 / g_totalTests) : 0) << "%)\n";
    std::cout << "  Failed Test Cases:          " << g_failedTests << "\n";

    if (g_failedTests == 0) {
        std::cout << "\n  ADVERSARIAL VERDICT: APPROVE (All resilience gates validated)\n";
    } else {
        std::cout << "\n  ADVERSARIAL VERDICT: REQUEST_CHANGES (" << g_failedTests << " failure(s))\n";
    }
    std::cout << "======================================================================\n";

    return (g_failedTests == 0) ? 0 : 1;
}
