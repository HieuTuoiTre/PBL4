#include "Network.h"
#include "Protocol.h"
#include <iostream>
#include <mstcpip.h>

namespace {
    // Hàm tối ưu hóa Socket nâng cao theo đúng thiết kế mục 7.2
    void ApplySocketOptimizations(SOCKET sock, int port) {
        if (sock == INVALID_SOCKET) return;

        // 1. Vô hiệu hóa thuật toán Nagle (TCP_NODELAY) để truyền sự kiện tức thời 0ms
        BOOL nodelay = TRUE;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        // 2. Phân cấp kích thước bộ đệm SO_SNDBUF & SO_RCVBUF theo từng kênh:
        //    - Kênh Video (8081): 1MB (1024 * 1024) chống nghẽn buffer khi khung hình JPEG lớn ở 60 FPS
        //    - Kênh Audio (8082): 64KB (64 * 1024) phát tiếng tức thì, tránh trễ tiếng so với hình
        //    - Kênh Control (8080) & khác: 256KB (256 * 1024) đủ lớn cho file transfer và tối ưu chuột/phím
        int bufSize = 256 * 1024;
        if (port == Protocol::PORT_VIDEO) {
            bufSize = 1024 * 1024;
        } else if (port == Protocol::PORT_AUDIO) {
            bufSize = 64 * 1024;
        }
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&bufSize, sizeof(bufSize));
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(bufSize));

        // 3. Giữ kết nối & phát hiện đứt mạng ngầm (SO_KEEPALIVE & SIO_KEEPALIVE_VALS)
        BOOL keepAlive = TRUE;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));

        tcp_keepalive aliveSettings = {};
        aliveSettings.onoff = 1;
        aliveSettings.keepalivetime = 5000;     // 5s không có dữ liệu -> bắt đầu gửi probe
        aliveSettings.keepaliveinterval = 1000; // Gửi lại mỗi 1s nếu chưa có phản hồi
        DWORD bytesReturned = 0;
        WSAIoctl(sock, SIO_KEEPALIVE_VALS, &aliveSettings, sizeof(aliveSettings), NULL, 0, &bytesReturned, NULL, NULL);
    }
}

//Network
bool Network::Initialize(){ 
    WSADATA wsadata;
    // Thêm "== 0" để so sánh. Nếu WSAStartup trả về 0 (thành công) thì hàm này sẽ return true.
    return WSAStartup(MAKEWORD(2,2), &wsadata) == 0;
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
    boundPort = port;
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

bool TcpServer::AcceptConnection(int timeoutMs){
    if (listenSocket == INVALID_SOCKET){
        return false;
    }

    if (timeoutMs >= 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenSocket, &fds);
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int result = select(0, &fds, NULL, NULL, &tv);
        if (result <= 0) {
            return false;
        }
    }

    clientSocket = accept(listenSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET){
        return false;
    }

    // Áp dụng bộ cấu hình tối ưu socket chuyên biệt theo từng kênh
    ApplySocketOptimizations(clientSocket, boundPort);
    return true;
}

void TcpServer::Close(){
    std::lock_guard<std::mutex> lock(sendMutex);
    if (clientSocket != INVALID_SOCKET){
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }

    if (listenSocket != INVALID_SOCKET){
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
    boundPort = 0;
}

void TcpServer::CloseClient(){
    std::lock_guard<std::mutex> lock(sendMutex);
    // Chỉ đóng socket đang giao tiếp với Manager cũ
    if (clientSocket != INVALID_SOCKET){
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
}

bool TcpServer::SendPacket(Protocol::MessageType type, const char* payload, int payloadSize){
    // Khóa mutex trước để đảm bảo tính an toàn đa luồng, sau đó mới kiểm tra trạng thái socket
    std::lock_guard<std::mutex> lock(sendMutex);

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

bool TcpServer::CanSendWithoutBlocking(){
    if (clientSocket == INVALID_SOCKET){
        return false;
    }
    fd_set writeFds;
    FD_ZERO(&writeFds);
    FD_SET(clientSocket, &writeFds);
    timeval tv = { 0, 0 }; // 0 ms: Polling tức thì
    int result = select(0, NULL, &writeFds, NULL, &tv);
    return (result > 0);
}

//Tcp client
TcpClient::TcpClient(){}

TcpClient::~TcpClient(){
    Close();
}

bool TcpClient::Connect(const std::string& ip, int port){
    connectedPort = port;
    connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (connectSocket == INVALID_SOCKET){
        return false;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) <= 0){
        Close();
        return false;
    }

    // Đặt socket sang chế độ Non-blocking để kiểm soát thời gian chờ (Timeout 4s)
    u_long nonBlocking = 1;
    ioctlsocket(connectSocket, FIONBIO, &nonBlocking);

    int connResult = connect(connectSocket, (sockaddr*)&serverAddress, sizeof(serverAddress));
    if (connResult == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            fd_set writeFds, errFds;
            FD_ZERO(&writeFds); FD_SET(connectSocket, &writeFds);
            FD_ZERO(&errFds);   FD_SET(connectSocket, &errFds);

            timeval tv = { 4, 0 }; // Chờ tối đa 4 giây thay vì treo 21 giây
            int sel = select(0, NULL, &writeFds, &errFds, &tv);
            if (sel <= 0) {
                Close();
                return false;
            }

            int so_err = 0;
            int errLen = sizeof(so_err);
            getsockopt(connectSocket, SOL_SOCKET, SO_ERROR, (char*)&so_err, &errLen);
            if (so_err != 0) {
                Close();
                return false;
            }
        } else {
            Close();
            return false;
        }
    }

    // Trả lại chế độ Blocking thông thường để truyền nhận ổn định
    u_long blocking = 0;
    ioctlsocket(connectSocket, FIONBIO, &blocking);

    // Áp dụng bộ cấu hình tối ưu socket chuyên biệt theo từng kênh
    ApplySocketOptimizations(connectSocket, port);

    return true;
}

void TcpClient::Close(){
    std::lock_guard<std::mutex> lock(sendMutex);
    if (connectSocket != INVALID_SOCKET){
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
    }
    connectedPort = 0;
}

bool TcpClient::SendPacket(Protocol::MessageType type, const char* payload, int payloadSize){
    // Khóa mutex trước để đảm bảo tính an toàn đa luồng, sau đó mới kiểm tra trạng thái socket
    std::lock_guard<std::mutex> lock(sendMutex);

    if (connectSocket == INVALID_SOCKET){
        return false;
    }

    Protocol::PacketHeader header;
    header.type = type;
    header.size = payloadSize;

    if (send(connectSocket, (const char*)&header, sizeof(Protocol::PacketHeader), 0) == SOCKET_ERROR) {
        return false;
    }

    int byteSent = 0;
    while (byteSent < payloadSize){
        int result = send(connectSocket, payload + byteSent, payloadSize - byteSent, 0);
        if (result == SOCKET_ERROR){
            return false;
        }
        byteSent += result;
    }

    return true;
}

bool TcpClient::ReceiveExact(char* buffer, int length){
    if (connectSocket == INVALID_SOCKET){
        return false;
    }

    int byteReceived = 0;
    while (byteReceived < length){
        int result = recv(connectSocket, buffer + byteReceived, length - byteReceived, 0);
        if (result <= 0){
            return false;
        }
        byteReceived += result;
    }

    return true;
}
