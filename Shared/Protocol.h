#pragma once

#include <cstdint>

//uint8_t = 1 byte

namespace Protocol
{
    constexpr int PORT_CONTROL          = 8080;
    constexpr int PORT_VIDEO            = 8081;
    constexpr int PORT_AUDIO            = 8082;
    constexpr int PORT_DISCOVERY_MGR    = 8088; // Manager lắng nghe phản hồi (HELLO, BYE, RESPONSE)
    constexpr int PORT_DISCOVERY_CLIENT = 8089; // Client lắng nghe yêu cầu quét (QUERY)

    enum DiscoveryType : uint8_t {
        DISCOVERY_QUERY = 1,    // Manager -> Broadcast: Hỏi máy nào đang online
        DISCOVERY_RESPONSE = 2, // Client -> Manager: Phản hồi thông tin máy
        DISCOVERY_HELLO = 3,    // Client -> Broadcast: Thông báo vừa khởi động
        DISCOVERY_BYE = 4       // Client -> Broadcast: Thông báo chuẩn bị tắt app
    };

    enum MessageType : uint8_t {
        //connection
        MSG_PING = 1,        //request
        MSG_PONG,            //reponse
        MSG_DISCONNECT,

        //video/audio
        MSG_VIDEO_FRAME,
        MSG_AUDIO_CHUNK,

        //mouse/keyboard control
        MSG_MOUSE_EVENT,
        MSG_KEY_EVENT,

        //task manager
        MSG_SYS_INFO_REQUEST,
        MSG_SYS_INFO_RESPONSE,
        MSG_PROCESS_LIST_REQUEST,
        MSG_PROCESS_LIST_RESPONSE,
        MSG_KILL_PROCESS,

        //file transfer
        MSG_DRIVE_LIST_REQUEST,
        MSG_DRIVE_LIST_RESPONSE,
        MSG_DIR_REQUEST,      //request to see path content  
        MSG_DIR_RESPONSE,     //response of path conten
        MSG_FILE_DOWNLOAD_REQ,
        MSG_FILE_INFO,        //name, size of file
        MSG_FILE_CHUNK,
        MSG_FILE_END,

        //in-session chat
        MSG_CHAT_MESSAGE
    };

    enum MouseEventType : uint8_t {
        MOUSE_ACTION_MOVE = 1,
        MOUSE_ACTION_PRESS, 
        MOUSE_ACTION_RELEASE,
        MOUSE_ACTION_WHEEL
    };

    enum MouseButtonType : uint8_t {
        BUTTON_NONE = 0,
        BUTTON_LEFT,
        BUTTON_RIGHT,
        BUTTON_MIDDLE
    };

    #pragma pack(push, 1)

    struct DiscoveryPacket {
        char magic[4];         // "LAND"
        uint8_t type;          // DiscoveryType
        uint8_t isBusy;        // 0: Sẵn sàng, 1: Đang có người điều khiển
        char computerName[64]; // Tên máy tính (UTF-8)
        char userName[64];     // Tên tài khoản đang đăng nhập (UTF-8)
    };

    struct PacketHeader{ //total size = type (1 byte) + content (4 byte) = 5 byte
        MessageType type;
        uint32_t size; // 4 byte, 2 byte (uint16_t) would be too small; while 16 byte (uint64_t) wouldve been too large and unneeded
    };

    struct MousePayload{
        uint8_t actionType;
        uint8_t buttonType;
        int16_t x;
        int16_t y;
        int16_t wheelDelta;
    };

    struct KeyboardPayload{
        uint32_t vkCode;
        uint8_t isDown; //down = pressed, vice versa
    };

    struct SysInfoPayload{
        uint8_t cpuUsage;
        uint8_t ramUsage;
        uint8_t diskUsage;
    };

    struct ProcessInfoPayload{
        uint32_t pid;
        uint32_t ramUsageMB;
        uint32_t diskUsageMB;
        char name[256];
    };

    struct FileInfoPayload{
        uint64_t fileSize;
        char fileName[256];
    };

    struct VideoDeltaHeader {
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    };

    struct ChatMessagePayload {
        char sender[32];      // "Quản lý" (Manager) hoặc "Máy khách" (Client)
        char message[1024];   // Nội dung tin nhắn UTF-8
        uint64_t timestamp;   // Thời gian gửi (Unix ms hoặc tick)
    };
    
    #pragma pack(pop)
}   
