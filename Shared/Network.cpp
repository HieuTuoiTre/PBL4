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
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR){
        Close();
        return false;
    }

    return listen(listenSocket, SOMAXCONN) != SOCKET_ERROR;
}

bool TcpServer::AcceptConnection(){

}

void TcpServer::Close(){

}

bool TcpServer::SendPacket(Protocol::MessageType type, const char* payload, int payloadSize){

}

bool ReceiveExact(char* buffer, int length){

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
