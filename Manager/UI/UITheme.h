#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <string>

namespace UITheme {

void Initialize();
void Cleanup();

HFONT GetNormalFont();
HFONT GetBoldFont();

void SetControlFont(HWND control, HFONT font = nullptr);
COLORREF UsageColor(int level);

void FillSolid(HDC dc, const RECT& rect, COLORREF color);
void DrawRoundedPanel(HDC dc, const RECT& rect, COLORREF fill, COLORREF outline, int radius = 8);

void AddListColumn(HWND list, int index, const wchar_t* text, int width, int format = LVCFMT_LEFT);
void AddListRow(HWND list, int row, const wchar_t* firstColumn);
void SetListItemText(HWND list, int row, int column, const wchar_t* text);
std::wstring GetControlText(HWND control);

} // namespace UITheme

