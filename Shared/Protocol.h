#pragma once

#include <cstdint>

/**
 * ============================================================================
 * KIẾN TRÚC GIAO THỨC LANREMOTE (PBL4)
 * ============================================================================
 * 1. TẦNG REMOTE DESKTOP (Màn hình, Chuột, Bàn phím):
 *    - Đã được chuẩn hóa 100% theo giao thức công nghiệp VNC / RFB 3.8 (RFC 6143)
 *      trên cổng chuẩn 5900 (Xem chi tiết tại: Shared/RFB/).
 *    - Toàn bộ gói tin bắt tay, FramebufferUpdate (Tight/JPEG), PointerEvent,
 *      KeyEvent được định nghĩa chuẩn tại RfbMessages.h.
 *
 * 2. TẦNG DỊCH VỤ QUẢN TRỊ & MẠNG (Management & Utility Services):
 *    - Được định nghĩa trong file Protocol.h này, phục vụ các tính năng mở rộng:
 *      + UDP Discovery: Tự động quét và nhận diện máy trạm trong mạng LAN.
 *      + Quản lý tiến trình (Task Manager) & Giám sát CPU/RAM/Disk.
 *      + Quản lý và truyền tải tệp tin (File Explorer) kèm xác thực toàn vẹn CRC32.
 *      + Hộp thoại trò chuyện hai chiều (In-session Chat) & Đồng bộ Clipboard.
 *      + Xác thực phiên (Session Authentication) & Mã hóa kênh điều khiển.
 *      + Luồng âm thanh thời gian thực (Audio Streaming).
 * ============================================================================
 */

namespace Protocol
{
    // --- 1. CỔNG MẠNG DỊCH VỤ QUẢN TRỊ ---
    constexpr int PORT_CONTROL          = 8080; // Kênh Quản trị hệ thống (File, Task Manager, Chat, Clipboard)
    constexpr int PORT_AUDIO            = 8082; // Kênh Âm thanh thời gian thực
    constexpr int PORT_DISCOVERY_MGR    = 8088; // Manager lắng nghe phản hồi UDP Broadcast (HELLO, BYE, RESPONSE)
    constexpr int PORT_DISCOVERY_CLIENT = 8089; // Client lắng nghe yêu cầu quét UDP Broadcast (QUERY)

    // --- 2. GIAO THỨC TỰ ĐỘNG PHÁT HIỆN MẠNG LAN (UDP DISCOVERY) ---
    enum DiscoveryType : uint8_t {
        DISCOVERY_QUERY = 1,    // Manager -> Broadcast: Hỏi máy nào đang online
        DISCOVERY_RESPONSE = 2, // Client -> Manager: Phản hồi thông tin máy
        DISCOVERY_HELLO = 3,    // Client -> Broadcast: Thông báo vừa khởi động
        DISCOVERY_BYE = 4       // Client -> Broadcast: Thông báo chuẩn bị tắt app
    };

    // --- 3. ĐỊNH DANH THÔNG ĐIỆP KÊNH QUẢN TRỊ (PORT 8080) ---
    enum MessageType : uint8_t {
        // Kết nối cơ bản
        MSG_PING = 1,
        MSG_PONG,
        MSG_DISCONNECT,

        // Âm thanh
        MSG_AUDIO_CHUNK = 5,

        // Quản lý tác vụ & Hệ thống (Task Manager)
        MSG_SYS_INFO_REQUEST = 8,
        MSG_SYS_INFO_RESPONSE,
        MSG_PROCESS_LIST_REQUEST,
        MSG_PROCESS_LIST_RESPONSE,
        MSG_KILL_PROCESS,

        // Quản lý tệp tin (File Explorer)
        MSG_DRIVE_LIST_REQUEST = 13,
        MSG_DRIVE_LIST_RESPONSE,
        MSG_DIR_REQUEST,
        MSG_DIR_RESPONSE,
        MSG_FILE_DOWNLOAD_REQ,
        MSG_FILE_INFO,
        MSG_FILE_CHUNK,
        MSG_FILE_END,

        // Trò chuyện nội bộ (In-session Chat)
        MSG_CHAT_MESSAGE = 21,

        // Xác thực bảo mật phiên (Session Authentication)
        MSG_AUTH_REQUEST = 22,
        MSG_AUTH_RESPONSE = 23,

