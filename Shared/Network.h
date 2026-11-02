#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <mutex>
#include <atomic>
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
        bool ReceiveExact(char* buffer, int length);
        bool CanSendWithoutBlocking();

        SOCKET GetClientSocket() const { return clientSocket.load(); }
        int GetPort() const { return serverPort; }
};

//the client (control the machine)
class TcpClient{
    private:
        std::atomic<SOCKET> connectSocket{INVALID_SOCKET};
        std::mutex sendMutex; // THÊM KHÓA CHO CLIENT

    public:
        TcpClient();
        ~TcpClient();

        TcpClient(const TcpClient&) = delete;
        TcpClient& operator=(const TcpClient&) = delete;

        bool Connect(const std::string& ip, int port);
        void Close();
        
        bool SendPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool ReceiveExact(char* buffer, int length);

        SOCKET GetConnectSocket() const { return connectSocket.load(); }
};
