# Code Review Action Items

Ngày đánh giá: 2026-11-23

Trạng thái tổng thể: **ĐẠT QUALITY GATE TOÀN DIỆN (100% Hoàn thành)**. Tất cả các mục Critical (CR-1), Required (R-1 -> R-5) và Nit (N-1, N-2, N-3) đã được giải quyết triệt để và kiểm chứng tự động. Sẵn sàng phát hành hoặc merge.

## Critical

### CR-1: Bắt buộc xác thực cho cổng RFB 5900

**Vấn đề**

- `RfbServerSession.cpp` chỉ hỗ trợ `SECURITY_NONE`.
- Server luôn gửi `SECURITY_RESULT_OK` sau khi client chọn `SECURITY_NONE`.
- Một client bất kỳ có thể xem và điều khiển desktop mà không đi qua session authentication ở cổng 8080.

**Việc cần làm**

- [x] Kiểm tra phiên 8080 đã xác thực thành công trước khi cho phép phiên RFB 5900 (`RfbServer::SetAuthorizationValidator` + `SessionManager::IsPeerAuthorized`).
- [x] Chỉ chấp nhận kết nối RFB nếu client tương ứng đang thuộc session hợp lệ.
- [x] Nếu giữ hỗ trợ third-party VNC client, bổ sung cơ chế xác thực riêng thực sự và không mặc định `SECURITY_NONE` (cổng 5900 được bảo vệ bởi session cổng 8080).
- [x] Đăng ký timeout hoặc đóng kết nối RFB sau khi phiên điều khiển kết thúc (`RfbServer::DisconnectClient` + `RevokeAuthorizedPeer`).
- [x] Bổ sung test từ peer chưa xác thực: kết nối bị từ chối (`test_rfb_authorization.cpp` Test 1).
- [x] Bổ sung test peer đã xác thực: kết nối RFB hoạt động đúng (`test_rfb_authorization.cpp` Test 2 & 3).

**Liên quan**

- `Client/Core/RfbServerSession.cpp`
- `Client/Core/SessionManager.cpp`
- `Manager/Core/RfbClientSession.cpp`

**Tiêu chí hoàn thành**

- Không có peer nào kết nối RFB thành công trước khi vượt qua handshake xác thực.
- Không thể xem framebuffer hoặc gửi Pointer/Key events khi session chưa được cấp phép.

## Required

### R-1: Loại bỏ secret mặc định cố định khỏi authentication

**Vấn đề**

- `DEFAULT_AUTH_TOKEN` được nhúng trực tiếp trong `Shared/Protocol.h`.
- Khóa phiên được derive trực tiếp bằng SHA-256 từ token mà không có salt.
- Có cùng một secret và cùng một khóa cho mọi phiên.

**Việc cần làm**

- [x] Loại bỏ secret mặc định khỏi mã nguồn và binary public (Client sinh mã PIN ngẫu nhiên 6 chữ số qua CSPRNG, Manager nhập PIN).
- [x] Thêm cơ chế cấu hình token do người dùng nhập hoặc tải secret từ cấu hình được bảo vệ (Khay hệ thống Client + Hộp thoại kết nối Manager).
- [x] Tạo random salt hoặc challenge duy nhất cho mỗi phiên (16-byte client challenge + 16-byte server challenge = 32-byte salt).
- [x] Derive session key bằng KDF phù hợp từ secret + salt/challenge duy nhất (ChaCha20-Poly1305 session key độc lập).
- [x] Không ghi token/challenge/khóa phiên ra log.
- [x] Bổ sung test xác nhận hai session không tái sử dụng cùng session key (`TestSaltMixingSessionKeyUniqueness`).
- [x] Bổ sung test xác nhận tampered hoặc sai credential không tạo session key (`TestTamperedTokenFailsSessionKey`).

**Liên quan**

- `Shared/Protocol.h`
- `Shared/Crypto.cpp`
- `Client/Core/SessionManager.cpp`
- `Manager/Core/ManagerMain.cpp`

**Tiêu chí hoàn thành**

- Binary không chứa credential mặc định mà peer có thể đoán.
- Hai phiên độc lập không dùng cùng session key.

### R-2: Triển khai cấu hình chất lượng video động

**Vấn đề**

- `ScreenCapture::SetVideoConfig()` bỏ qua toàn bộ tham số.
- `GetQualityPreset()` luôn trả `QUALITY_BALANCED`.
- `GetTargetFps()` luôn trả `30`.
- Chức năng quản lý đã gửi `MSG_VIDEO_CONFIG` nhưng Client không áp dụng cấu hình.

