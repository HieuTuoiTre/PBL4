# Code Review Action Items

Ngày đánh giá: 2026-11-23

Trạng thái tổng thể: **Không đạt quality gate**. Cần hoàn thành các mục Critical/Required trước khi xem ứng dụng sẵn sàng merge hoặc phát hành.

## Critical

### CR-1: Bắt buộc xác thực cho cổng RFB 5900

**Vấn đề**

- `RfbServerSession.cpp` chỉ hỗ trợ `SECURITY_NONE`.
- Server luôn gửi `SECURITY_RESULT_OK` sau khi client chọn `SECURITY_NONE`.
- Một client bất kỳ có thể xem và điều khiển desktop mà không đi qua session authentication ở cổng 8080.

**Việc cần làm**

- [ ] Kiểm tra phiên 8080 đã xác thực thành công trước khi cho phép phiên RFB 5900.
- [ ] Chỉ chấp nhận kết nối RFB nếu client tương ứng đang thuộc session hợp lệ.
- [ ] Nếu giữ hỗ trợ third-party VNC client, bổ sung cơ chế xác thực riêng thực sự và không mặc định `SECURITY_NONE`.
- [ ] Đăng ký timeout hoặc đóng kết nối RFB sau khi phiên điều khiển kết thúc.
- [ ] Bổ sung test từ peer chưa xác thực: kết nối bị từ chối.
- [ ] Bổ sung test peer đã xác thực: kết nối RFB hoạt động đúng.

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

- [ ] Loại bỏ secret mặc định khỏi mã nguồn và binary public.
- [ ] Thêm cơ chế cấu hình token do người dùng nhập hoặc tải secret từ cấu hình được bảo vệ.
- [ ] Tạo random salt hoặc challenge duy nhất cho mỗi phiên.
- [ ] Derive session key bằng KDF phù hợp từ secret + salt/challenge duy nhất.
- [ ] Không ghi token/challenge/khóa phiên ra log.
- [ ] Bổ sung test xác nhận hai session không tái sử dụng cùng session key.
- [ ] Bổ sung test xác nhận tampered hoặc sai credential không tạo session key.

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

- [ ] Lưu preset và FPS bằng atomic state hợp lệ.
- [ ] Xác thực preset và FPS trước khi cập nhật.
- [ ] Ánh xạ preset sang tham số TurboJPEG hoặc cấu hình nén thực tế:
  - LOW
  - BALANCED
  - HIGH
  - AUTO
- [ ] Áp dụng target FPS vào pacing của capture loop.
- [ ] Gọi `ForceFullFrame()` khi thay đổi cấu hình để tránh giữ frame cũ.
- [ ] Cập nhật encoding và FPS một cách atomic để các getter không quan sát trạng thái nửa cập nhật.
- [ ] Kích hoạt lại test 3.2 trong suite M4.
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

- [ ] Tạo một helper canonicalization/path containment chung cho cả upload và download.
- [ ] Normalize tất cả separator và relative path.
- [ ] Từ chối `..`, absolute path và tên rỗng.
- [ ] Chuyển root và candidate sang absolute path rồi dùng canonical/lexical containment check.
- [ ] Với download, giới hạn mọi file/directory trong thư mục `downloads/`.
- [ ] Với upload, giới hạn mọi file/directory trong thư mục đích đã chọn.
- [ ] Kiểm tra symlink hoặc reparse point nếu candidate có thể đi qua nó.
- [ ] Bổ sung test `../outside.txt`, `..\\outside.txt`, absolute path và nested path hợp lệ.
- [ ] Gửi `MSG_FILE_ERROR` thay vì âm thầm bỏ qua packet hoặc ghi sai nơi.

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

- [ ] Cập nhật test để dùng message type hiện có hoặc thêm lại enum nếu protocol thực sự cần nó.
- [ ] Compile riêng và chạy `test_crypto_security.exe`.
- [ ] Tách crypto test khỏi build app chính để lỗi build không làm mất kết quả crypto.
- [ ] Thêm test cho wrong key, nonce reuse, ciphertext tampering, truncation và envelope size mismatch.
- [ ] Đảm bảo test xác nhận nonce được tạo ngẫu nhiên cho mỗi encryption.
- [ ] Xem xét nonce replay protection hoặc counter/sequence number nếu protocol phải chống replay.

