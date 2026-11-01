# Project: PBL4 - C++ Remote Desktop Application (Advanced Features Optimization & Completion)

## Architecture

### System Topology & Ports
- **Client.exe** acts as the controlled endpoint (TCP Server) listening on:
  - Port 8080 (`PORT_CONTROL`): Bidirectional Control Stream (commands, input, auth, clipboard, file transfer, chat)
  - Port 8081 (`PORT_VIDEO`): Unidirectional Video Stream (JPEG frames via TurboJPEG)
  - Port 8082 (`PORT_AUDIO`): Unidirectional Audio Stream (24kHz Mono PCM via WASAPI)
  - Port 8089 (`PORT_DISCOVERY_CLIENT`): UDP broadcast discovery responder
- **Manager.exe** acts as the controller endpoint (TCP Client) connecting to Client:
  - Connects to Port 8080, then 8081, then 8082
  - Port 8088 (`PORT_DISCOVERY_MGR`): UDP broadcast discovery scanner

### Framing & Packet Serialization
- Header: 5-byte packed struct (`PacketHeader`):
  ```cpp
  #pragma pack(push, 1)
  struct PacketHeader {
      uint8_t type;  // MessageType enum
      uint32_t size; // Little-Endian payload size
  };
  #pragma pack(pop)
  ```
- Send synchronization: Sockets serialize packet header + payload atomically via `std::mutex sendMutex`.
- Framing boundary: Variable length payload follows `PacketHeader` directly and is received via `ReceiveExact(buffer, size)`.

### Threading Model
- **Manager**:
  - UI Thread: Win32 message pump (`ManagerMain.cpp`), rendering viewport (`RemoteWindow.cpp`), toolbar GDI+ drawing
  - Worker Threads: `DataReceiver.cpp` runs dedicated threads for Control, Video, and Audio streams
- **Client**:
  - UI Thread: Win32 message pump (`ClientMain.cpp`), tray icon, status overlay
  - Supervisor Thread: Listens and accepts connections on 8080/8081/8082
  - Worker Threads: `TransmitVideoLoop` (60 FPS pacer), `ReceiveControlLoop` (control socket read loop), WASAPI audio capture thread

---

## Feature Inventory