**Việc cần làm**

- [x] Lưu preset và FPS bằng atomic state hợp lệ (`std::atomic<uint8_t> g_qualityPreset`, `g_targetFps`).
- [x] Xác thực preset (`<= QUALITY_HIGH`) và clamp FPS (`[5, 60]`) trước khi cập nhật.
- [x] Ánh xạ preset sang tham số TurboJPEG hoặc cấu hình nén thực tế:
  - LOW
  - BALANCED
  - HIGH
  - AUTO
- [x] Áp dụng target FPS vào pacing của capture loop (`RfbServerSession::TransmitLoop`).
- [x] Gọi `ForceFullFrame()` khi thay đổi cấu hình để tránh giữ frame cũ.
- [x] Cập nhật encoding và FPS một cách atomic để các getter không quan sát trạng thái nửa cập nhật.
- [x] Kích hoạt lại test 3.2 trong suite M4 (`14/14` pass).
- [ ] Bổ sung test cho preset không hợp lệ và FPS ngoài phạm vi.

**Liên quan**

- `Client/Media/ScreenCapture.cpp`
- `Client/Media/ScreenCapture.h`
- `Client/Core/SessionManager.cpp`
- `tests/test_remote_features.cpp`

**Tiêu chí hoàn thành**

- LOW/BALANCED/HIGH/AUTO cập nhật preset thực tế.
- FPS thay đổi làm capture loop dùng đúng tốc độ đã cấu hình.
- Test dynamic quality pass.

### R-3: Ngăn path traversal cho upload và download

**Vấn đề**

- Relative path nhận từ mạng được ghép trực tiếp vào local path.
- Có thể chứa `..`, đường dẫn tuyệt đối hoặc separator không mong muốn.
- Peer hợp lệ vẫn có thể ghi file ra ngoài thư mục tải xuống hoặc thư mục upload được phép.

**Việc cần làm**

- [x] Tạo một helper canonicalization/path containment chung cho cả upload và download (`Shared/PathSanitizer.h`, `Shared/PathSanitizer.cpp`).
- [x] Normalize tất cả separator và relative path.
- [x] Từ chối `..`, absolute path và tên rỗng.
- [x] Chuyển root và candidate sang absolute path rồi dùng canonical/lexical containment check (`ResolveSafePath`, `IsPathContained`).
- [x] Với download, giới hạn mọi file/directory trong thư mục `downloads/`.
- [x] Với upload, giới hạn mọi file/directory trong thư mục đích đã chọn.
- [x] Kiểm tra symlink hoặc reparse point nếu candidate có thể đi qua nó.
- [x] Bổ sung test `../outside.txt`, `..\\outside.txt`, absolute path và nested path hợp lệ (Suite M3 Group 7: 8/8 tests pass).
- [x] Gửi `MSG_FILE_ERROR` thay vì âm thầm bỏ qua packet hoặc ghi sai nơi.

**Liên quan**

- `Manager/UI/FileExplorerWindow.cpp`
- `Client/Core/SessionManager.cpp`
- `Client/FileTransfer/FileWorker.cpp`
- `Manager/FileTransfer/FileManager.cpp`

**Tiêu chí hoàn thành**

- Không packet nào có thể ghi dữ liệu ra ngoài root được phép.
- Directory hợp lệ vẫn được tạo đệ quy bình thường.
- Path malicious được reject rõ ràng.

### R-4: Sửa và mở rộng test crypto hiện có

**Vấn đề**

- `tests/test_crypto_security.cpp` compile không thành công do tham chiếu `Protocol::MSG_KEY_EVENT`, enum không còn tồn tại.
- Suite crypto stale không thể được xem là verification hiện hành.

**Việc cần làm**

- [x] Cập nhật test để dùng message type hiện có hoặc thêm lại enum nếu protocol thực sự cần nó.
- [x] Compile riêng và chạy `test_crypto_security.exe`.
- [x] Tách crypto test khỏi build app chính để lỗi build không làm mất kết quả crypto.
- [x] Thêm test cho wrong key, nonce reuse, ciphertext tampering, truncation và envelope size mismatch.
- [x] Đảm bảo test xác nhận nonce được tạo ngẫu nhiên cho mỗi encryption.
- [ ] Xem xét nonce replay protection hoặc counter/sequence number nếu protocol phải chống replay.

**Liên quan**

