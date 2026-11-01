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
        MSG_CHAT_MESSAGE = 21,

        // Authentication (M1)
        MSG_AUTH_REQUEST = 22,
        MSG_AUTH_RESPONSE = 23,

        // Encryption Wrapper (M1)
        MSG_ENCRYPTED_CONTROL = 24,

        // Extensions (M2, M3, M4)
        MSG_CLIPBOARD_TEXT = 25,
        MSG_FILE_UPLOAD_REQ = 26,
        MSG_FILE_BATCH_START = 27,
        MSG_FILE_BATCH_END = 28,
        MSG_FILE_CANCEL = 29,
        MSG_FILE_ERROR = 30,
        MSG_SPECIAL_ACTION = 31,
        MSG_VIDEO_CONFIG = 32
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

    constexpr uint32_t PROTOCOL_VERSION = 1;
    constexpr const char* DEFAULT_AUTH_TOKEN = "PBL4_REMOTE_DESKTOP_SECRET_2026";

    enum AuthStatus : uint8_t {
        AUTH_SUCCESS = 0,
        AUTH_INVALID_TOKEN = 1,
        AUTH_REJECTED = 2,
        AUTH_TIMEOUT = 3,
        AUTH_VERSION_MISMATCH = 4
    };

    struct AuthRequestPayload {
        char sessionToken[64]; // SHA-256 hex or pre-shared key (null-terminated)
        uint32_t clientVersion; // Protocol version (e.g. PROTOCOL_VERSION)
    };

    struct AuthResponsePayload {
        uint8_t status;        // AuthStatus enum (0 = Success, 1 = Invalid, etc.)
        char message[64];      // Human-readable status/error description (null-terminated)
    };

    enum SpecialActionType : uint8_t {
        ACTION_CTRL_ALT_DEL = 1,
        ACTION_WIN_KEY      = 2,
        ACTION_LOCK_SCREEN  = 3
    };

    struct SpecialActionPayload {
        uint8_t actionType; // SpecialActionType
    };

    enum VideoQualityPreset : uint8_t {
        QUALITY_AUTO     = 0,
        QUALITY_LOW      = 1,
        QUALITY_BALANCED = 2,
        QUALITY_HIGH     = 3
    };

    struct VideoConfigPayload {
        uint8_t qualityPreset; // VideoQualityPreset
        uint8_t targetFps;     // 30 or 60
    };

    constexpr uint32_t FILE_CHUNK_SIZE = 64 * 1024;

    enum FileTransferStatus : uint8_t {
        FILE_STATUS_SUCCESS = 0,
        FILE_STATUS_ERROR   = 1,
        FILE_STATUS_CANCEL  = 2
    };

    struct FileBatchStartPayload {
        uint32_t totalFiles;
        uint64_t totalBytes;
        char rootName[256];
    };

    struct FileHeaderPayload {
        uint64_t fileSize;
        uint32_t expectedCrc32;
        uint32_t totalChunks;
        uint8_t isDirectory;      // 1 = dir, 0 = file
        char relativePath[512];   // UTF-8 relative path
    };

    struct FileChunkHeader {
        uint32_t chunkIndex;
        uint32_t dataLength;
    };

    struct FileEndPayload {
        uint32_t fileIndex;
        uint32_t finalCrc32;
        uint8_t status;           // FileTransferStatus: 0 = Success, 1 = Error, 2 = Cancelled
    };

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