**Liên quan**

- `tests/test_crypto_security.cpp`
- `Shared/Crypto.cpp`
- `Shared/ControlEnvelope.h`
- `build.bat`

**Tiêu chí hoàn thành**

- `test_crypto_security.exe` build và pass.
- Không còn reference đến enum hoặc API đã bị xóa.

### R-5: Ngăn build script che giấu lỗi

**Vấn đề**

- `build.bat test` vẫn chạy test sau lỗi link Manager/Client.
- Test success không phản ánh trạng thái build app thực tế.
- Script không dừng ngay khi từng bước build thất bại.

**Việc cần làm**

- [ ] Kiểm tra `errorlevel` sau mỗi lệnh compile app.
- [ ] Dừng script ngay khi build Manager hoặc Client thất bại.
- [ ] Dừng script khi một test suite build thất bại hoặc test executable trả exit code khác 0.
- [ ] In rõ tổng kết từng bước: build app, build test, run test.
- [ ] Đổi tên hoặc thêm target CI test nhất quán.
- [ ] Xử lý executable đang chạy hoặc bị khóa bằng thông báo rõ ràng thay vì `Permission denied`.

**Liên quan**

- `build.bat`

**Tiêu chí hoàn thành**

- Một build lỗi luôn trả exit code khác 0.
- Không in `[SUCCESS]` nếu app hoặc test nào chưa pass.
- Test suite chỉ pass khi executable build thành công.

## Nit

### N-1: Hoàn thiện file protocol và test consistency

- [ ] Loại bỏ enum/reference stale trong tests.
- [ ] Kiểm tra mọi struct packed có padding/kích thước mong đợi trên mỗi compiler.
- [ ] Kiểm tra version mismatch trong auth response trước khi đặt session key.
- [ ] Validate `header.type` và `header.size` trước mọi dispatch.
- [ ] Giới hạn packet payload theo từng loại message thay vì limit chung.

### N-2: Cải thiện lỗi và observability

- [ ] Ghi log không chứa secret hoặc payload nhạy cảm.
- [ ] Phân biệt rõ timeout, invalid token, rejected, version mismatch.
- [ ] Thêm kết quả build/test machine-readable cho CI.
- [ ] Thêm correlation/session ID để truy vết cổng 8080, 5900 và 8082.

### N-3: Review dead code sau các thay đổi

- [ ] Liệt kê hàm hoặc biến không còn dùng sau khi triển khai dynamic quality.
- [ ] Xóa wrapper hoặc branch stale không còn call site.
- [ ] Không xóa API chưa xác định vẫn có thể dùng bởi external integration trước khi xác nhận call graph.

## Verification hiện tại

| Verification | Kết quả |
|---|---|
| CRC32 và file transfer unit suite | 17/17 pass |
| Authentication handshake suite | 100% pass |
| RFB state machine | Pass |
| RFB third-party interop | Pass |
| Remote features suite | 13/14 pass |
| Dynamic quality test | Fail |
| Crypto security suite | Không compile |
| Manager/Client build | Không thành công do executable bị `Permission denied` |

## Thứ tự triển khai đề xuất

1. Fix R-3 path containment.
2. Fix R-1 và CR-1 authentication boundaries.
3. Fix R-4 và R-5 build/test verification.
4. Fix R-2 dynamic video quality.
5. Hoàn thành Nit test consistency và observability.
6. Chạy lại toàn bộ suite trước khi approve.

## Quality Gate trước merge

- [ ] Không còn Critical issue.
- [ ] Không còn Required issue.
- [ ] Crypto test build và pass.
- [ ] Remote features test 14/14 pass.
- [ ] File transfer test pass.
- [ ] Auth handshake test pass.
- [ ] RFB state machine và interop test pass.
- [ ] Manager/Client build exit 0.
- [ ] Build script trả exit code khác 0 khi có bước lỗi.