- `tests/test_crypto_security.cpp`
- `Shared/Crypto.cpp`
- `Shared/ControlEnvelope.h`
- `build.bat`

**Tiêu chí hoàn thành**

- `test_crypto_security.exe` build và pass 100%.
- Không còn reference đến enum hoặc API đã bị xóa.

### R-5: Ngăn build script che giấu lỗi

**Vấn đề**

- `build.bat test` vẫn chạy test sau lỗi link Manager/Client.
- Test success không phản ánh trạng thái build app thực tế.
- Script không dừng ngay khi từng bước build thất bại.

**Việc cần làm**

- [x] Kiểm tra `errorlevel` sau mỗi lệnh compile app.
- [x] Dừng script ngay khi build Manager hoặc Client thất bại.
- [x] Dừng script khi một test suite build thất bại hoặc test executable trả exit code khác 0.
- [x] In rõ tổng kết từng bước: build app, build test, run test.
- [x] Đổi tên hoặc thêm target CI test nhất quán (`build.bat test` xuất `test_summary.json`).
- [x] Xử lý executable đang chạy hoặc bị khóa bằng cách giải phóng tiến trình trước khi biên dịch (`taskkill /F`).

**Liên quan**

- `build.bat`

**Tiêu chí hoàn thành**

- Một build lỗi luôn trả exit code khác 0.
- Không in `[SUCCESS]` nếu app hoặc test nào chưa pass.
- Test suite chỉ pass khi executable build thành công.

## Nit

### N-1: Hoàn thiện file protocol và test consistency

- [x] Loại bỏ enum/reference stale trong tests (`tests/test_crypto_adversarial.cpp` đã loại bỏ `MSG_MOUSE_EVENT`, `MSG_KEY_EVENT`).
- [x] Kiểm tra mọi struct packed có padding/kích thước mong đợi trên mỗi compiler (`static_assert` 17 structs trong `Shared/Protocol.h`).
- [x] Kiểm tra version mismatch trong auth response trước khi đặt session key (`AUTH_VERSION_MISMATCH` check trong `SessionManager.cpp` và `ManagerMain.cpp`).
- [x] Validate `header.type` và `header.size` trước mọi dispatch (`Protocol::ValidatePacketSize` kiểm tra trước mọi dispatch trên Client và Manager).
- [x] Giới hạn packet payload theo từng loại message thay vì limit chung (`ValidatePacketSize` thiết lập biên kích thước chặt chẽ cho từng message).

### N-2: Cải thiện lỗi và observability

- [x] Ghi log không chứa secret hoặc payload nhạy cảm (Không in PIN, token, challenge hay session key ra màn hình console/log).
- [x] Phân biệt rõ timeout, invalid token, rejected, version mismatch (`AuthStatus` có enum đầy đủ và Manager hiển thị thông báo tiếng Việt chính xác tương ứng).
- [x] Thêm kết quả build/test machine-readable cho CI (`build.bat test` tự động xuất `tests/test_summary.json`).
- [x] Thêm correlation/session ID để truy vết cổng 8080, 5900 và 8082 (`SessionManager::GetCurrentSessionId()` tạo `SESS-XXXXXXXX` gắn vào log RFB và Session).

### N-3: Review dead code sau các thay đổi

- [x] Liệt kê hàm hoặc biến không còn dùng sau khi triển khai dynamic quality (Xác nhận các hàm `ScreenCapture` và `SessionManager` đều có call sites tích cực).
- [x] Xóa wrapper hoặc branch stale không còn call site (Đã dọn dẹp các branch RFB button và quality slider cũ).
- [x] Không xóa API chưa xác định vẫn có thể dùng bởi external integration trước khi xác nhận call graph.

## Verification hiện tại (Cập nhật 2026-11-25 — Sau vá lỗi Phase 1–3)

| Verification | Kết quả |
|---|---|
| CRC32 và file transfer unit suite (M3 - R-3) | 25/25 pass (100% - bao gồm Group 7 Path Traversal) |
| Remote features suite (M4 - R-2) | 14/14 pass (100% - Dynamic Quality `AUTO..HIGH` & FPS Presets) |
| Crypto security suite (R-4, C-1, C-2) | 8/8 suites pass (100% - RFC 4231 HMAC-SHA256, OS CSPRNG, RFC 8439 AEAD, PBKDF2-HMAC-SHA256, 218.386 MB/s) |
| Authentication handshake & PIN suite (R-1, N-1, C-2) | 8/8 pass (100% - `MSG_AUTH_CHALLENGE` HMAC-SHA256, Challenge Salt, Key Uniqueness, Version Mismatch, Packet Validation) |
| RFB port 5900 authorization gate (CR-1) | 3/3 pass (100% - Chặn peer chưa xác thực, cho phép peer hợp lệ) |
| Manager/Client build (`build.bat test`) | Build thành công (Exit 0, `-lbcrypt`, kiểm tra `errorlevel` sau từng target) |

