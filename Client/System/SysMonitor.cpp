#include "SysMonitor.h"
#include <windows.h>

namespace {
    // Các biến toàn cục ẩn dùng để lưu mốc thời gian CPU lần đo trước đó
    FILETIME g_prevIdleTime;
    FILETIME g_prevKernelTime;
    FILETIME g_prevUserTime;
    bool g_isInitialized = false;

    // Hàm phụ trợ đổi định dạng thời gian của Windows sang số nguyên 64-bit để dễ làm phép trừ
    uint64_t FileTimeToInt64(const FILETIME& ft) {
        return (((uint64_t)ft.dwHighDateTime) << 32) | ((uint64_t)ft.dwLowDateTime);
    }
}

namespace SysMonitor {

    void Initialize() {
        // Lấy mốc thời gian CPU hiện tại lưu vào biến toàn cục
        GetSystemTimes(&g_prevIdleTime, &g_prevKernelTime, &g_prevUserTime);
        g_isInitialized = true;
    }

    Protocol::SysInfoPayload GetSystemInfo() {
        Protocol::SysInfoPayload payload = { 0 };

        // ==========================================
        // 1. LẤY PHẦN TRĂM RAM SỬ DỤNG
        // ==========================================
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            // memInfo.dwMemoryLoad trả về thẳng số % RAM đang bị chiếm dụng
            payload.ramUsage = (uint8_t)memInfo.dwMemoryLoad; 
        }

        // ==========================================
        // 2. LẤY PHẦN TRĂM Ổ ĐĨA C: SỬ DỤNG
        // ==========================================
        ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
        if (GetDiskFreeSpaceExA("C:\\", &freeBytes, &totalBytes, &totalFreeBytes)) {
            uint64_t usedBytes = totalBytes.QuadPart - totalFreeBytes.QuadPart;
            payload.diskUsage = (uint8_t)((usedBytes * 100) / totalBytes.QuadPart);
        }

        // ==========================================
        // 3. LẤY PHẦN TRĂM CPU SỬ DỤNG
        // ==========================================
        if (!g_isInitialized) {
            Initialize();
        } else {
            FILETIME idleTime, kernelTime, userTime;
            // Lấy mốc thời gian CPU ở thời điểm hiện tại
            if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
                
                uint64_t curIdle   = FileTimeToInt64(idleTime);
                uint64_t curKernel = FileTimeToInt64(kernelTime);
                uint64_t curUser   = FileTimeToInt64(userTime);

                uint64_t prevIdle   = FileTimeToInt64(g_prevIdleTime);
                uint64_t prevKernel = FileTimeToInt64(g_prevKernelTime);
                uint64_t prevUser   = FileTimeToInt64(g_prevUserTime);

                // Tính tổng thời gian hệ thống đã trôi qua
                uint64_t sysTimeDelta = (curKernel - prevKernel) + (curUser - prevUser);
                // Tính thời gian CPU rảnh rỗi (idle)
                uint64_t idleTimeDelta = curIdle - prevIdle;

                if (sysTimeDelta > 0) {
                    // Thời gian CPU thực sự làm việc = Tổng thời gian - Thời gian nghỉ
                    uint64_t activeTime = sysTimeDelta - idleTimeDelta;
                    payload.cpuUsage = (uint8_t)((activeTime * 100) / sysTimeDelta);
                }

                // Cập nhật lại mốc thời gian cho lần đo tiếp theo
                g_prevIdleTime = idleTime;
                g_prevKernelTime = kernelTime;
                g_prevUserTime = userTime;
            }
        }

        return payload;
    }
}