#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include "SysMonitor.h"

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

    // ==========================================
    // 4. LẤY DANH SÁCH TIẾN TRÌNH ĐANG CHẠY
    // ==========================================
    std::vector<Protocol::ProcessInfoPayload> GetProcessList() {
        std::vector<Protocol::ProcessInfoPayload> processList;
        
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return processList;

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(hSnapshot, &pe32)) {
            do {
                Protocol::ProcessInfoPayload pInfo = {0};
                pInfo.pid = pe32.th32ProcessID;

                // Copy tên file .exe
                WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, pInfo.name, sizeof(pInfo.name), NULL, NULL);
                // Mở process để lấy dung lượng RAM
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        pInfo.ramUsageMB = (uint32_t)(pmc.WorkingSetSize / (1024 * 1024));
                    }
                    CloseHandle(hProcess);
                }
                
                pInfo.diskUsageMB = 0;
                processList.push_back(pInfo);
            } while (Process32Next(hSnapshot, &pe32));
        }
        
        CloseHandle(hSnapshot);
        return processList;
    }

    // ==========================================
    // 5. BUỘC DỪNG TIẾN TRÌNH THEO PID
    // ==========================================
    bool KillProcess(uint32_t pid) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess == NULL) return false;
        
        bool result = TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        return result;
    }
}