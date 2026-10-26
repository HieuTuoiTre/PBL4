#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "ConnectionAlert.h"

enum class OverlayState {
    Disconnected,           // No active session
    ConnectedViewing,       // Manager is connected/watching, input idle
    ConnectedControlling    // Manager is actively injecting input/controlling
};

class StatusOverlay {
public:
    StatusOverlay();
    ~StatusOverlay();

    bool Initialize(HINSTANCE hInstance = nullptr);
    void SetState(OverlayState newState);
    void SetVisible(bool visible);
    void Destroy();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hwnd);

    HWND m_hwnd;
    HINSTANCE m_hInstance;
    std::atomic<OverlayState> m_currentState;
    std::atomic<bool> m_visible;
    std::thread m_uiThread;
    std::atomic<bool> m_isInitialized;

    // Dimensions
    const int m_width = 220;
    const int m_height = 42;
    const int m_margin = 15;
};
