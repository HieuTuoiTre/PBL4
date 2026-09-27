#include "Network.h"
#include "Protocol.h"
#include <iostream>

//Network
bool Network::Initialize(){ 
    WSADATA wsadata;
    return WSAStartup(MAKEWORD(2,2), &wsadata);
}

void Network::Cleanup(){
    WSACleanup();
}

//Tcp server
TcpServer::TcpServer() {}

TcpServer::~TcpServer() {
    Close();
}

bool TcpServer::Start(int port){
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET){
        return false;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;             //use ipv4
    serverAddress.sin_addr.s_addr = INADDR_ANY;     //allow connection to any kind of ip (ethernet/lan)
    serverAddress.sin_port = htons(port);           //at port

    //actually create the socket at the address and the port
    //socket error is returned if the port is already in use
    if (bind(listenSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR){
        Close();
        return false;
    }

    //SOMAXCONN allow multiple connection at the same time (view/audio/control)    
    int result = listen(listenSocket, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        return false;
    }
    return true;
}

bool TcpServer::AcceptConnection(){
    clientSocket = accept(listenSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET){
        return false;
    }
    return true;
}

void TcpServer::Close(){
    if (clientSocket != INVALID_SOCKET){
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }

    if (listenSocket != INVALID_SOCKET){
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
}

bool TcpServer::SendPacket(Protocol::MessageType type, const char* payload, int payloadSize){
    if (clientSocket == INVALID_SOCKET){
        return false;
    }

    Protocol::PacketHeader header;
    header.type = type;
    header.size = payloadSize;

    if (send(clientSocket, (const char*)&header, sizeof(Protocol::PacketHeader), 0) == SOCKET_ERROR){
        return false;
    }

    if (payloadSize > 0){
        int byteSent = 0;
        while (byteSent < payloadSize) {
            int result = send(clientSocket, payload + byteSent, payloadSize - byteSent, 0);
            if (result == SOCKET_ERROR){
                return false;
            }
            byteSent += result;
        }
    }

    return true;
}

bool TcpServer::ReceiveExact(char* buffer, int length){
    if (clientSocket == INVALID_SOCKET){
        return false;
    }

    int byteReceived = 0;
    while (byteReceived < length){
        int result = recv(clientSocket, buffer + byteReceived, length - byteReceived, 0);
        if (result <= 0){
            return false;
        }
        byteReceived += result;
    }

    return true;
}

//Tcp client
TcpClient::TcpClient(){}

TcpClient::~TcpClient(){
    Close();
}

bool Connect(const std::string& ip, int port){

}

bool SendPacket(Protocol::MessageType type, const char* payload, int payloadSize){

}

 bool ReceiveExact(char* buffer, int length){

 }
