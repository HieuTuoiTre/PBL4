#include "UITheme.h"
#include <ole2.h>
#include <gdiplus.h>

namespace UITheme {

static HFONT gFont = nullptr;
static HFONT gBoldFont = nullptr;
static ULONG_PTR gGdiplusToken = 0;

void Initialize() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gGdiplusToken, &gdiplusStartupInput, nullptr);

    if (!gFont) {
        gFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    if (!gBoldFont) {
        gBoldFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
}

void Cleanup() {
    if (gFont) {
        DeleteObject(gFont);
        gFont = nullptr;
    }
    if (gBoldFont) {
        DeleteObject(gBoldFont);
        gBoldFont = nullptr;
    }
    if (gGdiplusToken) {
        Gdiplus::GdiplusShutdown(gGdiplusToken);
        gGdiplusToken = 0;
    }
}

HFONT GetNormalFont() {
    return gFont;
}

HFONT GetBoldFont() {
    return gBoldFont;
}

void SetControlFont(HWND control, HFONT font) {
    if (!font) font = gFont;
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

COLORREF UsageColor(int level) {
    switch (level) {
    case 3: return RGB(125, 211, 252);
    case 2: return RGB(186, 230, 253);
    default: return RGB(224, 242, 254);
    }
}

void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawRoundedPanel(HDC dc, const RECT& rect, COLORREF fill, COLORREF outline, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void AddListColumn(HWND list, int index, const wchar_t* text, int width, int format) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    column.fmt = format;
    column.cx = width;
    column.pszText = const_cast<LPWSTR>(text);
    SendMessageW(list, LVM_INSERTCOLUMNW, index, reinterpret_cast<LPARAM>(&column));
}

void AddListRow(HWND list, int row, const wchar_t* firstColumn) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.pszText = const_cast<LPWSTR>(firstColumn);
    SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
}

void SetListItemText(HWND list, int row, int column, const wchar_t* text) {
    LVITEMW item{};
    item.iSubItem = column;
    item.pszText = const_cast<LPWSTR>(text);
    SendMessageW(list, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
}

std::wstring GetControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length), L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    return value;
}

} // namespace UITheme

