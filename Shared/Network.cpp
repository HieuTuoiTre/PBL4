#include "Network.h"

// --- Khởi tạo và Dọn dẹp Winsock ---
bool Network::Initialize() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void Network::Cleanup() {
    WSACleanup();
}

// --- TCP Server (Dành cho máy bị điều khiển) ---
bool TcpServer::Start(int port) {
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) return false;

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Lắng nghe mọi IP
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) return false;
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) return false;
    
    return true;
}

bool TcpServer::AcceptClient() {
    clientSocket = accept(serverSocket, NULL, NULL);
    return clientSocket != INVALID_SOCKET;
}

bool TcpServer::SendData(const char* data, int length) {
    return send(clientSocket, data, length, 0) != SOCKET_ERROR;
}

int TcpServer::ReceiveData(char* buffer, int bufferSize) {
    return recv(clientSocket, buffer, bufferSize, 0);
}

void TcpServer::Close() {
    if (clientSocket != INVALID_SOCKET) closesocket(clientSocket);
    if (serverSocket != INVALID_SOCKET) closesocket(serverSocket);
}

// --- TCP Client (Dành cho máy điều khiển) ---
bool TcpClient::Connect(const std::string& ip, int port) {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

    return connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) != SOCKET_ERROR;
}

bool TcpClient::SendData(const char* data, int length) {
    return send(sock, data, length, 0) != SOCKET_ERROR;
}

int TcpClient::ReceiveData(char* buffer, int bufferSize) {
    return recv(sock, buffer, bufferSize, 0);
}

void TcpClient::Close() {
    if (sock != INVALID_SOCKET) closesocket(sock);
}