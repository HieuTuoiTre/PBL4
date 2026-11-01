#pragma once

#include <windows.h>
#include <string>
#include <cstdint>
#include <mutex>
#include <atomic>

/**
 * @brief Thread-safe Win32 Clipboard Synchronization Engine with Dual-Tier Anti-Echo Loop.
 * 
 * Implements bidirectional UTF-8 text synchronization using Windows API:
 * - Read: OpenClipboard, GetClipboardData(CF_UNICODETEXT), GlobalLock, UTF-16 to UTF-8 conversion.
 * - Write: UTF-8 to UTF-16 conversion, GlobalAlloc(GMEM_MOVEABLE), EmptyClipboard, SetClipboardData.
 * - Robustness: Automatic retry loop (5 attempts, 5ms delay) for lock contention.
 * - Safety: 1 MB hard size cap to prevent memory exhaustion and socket starvation.
 * - Anti-Echo Loop: Dual-tier protection combining atomic re-entrancy suppression flag
 *                   and 32-bit FNV-1a content hash / text caching.
 */
class ClipboardManager {
public:
    ClipboardManager() = delete;
    ~ClipboardManager() = delete;

    // Maximum clipboard text payload size: 1 MB (1,048,576 bytes)
    static constexpr size_t MAX_CLIPBOARD_TEXT_BYTES = 1024 * 1024;

    // Registers window as clipboard format listener via AddClipboardFormatListener
    static bool Initialize(HWND hwnd);

    // Unregisters window via RemoveClipboardFormatListener and resets internal state
    static void Cleanup(HWND hwnd);

    // Reads UTF-16 text (CF_UNICODETEXT) from Win32 clipboard and converts to UTF-8
    // Uses retry loop (up to 5 attempts, 5ms delay) if clipboard is temporarily locked
    static bool ReadText(HWND hwnd, std::string& outUtf8);

    // Converts UTF-8 text to UTF-16, allocates GMEM_MOVEABLE, and sets CF_UNICODETEXT
    // Uses retry loop and updates internal cache to prevent echo loop
    static bool WriteText(HWND hwnd, const std::string& utf8Text);

    // Evaluates whether incoming text should be synchronized across the network:
    // Returns false if:
    //  - text is empty or > 1MB
    //  - local write flag was set (Tier 1 re-entrancy prevention)
    //  - content matches s_lastSyncedText / s_lastSyncedCrc (Tier 2 deduplication)
    // Returns true and updates cache if new unique text copied locally
    static bool ShouldSync(const std::string& text);

    // Marks that the upcoming WM_CLIPBOARDUPDATE is triggered by a remote network write
    static void MarkSettingLocal();

    // Checks if the local write flag is currently active
    static bool IsSettingLocal();

    // Resets internal sync cache and flags (useful for session disconnect or tests)
    static void Reset();

    // Fast 32-bit FNV-1a hash calculation for content fingerprinting
    static uint32_t CalculateHash(const std::string& text);

private:
    static std::atomic<bool> s_isSettingLocal;
    static std::atomic<bool> s_isInitialized;
    static std::string s_lastSyncedText;
    static uint32_t s_lastSyncedCrc;
    static std::mutex s_mutex;
};
