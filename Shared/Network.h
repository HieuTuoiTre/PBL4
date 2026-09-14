#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <iostream>

class Network {
public:
    static bool Initialize();
    static void Cleanup();
};

class TcpServer {
private:
    SOCKET serverSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;
public:
    bool Start(int port);
    bool AcceptClient();
    bool SendData(const char* data, int length);
    int ReceiveData(char* buffer, int bufferSize);
    void Close();
};

class TcpClient {
private:
    SOCKET sock = INVALID_SOCKET;
public:
    bool Connect(const std::string& ip, int port);
    bool SendData(const char* data, int length);
    int ReceiveData(char* buffer, int bufferSize);
    void Close();
};