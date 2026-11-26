#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <mutex>
#include <atomic>
#include <cstring>
#include "Protocol.h"

class Network{
    public:
        Network() = delete; //prevent empty network object

        static bool Initialize();
        static void Cleanup();
};  

//the host (controlled machine)
class TcpServer{
    private:
        // Sử dụng std::atomic để chống race condition khi đóng socket đa luồng
        std::atomic<SOCKET> listenSocket{INVALID_SOCKET};
        std::atomic<SOCKET> clientSocket{INVALID_SOCKET};
        int serverPort = 0;
        std::mutex sendMutex; // THÊM KHÓA CHO SERVER
        mutable std::mutex stateMutex;
        uint8_t m_sessionKey[32] = {0};
        std::atomic<bool> m_hasSessionKey{false};
        std::string clientIP = "";

    public:
        TcpServer();
        ~TcpServer();

        TcpServer(const TcpServer&) = delete;
        TcpServer& operator=(const TcpServer&) = delete;

        bool Start(int port);
        bool AcceptConnection(int timeoutMs = -1);
        void Close();
        void CloseClient();
        
        //a char = 1 byte, basically a stream of bytes
        //pointer is for memory efficency, not forcing the cpu to clone the data
        bool SendPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool SendEncryptedPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool SendRaw(const char* data, int length);
        bool ReceiveExact(char* buffer, int length);

        void SetSessionKey(const uint8_t key[32]) {
            std::lock_guard<std::mutex> lock(stateMutex);
            std::memcpy(m_sessionKey, key, 32);
            m_hasSessionKey.store(true, std::memory_order_release);
        }
        void ClearSessionKey() {
            std::lock_guard<std::mutex> lock(stateMutex);
            m_hasSessionKey.store(false, std::memory_order_release);
            std::memset(m_sessionKey, 0, 32);
        }
        bool HasSessionKey() const { return m_hasSessionKey.load(std::memory_order_acquire); }
        bool CopySessionKey(uint8_t outKey[32]) const {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (!m_hasSessionKey.load(std::memory_order_acquire)) return false;
            std::memcpy(outKey, m_sessionKey, 32);
            return true;
        }

        SOCKET GetClientSocket() const { return clientSocket.load(); }
        std::string GetClientIP() const;
        int GetPort() const { return serverPort; }
};

//the client (control the machine)
class TcpClient{
    private:
        std::atomic<SOCKET> connectSocket{INVALID_SOCKET};
        std::mutex sendMutex; // THÊM KHÓA CHO CLIENT
        mutable std::mutex stateMutex;
        uint8_t m_sessionKey[32] = {0};
        std::atomic<bool> m_hasSessionKey{false};

    public:
        TcpClient();
        ~TcpClient();

        TcpClient(const TcpClient&) = delete;
        TcpClient& operator=(const TcpClient&) = delete;

        bool Connect(const std::string& ip, int port);
        void Close();
        
        bool SendPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool SendEncryptedPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool SendRaw(const char* data, int length);
        bool ReceiveExact(char* buffer, int length);

        void SetSessionKey(const uint8_t key[32]) {
            std::lock_guard<std::mutex> lock(stateMutex);
            std::memcpy(m_sessionKey, key, 32);
            m_hasSessionKey.store(true, std::memory_order_release);
        }
        void ClearSessionKey() {
            std::lock_guard<std::mutex> lock(stateMutex);
            m_hasSessionKey.store(false, std::memory_order_release);
            std::memset(m_sessionKey, 0, 32);
        }
        bool HasSessionKey() const { return m_hasSessionKey.load(std::memory_order_acquire); }
        bool CopySessionKey(uint8_t outKey[32]) const {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (!m_hasSessionKey.load(std::memory_order_acquire)) return false;
            std::memcpy(outKey, m_sessionKey, 32);
            return true;
        }

        SOCKET GetConnectSocket() const { return connectSocket.load(); }
};