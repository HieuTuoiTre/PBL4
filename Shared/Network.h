#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <mutex>
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
        //invalid socket so it doesnt set the value to be a random value
        SOCKET listenSocket = INVALID_SOCKET;   //forward to clientSocket
        SOCKET clientSocket = INVALID_SOCKET;   //actual socket used by the client
        std::mutex sendMutex; // THÊM KHÓA CHO SERVER

    public:
        TcpServer();
        ~TcpServer();

        bool Start(int port);
        bool AcceptConnection();
        void Close();
        void CloseClient();
        
        //a char = 1 byte, basically a stream of bytes
        //pointer is for memory efficency, not forcing the cpu to clone the data
        bool SendPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool ReceiveExact(char* buffer, int length);
};

//the client (control the machine)
class TcpClient{
    private:
        SOCKET connectSocket = INVALID_SOCKET;
        std::mutex sendMutex; // THÊM KHÓA CHO CLIENT

    public:
        TcpClient();
        ~TcpClient();

        bool Connect(const std::string& ip, int port);
        void Close();
        
        bool SendPacket(Protocol::MessageType type, const char* payload, int payloadSize);
        bool ReceiveExact(char* buffer, int length);
};