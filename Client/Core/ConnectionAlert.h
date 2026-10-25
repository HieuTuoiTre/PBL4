#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

class ConnectionAlert {
public:
    static void Show(HINSTANCE hInstance = nullptr, const std::wstring& message = L"You are currently being connected to.");

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void Draw(HWND hwnd);

    static const UINT_PTR TIMER_DISMISS = 1001;
    static const int POPUP_WIDTH = 320;
    static const int POPUP_HEIGHT = 70;
};