        // Gói tin mã hóa bảo vệ kênh quản trị
        MSG_ENCRYPTED_CONTROL = 24,

        // Các tính năng mở rộng
        MSG_CLIPBOARD_TEXT = 25,
        MSG_FILE_UPLOAD_REQ = 26,
        MSG_FILE_BATCH_START = 27,
        MSG_FILE_BATCH_END = 28,
        MSG_FILE_CANCEL = 29,
        MSG_FILE_ERROR = 30,
        MSG_SPECIAL_ACTION = 31,
        MSG_VIDEO_CONFIG = 32
    };

    // --- 4. HẰNG SỐ VÀ ENUM ĐIỀU KHIỂN CHUỘT / PHÍM NỘI BỘ ---
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

    // --- 5. CẤU TRÚC GÓI TIN ĐIỀU KHIỂN CHUẨN ---
    struct PacketHeader {
        MessageType type; // 1 byte
        uint32_t size;    // 4 byte độ dài payload
    };

    // --- 6. XÁC THỰC PHIÊN BẢO MẬT (SESSION AUTHENTICATION) ---
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
        char sessionToken[64];
        uint32_t clientVersion;
    };

    struct AuthResponsePayload {
        uint8_t status;
        char message[64];
    };

    // --- 7. PHÍM TẮT ĐẶC BIỆT ---
    enum SpecialActionType : uint8_t {
        ACTION_CTRL_ALT_DEL = 1,
        ACTION_WIN_KEY      = 2,
        ACTION_LOCK_SCREEN  = 3
    };

    struct SpecialActionPayload {
        uint8_t actionType;
    };

    // --- 8. CẤU HÌNH CHẤT LƯỢNG HÌNH ẢNH DYNAMIC TURBOJPEG ---
    enum VideoQualityPreset : uint8_t {
        QUALITY_AUTO     = 0,
        QUALITY_LOW      = 1,
        QUALITY_BALANCED = 2,
        QUALITY_HIGH     = 3
    };

    struct VideoConfigPayload {
        uint8_t qualityPreset;
        uint8_t targetFps;
    };

    // --- 9. TRUYỀN TẢI TỆP TIN & TOÀN VẸN DỮ LIỆU (FILE TRANSFER) ---
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
        uint8_t isDirectory;
        char relativePath[512];
    };

    struct FileChunkHeader {
        uint32_t chunkIndex;
        uint32_t dataLength;
    };

    struct FileEndPayload {
        uint32_t fileIndex;
        uint32_t finalCrc32;
        uint8_t status;
    };

    struct FileInfoPayload {
        uint64_t fileSize;
        char fileName[256];
    };

    // --- 10. GÓI TIN QUÉT MẠNG LAN (DISCOVERY PACKET) ---
    struct DiscoveryPacket {
        char magic[4];         // "LAND"
        uint8_t type;          // DiscoveryType
        uint8_t isBusy;        // 0: Sẵn sàng, 1: Đang bận
        char computerName[64];
        char userName[64];
    };

    // --- 11. CẤU TRÚC TIÊM LỆNH CHUỘT / PHÍM NỘI BỘ (INPUT ENGINE DTO) ---
    struct MousePayload {
        uint8_t actionType;
        uint8_t buttonType;
        int16_t x;
        int16_t y;
        int16_t wheelDelta;
    };

    struct KeyboardPayload {
        uint32_t vkCode;
        uint8_t isDown;
    };

    // --- 12. QUẢN LÝ TIẾN TRÌNH & THÔNG TIN HỆ THỐNG ---
    struct SysInfoPayload {
        uint8_t cpuUsage;
        uint8_t ramUsage;
        uint8_t diskUsage;
    };

    struct ProcessInfoPayload {
        uint32_t pid;
        uint32_t ramUsageMB;
        uint32_t diskUsageMB;
        char name[256];
    };

    // --- 13. TỌA ĐỘ VÙNG THAY ĐỔI MÀN HÌNH (DIRTY RECTANGLE HEADER) ---
    struct VideoDeltaHeader {
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    };

    // --- 14. TRÒ CHUYỆN NỘI BỘ (CHAT) ---
    struct ChatMessagePayload {
        char sender[32];
        char message[1024];
        uint64_t timestamp;
    };
    
    #pragma pack(pop)
}