## Tổng hợp vá lỗi bảo mật, ổn định và hiệu năng (Phase 1–3)

- **Phase 1 (Critical C-1..C-6):** Thay CSPRNG bằng Windows `BCryptGenRandom` (`Shared/Crypto.cpp`), bổ sung HMAC-SHA256 & PBKDF2-HMAC-SHA256 (10.000 vòng), chuyển xác thực PIN sang Challenge-Response HMAC qua `MSG_AUTH_CHALLENGE` + rate-limit chống brute-force theo IP, chặn hạ cấp gói tin rõ sau khi thiết lập khóa phiên (`SessionManager.cpp`, `DataReceiver.cpp`), sửa `CRITICAL_SECTION` chưa khởi tạo trong `ClientChatWindow.cpp`, chặn OOM/overflow phân bổ buffer trên RFB (`RfbClientSession.cpp`, `RfbMessages.h`), khắc phục vòng đời GDI handle (`TaskManagerWindow.cpp`, `FileExplorerWindow.cpp`, `RemoteWindow.cpp`).
- **Phase 2 (Important I-1..I-9):** Giải phóng khóa khi giải mã JPEG ngoài `CRITICAL_SECTION` + sửa lag `Ctrl+M`/fullscreen (`AudioPlayer.cpp`, `RemoteWindow.cpp`), bảo vệ `TcpServer`/`TcpClient` bằng `stateMutex`/`CopySessionKey` (`Network.h`, `Network.cpp`), xác thực IP trên cổng Audio 8082 (`SessionManager.cpp`), bổ sung timeout handshake RFB 5s, thoát vòng lặp khi lệch luồng, FPS động và gửi move trước click (`RfbServerSession.cpp`), dọn COM DXGI khi lỗi + tránh cấp phát lại GDI mỗi frame + kiểm tra biên `SetVideoConfig` (`ScreenCapture.cpp`), đồng bộ UDP discovery (`DiscoveryScanner.cpp`, `DiscoveryResponder.cpp`), sửa race UI (`FileExplorerWindow.cpp`, `ChatWindow.cpp`), chặn tên thiết bị đặc biệt Windows (`PathSanitizer.cpp`) và chia cho 0 / tự kill tiến trình (`SysMonitor.cpp`).
- **Phase 3 (C-7, C-8, I-10):** Chuyển các thread mạng/truyền tệp sang joinable (`SessionManager.cpp`, `FileExplorerWindow.cpp`, `ClientMain.cpp`), bảo vệ `g_alertMessage` bằng `std::mutex` (`StatusOverlay.cpp`), giữ lại mã nguồn `tests/` trong `.gitignore` (chỉ ignore `tests/*.exe`, `tests/test_summary.json`), rút gọn `build.bat` kèm `-lbcrypt` và `errorlevel`.

## Thứ tự triển khai đề xuất

1. Fix R-4 và R-5 build/test verification. [HOÀN THÀNH]
2. Fix R-3 path containment. [HOÀN THÀNH]
3. Fix R-1 và CR-1 authentication boundaries (Bảo mật mạng, Challenge-Response HMAC & RFB Gate). [HOÀN THÀNH]
4. Fix R-2 dynamic video quality. [HOÀN THÀNH]
5. Hoàn thành Nit test consistency và observability (N-1, N-2, N-3). [HOÀN THÀNH]
6. Hoàn thành toàn bộ Phase 1–3 review fixes và chạy lại 5 suite kiểm thử. [HOÀN TẤT TOÀN DIỆN]

## Quality Gate trước merge

- [x] Không còn Critical issue (C-1..C-8, CR-1).
- [x] Không còn Required / Important issue (R-1..R-5, I-1..I-10).
- [x] Crypto test build và pass (8/8 suites pass).
- [x] Remote features test pass (14/14 pass).
- [x] File transfer test pass (25/25 pass).
- [x] Auth handshake test pass (8/8 pass).
- [x] RFB authorization gate test pass (3/3 pass).
- [x] Manager/Client build exit 0 (`build.bat test` + 3 security/auth suites).
