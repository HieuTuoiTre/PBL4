#include "ClipboardManager.h"
#include "Utils.h"
#include <chrono>
#include <thread>
#include <cstring>

namespace {
    // RAII guard for Win32 clipboard access with retry loop
    struct ClipboardScope {
        bool isOpen = false;

        explicit ClipboardScope(HWND hwnd, int maxAttempts = 5, int delayMs = 5) {
            for (int i = 0; i < maxAttempts; ++i) {
                if (OpenClipboard(hwnd)) {
                    isOpen = true;
                    return;
                }
                Sleep(delayMs);
            }
        }

        ~ClipboardScope() {
            if (isOpen) {
                CloseClipboard();
            }
        }

        operator bool() const { return isOpen; }
    };
}

std::atomic<bool> ClipboardManager::s_isSettingLocal{false};
std::atomic<bool> ClipboardManager::s_isInitialized{false};
std::string ClipboardManager::s_lastSyncedText;
uint32_t ClipboardManager::s_lastSyncedCrc{0};
std::mutex ClipboardManager::s_mutex;

uint32_t ClipboardManager::CalculateHash(const std::string& text) {
    // 32-bit FNV-1a hash algorithm
    uint32_t hash = 2166136261u;
    for (unsigned char c : text) {
        hash ^= (uint32_t)c;
        hash *= 16777619u;
    }
    return hash;
}

bool ClipboardManager::Initialize(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        AddClipboardFormatListener(hwnd);
    }
    s_isInitialized.store(true, std::memory_order_release);
    return true;
}

void ClipboardManager::Cleanup(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        RemoveClipboardFormatListener(hwnd);
    }
    Reset();
    s_isInitialized.store(false, std::memory_order_release);
}

void ClipboardManager::Reset() {
    s_isSettingLocal.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(s_mutex);
    s_lastSyncedText.clear();
    s_lastSyncedCrc = 0;
}

void ClipboardManager::MarkSettingLocal() {
    s_isSettingLocal.store(true, std::memory_order_release);
}

bool ClipboardManager::IsSettingLocal() {
    return s_isSettingLocal.load(std::memory_order_acquire);
}

bool ClipboardManager::ReadText(HWND hwnd, std::string& outUtf8) {
    outUtf8.clear();

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return false;
    }

    ClipboardScope clip(hwnd);
    if (!clip) {
        return false;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        return false;
    }

    const wchar_t* pWide = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!pWide) {
        return false;
    }

    SIZE_T maxBytes = GlobalSize(hData);
    size_t maxChars = maxBytes / sizeof(wchar_t);

    size_t wideLen = 0;
    while (wideLen < maxChars && pWide[wideLen] != L'\0') {
        wideLen++;
    }

    if (wideLen > MAX_CLIPBOARD_TEXT_BYTES) {
        GlobalUnlock(hData);
        return false;
    }

    std::wstring wideStr(pWide, wideLen);
    GlobalUnlock(hData);

    outUtf8 = Utils::WideToUtf8(wideStr);
    if (outUtf8.size() > MAX_CLIPBOARD_TEXT_BYTES) {
        outUtf8.clear();
        return false;
    }

    return true;
}

bool ClipboardManager::WriteText(HWND hwnd, const std::string& utf8Text) {
    if (utf8Text.size() > MAX_CLIPBOARD_TEXT_BYTES) {
        return false;
    }

    std::wstring wideStr = Utils::Utf8ToWide(utf8Text);
    size_t byteCount = (wideStr.size() + 1) * sizeof(wchar_t);

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (!hMem) {
        return false;
    }

    void* pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return false;
    }
    memcpy(pMem, wideStr.c_str(), byteCount);
    GlobalUnlock(hMem);

    // Pre-arm Tier 1 and Tier 2 before SetClipboardData so that if Windows synchronously
    // or immediately posts WM_CLIPBOARDUPDATE, the flags are already active.
    MarkSettingLocal();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_lastSyncedText = utf8Text;
        s_lastSyncedCrc = CalculateHash(utf8Text);
    }

    ClipboardScope clip(hwnd);
    if (!clip) {
        GlobalFree(hMem);
        s_isSettingLocal.store(false, std::memory_order_release);
        return false;
    }

    if (!EmptyClipboard()) {
        GlobalFree(hMem);
        s_isSettingLocal.store(false, std::memory_order_release);
        return false;
    }

    HANDLE hResult = SetClipboardData(CF_UNICODETEXT, hMem);
    if (!hResult) {
        GlobalFree(hMem);
        s_isSettingLocal.store(false, std::memory_order_release);
        return false;
    }

    // Success: Windows system owns hMem now.
    return true;
}

bool ClipboardManager::ShouldSync(const std::string& text) {
    if (text.empty() || text.size() > MAX_CLIPBOARD_TEXT_BYTES) {
        return false;
    }

    // Tier 1: Re-entrancy suppression flag
    if (s_isSettingLocal.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }

    // Tier 2: Content Hash & String Comparison
    uint32_t hash = CalculateHash(text);
    std::lock_guard<std::mutex> lock(s_mutex);

    if (hash == s_lastSyncedCrc && text == s_lastSyncedText) {
        return false;
    }

    s_lastSyncedText = text;
    s_lastSyncedCrc = hash;
    return true;
}