Every feature identified during the Survey phase is enumerated below and mapped to a specific milestone.

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | F1: Session Authentication Handshake | Mutual/challenge-response auth on port 8080 immediately after TCP accept; reject unauthorized connections before video/audio streams | M1 | Survey 1, ORIGINAL_REQUEST §R4 |
| 2 | F2: Control Stream Packet Encryption | Symmetric encryption wrapper (`MSG_ENCRYPTED_CONTROL`) using ChaCha20/AES protecting sensitive control commands, keystrokes, clipboard, file requests | M1 | Survey 1, ORIGINAL_REQUEST §R4 |
| 3 | F3: Protocol Definition Expansion | Add MessageType enums & payload structs for Auth, Crypto, Clipboard, File Batch/Cancel/Error, Special Actions, and Video Config | M1 | Survey 1 & 2, ORIGINAL_REQUEST §R1-R4 |
| 4 | F4: Bidirectional Clipboard Sync Engine | UTF-8 text synchronization using Windows `AddClipboardFormatListener` / `WM_CLIPBOARDUPDATE`, `CF_UNICODETEXT`, and UTF-8 conversion | M2 | Survey 2, ORIGINAL_REQUEST §R1 |
| 5 | F5: Clipboard Echo Loop Prevention | Dual-tier protection: re-entrancy suppression flag (`isSettingClipboard`) + content hash / text cache check (`lastSyncedCrc`) | M2 | Survey 2, ORIGINAL_REQUEST §R1 |
| 6 | F6: Chunked File Transfer Engine | Bidirectional chunked file upload and download with 64KB chunks, non-blocking flow, and streaming disk I/O | M3 | Survey 2, ORIGINAL_REQUEST §R2 |
| 7 | F7: Full Directory Tree Transfer | Recursive directory traversal via `std::filesystem::recursive_directory_iterator`, relative path serialization, destination directory creation | M3 | Survey 2, ORIGINAL_REQUEST §R2 |
| 8 | F8: Data Integrity Verification | Incremental CRC32 checksum computation during chunk streaming, validation against expected hash, rejection of corrupted/partial `.part` files | M3 | Survey 2, ORIGINAL_REQUEST §R2 |
| 9 | F9: File Transfer Cancellation & Error Handling | Atomic cancellation token, `MSG_FILE_CANCEL` packet, graceful connection preservation, safe deletion of temporary `.part` files | M3 | Survey 2, ORIGINAL_REQUEST §R2 |
| 10 | F10: File Explorer UI Enhancements | Real-time session/batch progress bar, speed/status labels, "Tải lên" file & folder buttons, and "Hủy" cancellation button | M3 | Survey 2, ORIGINAL_REQUEST §R2 |
| 11 | F11: Overlay Toolbar UI Expansion | GDI+ rendering of new buttons: Fullscreen toggle, Ctrl+Alt+Del, Windows Key, and Video Quality adjustment | M4 | Survey 3, ORIGINAL_REQUEST §R3 |
| 12 | F12: Borderless Fullscreen Mode | Toggle between windowed and borderless fullscreen using `GetWindowPlacement` / `SetWindowLongPtr` / `MonitorFromWindow`, overlay toolbar positioning at top-center | M4 | Survey 3, ORIGINAL_REQUEST §R3 |
| 13 | F13: Remote Special Shortcuts Injection | Start Menu trigger via atomic `VK_LWIN` tap; Ctrl+Alt+Del injection via tiered execution (`sas.dll` `SendSAS` with graceful `taskmgr.exe` fallback) | M4 | Survey 3, ORIGINAL_REQUEST §R3 |
| 14 | F14: Dynamic Video Quality Adjustment | Manager toolbar selector cycling Auto/Low/Balanced/High, sending `MSG_VIDEO_CONFIG`, Client `ScreenCapture` dynamically adjusting JPEG quality and target FPS | M4 | Survey 3, ORIGINAL_REQUEST §R3 |
| 15 | F15: Standalone Automated Test Suites | Unit tests in `tests/` (`test_crypto_security.cpp`, `test_clipboard.cpp`, `test_file_transfer.cpp`) with test runner | E2E Testing / M5 | Survey 1, 2, 3, ORIGINAL_REQUEST §Acceptance Criteria |
| 16 | F16: E2E Integration Testing & Live Guide | Live verification script and step-by-step testing guide between `Manager.exe` and `Client.exe` | M5 | ORIGINAL_REQUEST §Acceptance Criteria |

---

## Milestones

| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Core Protocol, Session Authentication & Control Stream Encryption | F1, F2, F3: Implement `Crypto.h`/`Crypto.cpp`, extend `Shared/Protocol.h`, implement auth handshake and packet encryption envelope in Manager and Client (Outputs: `Shared/Crypto.*`, `Shared/ControlEnvelope.h`, `Shared/Network.*`, `SessionManager.cpp`, `ManagerMain.cpp`, `tests/test_crypto_security.cpp`, `tests/test_auth_handshake.cpp`, `tests/test_adversarial_m1.cpp`, `Manager.exe`, `Client.exe` all 100% verified) | none | DONE |
| M2 | Bidirectional Clipboard Sync with Anti-Echo Loop | F4, F5: Implement `Shared/ClipboardManager.h`/`.cpp`, integrate with Manager window procedure and Client session listener, implement dual echo prevention (Outputs: `Shared/ClipboardManager.*`, `RemoteWindow.*`, `DataReceiver.cpp`, `TrayIcon.*`, `SessionManager.*`, `tests/test_clipboard.cpp`, `tests/test_adversarial_clipboard_loop.cpp`, `tests/test_adversarial_clipboard_fuzz.cpp`, `tests/test_adversarial_m2.cpp` all 100% verified) | M1 | DONE |
| M3 | Optimized Chunked File & Folder Transfer with Integrity & Cancellation | F6, F7, F8, F9, F10: Implement `CRC32.h`, recursive directory walking/creation, chunked upload/download, cancellation protocol, `.part` cleanup, and UI progress bar | M1, M2 | IN_PROGRESS |
| M4 | Manager Overlay Toolbar, Fullscreen Mode, Special Shortcuts & Video Quality | F11, F12, F13, F14: Add toolbar buttons to `RemoteWindow.cpp`, implement borderless fullscreen toggle, special shortcut injection (`VK_LWIN`, Ctrl+Alt+Del), video quality selector & packet | M1 | PLANNED |
| M5 | E2E Test Suite Validation, Live Integration Guide & Adversarial Hardening | F15, F16: Phase 1: Pass 100% of E2E tests (Tiers 1-4); Phase 2: Adversarial coverage hardening (Tier 5); provide live testing guide | M1, M2, M3, M4, TEST_READY.md | PLANNED |
| E2E | E2E Testing Track (Parallel) | Test harness, test runners, Tier 1-4 test suites for all features, publish `TEST_READY.md` | M1 interface contracts | PLANNED |

---

## Interface Contracts

### 1. Protocol Extensions (`Shared/Protocol.h`)
```cpp
enum MessageType : uint8_t {
    // Existing (1..21)
    MSG_PING = 1,
    MSG_PONG = 2,
    MSG_DISCONNECT = 3,
    MSG_VIDEO_FRAME = 4,
    MSG_AUDIO_CHUNK = 5,
    MSG_MOUSE_EVENT = 6,
    MSG_KEY_EVENT = 7,
    MSG_SYS_INFO_REQUEST = 8,
    MSG_SYS_INFO_RESPONSE = 9,
    MSG_PROCESS_LIST_REQUEST = 10,
    MSG_PROCESS_LIST_RESPONSE = 11,
    MSG_KILL_PROCESS = 12,
    MSG_DRIVE_LIST_REQUEST = 13,
    MSG_DRIVE_LIST_RESPONSE = 14,
    MSG_DIR_REQUEST = 15,
    MSG_DIR_RESPONSE = 16,
    MSG_FILE_DOWNLOAD_REQ = 17,
    MSG_FILE_INFO = 18,
    MSG_FILE_CHUNK = 19,
    MSG_FILE_END = 20,
    MSG_CHAT_MESSAGE = 21,

    // New additions
    MSG_AUTH_REQUEST = 22,
    MSG_AUTH_RESPONSE = 23,
    MSG_ENCRYPTED_CONTROL = 24,
    MSG_CLIPBOARD_TEXT = 25,
    MSG_FILE_UPLOAD_REQ = 26,
    MSG_FILE_BATCH_START = 27,
    MSG_FILE_BATCH_END = 28,
    MSG_FILE_CANCEL = 29,
    MSG_FILE_ERROR = 30,
    MSG_SPECIAL_ACTION = 31,
    MSG_VIDEO_CONFIG = 32
};

// Authentication Payload
struct AuthRequestPayload {
    char sessionToken[64]; // SHA-256 hex or pre-shared key
    uint32_t clientVersion;
};

struct AuthResponsePayload {
    uint8_t status;        // 0 = Success, 1 = Invalid Token, 2 = Rejected
    char message[64];
};

// Special Action Payload
enum SpecialActionType : uint8_t {
    ACTION_CTRL_ALT_DEL = 1,
    ACTION_WIN_KEY      = 2,
    ACTION_LOCK_SCREEN  = 3
};

struct SpecialActionPayload {
    uint8_t actionType; // SpecialActionType
};

// Video Quality Config Payload
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

// File Transfer Batch & Header Payloads
struct FileBatchStartPayload {
    uint32_t totalFiles;
    uint64_t totalBytes;
    char rootName[256];
};

struct FileHeaderPayload {
    uint64_t fileSize;
    uint32_t expectedCrc32;
    uint8_t isDirectory;      // 1 = dir, 0 = file
    char relativePath[512];   // UTF-8 relative path
};

struct FileEndPayload {
    uint32_t finalCrc32;
    uint8_t status;           // 0 = Success, 1 = Error, 2 = Cancelled
};
```

### 2. Crypto Module Interface (`Shared/Crypto.h`)
```cpp
namespace Crypto {
    // Encrypts plaintext buffer using ChaCha20/AES with generated 12-byte IV/nonce
    // Output format: [12-byte IV] + [Ciphertext] + [16-byte Tag]
    bool Encrypt(const uint8_t* key, const uint8_t* plaintext, size_t plainLen, std::vector<uint8_t>& outCipher);

    // Decrypts ciphertext buffer, verifies tag, and extracts plaintext
    bool Decrypt(const uint8_t* key, const uint8_t* ciphertext, size_t cipherLen, std::vector<uint8_t>& outPlain);

    // Generates SHA-256 hash
    void Sha256(const uint8_t* data, size_t len, uint8_t outHash[32]);
    std::string Sha256Hex(const std::string& input);
}
```

### 3. Clipboard Manager Interface (`Shared/ClipboardManager.h`)
```cpp
class ClipboardManager {
public:
    static bool Initialize(HWND hwnd);
    static void Cleanup(HWND hwnd);
    static bool ReadText(HWND hwnd, std::string& outUtf8);
    static bool WriteText(HWND hwnd, const std::string& utf8Text);
    static bool ShouldSync(const std::string& text);
    static void MarkSettingLocal();
    static bool IsSettingLocal();
};
```

### 4. CRC32 Integrity Interface (`Shared/CRC32.h`)
```cpp
class CRC32 {
public:
    static uint32_t Calculate(const void* data, size_t length);
    static uint32_t Update(uint32_t currentCrc, const void* data, size_t length);
};
```

---

## Code Layout & Write Ownership

To ensure parallel safety without write conflicts, file ownership is strictly designated:

| File / Directory | Owner Milestone | Description |
|---|---|---|
| `Shared/Protocol.h` | M1 | Core protocol enums and payload definitions |
| `Shared/Crypto.h` & `Shared/Crypto.cpp` | M1 | Cryptographic algorithms & auth helpers |
| `Manager/Core/ManagerMain.cpp`, `Client/Core/SessionManager.cpp` (Auth & Crypto hooks) | M1 | Authentication handshake & control stream encryption |
| `Shared/ClipboardManager.h` & `Shared/ClipboardManager.cpp` | M2 | Cross-platform Win32 clipboard monitoring and echo-loop prevention |
| `Manager/UI/RemoteWindow.cpp` (Clipboard event hooks) | M2 | Manager clipboard listener registration and message dispatch |
| `Shared/CRC32.h` & `Shared/CRC32.cpp` | M3 | CRC32 incremental checksum calculator |
| `Manager/FileTransfer/*` & `Client/FileTransfer/*` | M3 | Chunked upload/download, directory walking, `.part` management |
| `Manager/UI/FileExplorerWindow.h` & `.cpp` | M3 | File explorer UI, progress bar, cancel & upload buttons |
| `Manager/UI/RemoteWindow.h` & `RemoteWindow.cpp` (Toolbar UI) | M4 | Overlay toolbar buttons, fullscreen toggle, quality selector |
| `Client/System/InputInjector.h` & `InputInjector.cpp` | M4 | Special shortcut handling (VK_LWIN, Ctrl+Alt+Del / taskmgr) |
| `Client/Media/ScreenCapture.h` & `ScreenCapture.cpp` | M4 | Dynamic video quality & FPS preset adjustment |
| `tests/*` | E2E Testing Track / M5 | Standalone unit & integration test suites, runners |
| `build.bat` | E2E Testing Track / M5 | Build script enhancements (test target, running process cleanup) |
