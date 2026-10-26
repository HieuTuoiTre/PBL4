#include "FileExplorerWindow.h"

#include "../../Shared/Utils.h"

#include "../FileTransfer/FileManager.h"

#include <commctrl.h>
#include <gdiplus.h>

#include <sstream>
#include <vector>
#include <string>
#include <cwchar>
#include <cmath>

#define WM_FILE_DRIVE_LIST (WM_APP + 201)
#define WM_FILE_DIR_LIST   (WM_APP + 202)
#define WM_FILE_START_DL   (WM_APP + 203)
#define WM_FILE_CHUNK_DL   (WM_APP + 204)
#define WM_FILE_END_DL     (WM_APP + 205)

#define IDC_FILE_LIST      2001
#define IDC_FILE_ROOT      2002
#define IDC_FILE_UP        2003
#define IDC_FILE_REFRESH   2004
#define IDC_FILE_DOWNLOAD  2005
#define IDC_FILE_ADDR      2006
#define IDC_FILE_PROGRESS  2007
#define IDC_FILE_STATUS    2008

namespace {

    HWND g_hFileWnd = NULL;
    HWND g_hFileList = NULL;
    HWND g_hBtnRoot = NULL;
    HWND g_hBtnUp = NULL;
    HWND g_hBtnRefresh = NULL;
    HWND g_hBtnDownload = NULL;
    HWND g_hAddressEdit = NULL;
    HWND g_hProgressBar = NULL;
    HWND g_hStatusLabel = NULL;
    HWND g_hHeader = NULL;
    WNDPROC g_oldHeaderProc = NULL;
    HWND g_hHoveredBtn = NULL;
    HIMAGELIST g_hImageList = NULL;
    TcpClient* g_client = nullptr;

    std::string g_curPath = "";
    std::string g_prevPath = "";
    FileManager g_fileMgr;
    std::string g_downloadingFile = "";
    uint64_t g_totalFileSize = 0;
    uint64_t g_downloadedBytes = 0;

    CRITICAL_SECTION g_fileCs;
    bool g_csInit = false;
    std::string g_pendingDriveContent;
    std::string g_pendingDirContent;

    HFONT g_hFontNormal = NULL;
    HFONT g_hFontBtn = NULL;
    HBRUSH g_hBgBrush = NULL;

    float GetDpiScale(HWND hwnd) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        static PFN_GetDpiForWindow pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        UINT dpi = 96;
        if (pGetDpiForWindow && hwnd) {
            dpi = pGetDpiForWindow(hwnd);
        } else {
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                dpi = GetDeviceCaps(hdc, LOGPIXELSY);
                ReleaseDC(hwnd, hdc);
            }
        }
        if (dpi == 0) dpi = 96;
        return (float)dpi / 96.0f;
    }

    void AddRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
        float d = radius * 2.0f;
        if (d > rect.Width) d = rect.Width;
        if (d > rect.Height) d = rect.Height;
        path.Reset();
        path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }

    LRESULT CALLBACK HeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_ERASEBKGND) {
            return 1;
        }

        if (uMsg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;

            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
            g.FillRectangle(&whiteBrush, 0, 0, width, height);

            Gdiplus::Pen borderPen(Gdiplus::Color(255, 229, 231, 235), 1.0f);
            g.DrawLine(&borderPen, 0, height - 1, width, height - 1);

            int itemCount = Header_GetItemCount(hWnd);
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font fontHeader(&fontFamily, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 30, 41, 59));

            for (int i = 0; i < itemCount; i++) {
                RECT itemRc;
                Header_GetItemRect(hWnd, i, &itemRc);

                wchar_t textBuf[128] = {0};
                HDITEMW hdi = {};
                hdi.mask = HDI_TEXT | HDI_FORMAT;
                hdi.pszText = textBuf;
                hdi.cchTextMax = 127;
                Header_GetItem(hWnd, i, &hdi);

                Gdiplus::RectF textRect((float)itemRc.left + 10.0f, (float)itemRc.top, (float)(itemRc.right - itemRc.left - 20), (float)(itemRc.bottom - itemRc.top));
                Gdiplus::StringFormat sf;
                sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                if (hdi.fmt & HDF_RIGHT) {
                    sf.SetAlignment(Gdiplus::StringAlignmentFar);
                } else if (hdi.fmt & HDF_CENTER) {
                    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                } else {
                    sf.SetAlignment(Gdiplus::StringAlignmentNear);
                }

                g.DrawString(textBuf, -1, &fontHeader, textRect, &sf, &textBrush);

                if (i < itemCount - 1) {
                    Gdiplus::Pen sepPen(Gdiplus::Color(255, 226, 232, 240), 1.0f);
                    g.DrawLine(&sepPen, (float)itemRc.right, (float)itemRc.top + 4.0f, (float)itemRc.right, (float)itemRc.bottom - 4.0f);
                }
            }

            EndPaint(hWnd, &ps);
            return 0;
        }

        return CallWindowProcW(g_oldHeaderProc, hWnd, uMsg, wParam, lParam);
    }

    std::wstring FormatBytes(uint64_t bytes) {
        wchar_t buf[64];
        if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
            swprintf(buf, 64, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024ULL * 1024ULL) {
            swprintf(buf, 64, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
        } else if (bytes >= 1024ULL) {
            swprintf(buf, 64, L"%.1f KB", (double)bytes / 1024.0);
        } else {
            swprintf(buf, 64, L"%llu B", bytes);
        }
        return buf;
    }

    void InsertFileRow(int index, const std::wstring& name, const std::wstring& type, const std::wstring& size) {
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = index;
        lvi.pszText = (LPWSTR)name.c_str();
        SendMessageW(g_hFileList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 1;
        lvi.pszText = (LPWSTR)type.c_str();
        SendMessageW(g_hFileList, LVM_SETITEMTEXTW, index, (LPARAM)&lvi);

        lvi.iSubItem = 2;
        lvi.pszText = (LPWSTR)size.c_str();
        SendMessageW(g_hFileList, LVM_SETITEMTEXTW, index, (LPARAM)&lvi);
    }

    void NavigateRoot(HWND hwnd) {
        if (!g_client) return;
        g_prevPath = g_curPath;
        g_curPath = "";
        if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, L"Danh sách ổ đĩa");
        SetWindowTextW(hwnd, L"LanRemote - Trình duyệt tệp (Danh sách ổ đĩa)");
        g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
    }

    void NavigateUp(HWND hwnd) {
        if (!g_client) return;

        if (g_curPath.length() <= 3) {
            NavigateRoot(hwnd);
        } else {
            g_prevPath = g_curPath;

            if (g_curPath.back() == '\\') g_curPath.pop_back();

            size_t pos = g_curPath.find_last_of("\\/");
            if (pos != std::string::npos) {
                g_curPath = g_curPath.substr(0, pos);

                if (g_curPath.length() == 2 && g_curPath[1] == ':') g_curPath += "\\";
            }
            if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
            SetWindowTextW(hwnd, (L"LanRemote - Trình duyệt tệp (" + Utils::Utf8ToWide(g_curPath) + L")").c_str());

            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        }
    }

    void RefreshCurrent(HWND hwnd) {
        if (!g_client) return;
        if (g_curPath.empty()) {
            g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
        } else {
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        }
    }

    void TriggerActionOnSelected(HWND hwnd) {
        if (!g_hFileList || !g_client) return;
        int sel = (int)SendMessageW(g_hFileList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1) return;

        wchar_t nameBuf[260] = {0}, typeBuf[128] = {0};
        LVITEMW lvi = {};
        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 260;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);
        lvi.iSubItem = 1; lvi.pszText = typeBuf; lvi.cchTextMax = 128;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

        std::string itemName = Utils::WideToUtf8(nameBuf);
        std::wstring itemType = typeBuf;

        if (itemName == "..") {
            NavigateUp(hwnd);
        } else if (itemType == L"[Ổ ĐĨA]") {
            g_prevPath = g_curPath;
            g_curPath = itemName;
            if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
            SetWindowTextW(hwnd, (L"LanRemote - Trình duyệt tệp (" + Utils::Utf8ToWide(g_curPath) + L")").c_str());
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        } else if (itemType == L"[THƯ MỤC]") {
            g_prevPath = g_curPath;
            if (!g_curPath.empty() && g_curPath.back() != '\\') g_curPath += "\\";
            g_curPath += itemName;
            if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
            SetWindowTextW(hwnd, (L"LanRemote - Trình duyệt tệp (" + Utils::Utf8ToWide(g_curPath) + L")").c_str());
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        } else {
            std::wstring confirmMsg = L"Bạn có muốn tải file '" + std::wstring(nameBuf) + L"' về máy không?";
            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận tải file", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                std::string fullPath = g_curPath;
                if (!fullPath.empty() && fullPath.back() != '\\') fullPath += "\\";
                fullPath += itemName;
                g_client->SendPacket(Protocol::MSG_FILE_DOWNLOAD_REQ, fullPath.c_str(), (int)fullPath.size());
            }
        }
    }

    void TriggerDownloadSelected(HWND hwnd) {
        if (!g_hFileList || !g_client) return;
        int sel = (int)SendMessageW(g_hFileList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1) {
            MessageBoxW(hwnd, L"Vui lòng chọn một tệp tin trong danh sách trước khi bấm tải về!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
            return;
        }

        wchar_t nameBuf[260] = {0}, typeBuf[128] = {0};
        LVITEMW lvi = {};
        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 260;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);
        lvi.iSubItem = 1; lvi.pszText = typeBuf; lvi.cchTextMax = 128;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

        if (wcscmp(typeBuf, L"[TẬP TIN]") == 0) {
            std::wstring confirmMsg = L"Bạn có muốn tải file '" + std::wstring(nameBuf) + L"' về máy không?";
            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận tải file", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                std::string fullPath = g_curPath;
                if (!fullPath.empty() && fullPath.back() != '\\') fullPath += "\\";
                fullPath += Utils::WideToUtf8(nameBuf);
                g_client->SendPacket(Protocol::MSG_FILE_DOWNLOAD_REQ, fullPath.c_str(), (int)fullPath.size());
            }
        } else {
            MessageBoxW(hwnd, L"Mục được chọn là thư mục hoặc ổ đĩa. Vui lòng chọn một tệp tin để tải về!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
        }
    }

    LRESULT CALLBACK FileWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);

        switch (msg) {

            case WM_CREATE: {
                g_hFileWnd = hwnd;

                g_hBtnRoot = CreateWindowExW(0, L"BUTTON", L"⊞ Ổ đĩa",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_ROOT, GetModuleHandle(NULL), NULL);

                g_hBtnUp = CreateWindowExW(0, L"BUTTON", L"↑ Lên cấp",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_UP, GetModuleHandle(NULL), NULL);

                g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"Làm mới",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_REFRESH, GetModuleHandle(NULL), NULL);

                g_hAddressEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Danh sách ổ đĩa",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_ADDR, GetModuleHandle(NULL), NULL);

                g_hBtnDownload = CreateWindowExW(0, L"BUTTON", L"⬇ Tải file về",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_DOWNLOAD, GetModuleHandle(NULL), NULL);

                g_hFileList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_LIST, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hFileList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

                g_hImageList = ImageList_Create(1, (int)std::round(30.0f * scale), ILC_COLOR32, 1, 1);
                ListView_SetImageList(g_hFileList, g_hImageList, LVSIL_SMALL);

                g_hHeader = ListView_GetHeader(g_hFileList);
                if (g_hHeader) {
                    g_oldHeaderProc = (WNDPROC)SetWindowLongPtrW(g_hHeader, GWLP_WNDPROC, (LONG_PTR)HeaderSubclassProc);
                }

                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, (int)(340 * scale), (LPWSTR)L"Tên thư mục / tập tin", 0, 0 };
                SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = (int)(120 * scale); lvc.pszText = (LPWSTR)L"Phân loại"; SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
                lvc.cx = (int)(130 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"Kích thước"; SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

                g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                    WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_PROGRESS, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hProgressBar, PBM_SETRANGE32, 0, 100);
                SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

                g_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Sẵn sàng  •  Nhấp đúp thư mục để duyệt, hoặc chọn tệp tin và bấm 'Tải file về'",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_STATUS, GetModuleHandle(NULL), NULL);

                g_hFontNormal = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                g_hFontBtn = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hAddressEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendMessageW(g_hFileList, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

                g_curPath = "";
                return 0;
            }

            case WM_CTLCOLORSTATIC: {
                HDC hdcStatic = (HDC)wp;
                SetTextColor(hdcStatic, RGB(100, 116, 139));
                SetBkMode(hdcStatic, TRANSPARENT);
                if (!g_hBgBrush) g_hBgBrush = CreateSolidBrush(RGB(243, 244, 246));
                return (LRESULT)g_hBgBrush;
            }

            case WM_CTLCOLOREDIT: {
                HDC hdcEdit = (HDC)wp;
                SetTextColor(hdcEdit, RGB(30, 41, 59));
                SetBkColor(hdcEdit, RGB(255, 255, 255));
                static HBRUSH hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
                return (LRESULT)hWhiteBrush;
            }

            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lp;
                if (!pDIS) break;

                bool isHover = (pDIS->hwndItem == g_hHoveredBtn);
                bool isPressed = (pDIS->itemState & ODS_SELECTED);
                Gdiplus::Graphics g(pDIS->hDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                Gdiplus::RectF btnRect((float)pDIS->rcItem.left, (float)pDIS->rcItem.top, (float)(pDIS->rcItem.right - pDIS->rcItem.left), (float)(pDIS->rcItem.bottom - pDIS->rcItem.top));
                Gdiplus::GraphicsPath path;
                AddRoundRectPath(path, btnRect, 6.0f * scale);

                wchar_t text[64] = {0};
                GetWindowTextW(pDIS->hwndItem, text, 63);

                if (pDIS->CtlID == IDC_FILE_DOWNLOAD) {
                    Gdiplus::Color fill = isPressed ? Gdiplus::Color(255, 29, 78, 216) : (isHover ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 59, 130, 246));
                    Gdiplus::SolidBrush fillBrush(fill);
                    g.FillPath(&fillBrush, &path);

                    Gdiplus::FontFamily ff(L"Segoe UI");
                    Gdiplus::Font font(&ff, 12.5f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
                    Gdiplus::StringFormat sf;
                    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    g.DrawString(text, -1, &font, btnRect, &sf, &textBrush);
                } else if (pDIS->CtlID == IDC_FILE_REFRESH) {
                    Gdiplus::Color fill = isPressed ? Gdiplus::Color(255, 226, 232, 240) : (isHover ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 255, 255, 255));
                    Gdiplus::SolidBrush fillBrush(fill);
                    g.FillPath(&fillBrush, &path);

                    Gdiplus::Pen borderPen(Gdiplus::Color(255, 203, 213, 225), 1.0f);
                    g.DrawPath(&borderPen, &path);

                    Gdiplus::Color iconColor = Gdiplus::Color(255, 51, 65, 85);
                    float icX = btnRect.X + 16.0f * scale;
                    float icY = btnRect.Y + btnRect.Height / 2.0f;
                    float r = 5.0f * scale;

                    Gdiplus::Pen arcPen(iconColor, 1.5f * scale);
                    arcPen.SetStartCap(Gdiplus::LineCapRound);
                    arcPen.SetEndCap(Gdiplus::LineCapRound);

                    g.DrawArc(&arcPen, icX - r, icY - r, r * 2.0f, r * 2.0f, 215.0f, 170.0f);
                    float ax1 = icX + r * 0.82f;
                    float ay1 = icY - r * 0.57f;
                    g.DrawLine(&arcPen, ax1, ay1, ax1 - 3.0f * scale, ay1 - 2.5f * scale);
                    g.DrawLine(&arcPen, ax1, ay1, ax1 - 3.0f * scale, ay1 + 2.5f * scale);

                    g.DrawArc(&arcPen, icX - r, icY - r, r * 2.0f, r * 2.0f, 35.0f, 170.0f);
                    float ax2 = icX - r * 0.82f;
                    float ay2 = icY + r * 0.57f;
                    g.DrawLine(&arcPen, ax2, ay2, ax2 + 3.0f * scale, ay2 + 2.5f * scale);
                    g.DrawLine(&arcPen, ax2, ay2, ax2 + 3.0f * scale, ay2 - 2.5f * scale);

                    Gdiplus::FontFamily ff(L"Segoe UI");
                    Gdiplus::Font font(&ff, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush textBrush(iconColor);
                    Gdiplus::StringFormat sf;
                    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF textRect(btnRect.X + 20.0f * scale, btnRect.Y, btnRect.Width - 20.0f * scale, btnRect.Height);
                    g.DrawString(L"Làm mới", -1, &font, textRect, &sf, &textBrush);
                } else {
                    Gdiplus::Color fill = isPressed ? Gdiplus::Color(255, 226, 232, 240) : (isHover ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 255, 255, 255));
                    Gdiplus::SolidBrush fillBrush(fill);
                    g.FillPath(&fillBrush, &path);

                    Gdiplus::Pen borderPen(Gdiplus::Color(255, 203, 213, 225), 1.0f);
                    g.DrawPath(&borderPen, &path);

                    Gdiplus::FontFamily ff(L"Segoe UI");
                    Gdiplus::Font font(&ff, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 51, 65, 85));
                    Gdiplus::StringFormat sf;
                    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    g.DrawString(text, -1, &font, btnRect, &sf, &textBrush);
                }
                return TRUE;
            }

            case WM_GETMINMAXINFO: {
                MINMAXINFO* pMMI = (MINMAXINFO*)lp;
                float scale = GetDpiScale(hwnd);
                pMMI->ptMinTrackSize.x = (LONG)std::round(760.0f * scale);
                pMMI->ptMinTrackSize.y = (LONG)std::round(520.0f * scale);
                return 0;
            }

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                int pad = (int)std::round(16.0f * scale);
                int gap = (int)std::round(8.0f * scale);
                int btnH = (int)std::round(32.0f * scale);
                int navY = pad;

                int btnRootW = (int)std::round(80.0f * scale);
                int btnUpW = (int)std::round(85.0f * scale);
                int btnRefW = (int)std::round(85.0f * scale);
                int btnDlW = (int)std::round(125.0f * scale);

                MoveWindow(g_hBtnRoot, pad, navY, btnRootW, btnH, TRUE);
                MoveWindow(g_hBtnUp, pad + btnRootW + gap, navY, btnUpW, btnH, TRUE);
                MoveWindow(g_hBtnRefresh, pad + btnRootW + gap + btnUpW + gap, navY, btnRefW, btnH, TRUE);

                int leftOffset = pad + btnRootW + gap + btnUpW + gap + btnRefW + gap;
                int rightOffset = w - pad - btnDlW;
                int addrW = rightOffset - leftOffset - gap;
                if (addrW < 100) addrW = 100;

                MoveWindow(g_hAddressEdit, leftOffset, navY, addrW, btnH, TRUE);
                MoveWindow(g_hBtnDownload, rightOffset, navY, btnDlW, btnH, TRUE);

                int progressH = (int)std::round(8.0f * scale);
                int statusH = (int)std::round(24.0f * scale);
                int bottomY = h - pad - statusH - progressH;

                MoveWindow(g_hProgressBar, pad, bottomY, w - pad * 2, progressH, TRUE);
                MoveWindow(g_hStatusLabel, pad, bottomY + progressH + 4, w - pad * 2, statusH, TRUE);

                int listY = navY + btnH + gap + 4;
                int listH = bottomY - listY - gap;
                if (listH < 60) listH = 60;
                MoveWindow(g_hFileList, pad, listY, w - pad * 2, listH, TRUE);

                int colTypeW = (int)std::round(120.0f * scale);
                int colSizeW = (int)std::round(130.0f * scale);
                int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 4;
                int colNameW = (w - pad * 2) - colTypeW - colSizeW - scrollW;
                if (colNameW < (int)(200.0f * scale)) colNameW = (int)(200.0f * scale);

                ListView_SetColumnWidth(g_hFileList, 0, colNameW);
                ListView_SetColumnWidth(g_hFileList, 1, colTypeW);
                ListView_SetColumnWidth(g_hFileList, 2, colSizeW);

                return 0;
            }

            case WM_FILE_DRIVE_LIST: {
                if (!g_hFileList) return 0;
                EnterCriticalSection(&g_fileCs);
                std::string content = g_pendingDriveContent;
                LeaveCriticalSection(&g_fileCs);

                SendMessageW(g_hFileList, LVM_DELETEALLITEMS, 0, 0);
                std::stringstream ss(content);
                std::string line;
                int idx = 0;
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) {
                        InsertFileRow(idx++, Utils::Utf8ToWide(line), L"[Ổ ĐĨA]", L"--");
                    }
                }
                if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, L"Danh sách ổ đĩa");
                SetWindowTextW(hwnd, L"LanRemote - Trình duyệt tệp (Danh sách ổ đĩa)");
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, L"✓ Đã tải danh sách ổ đĩa máy từ xa  •  Nhấp đúp để mở ổ đĩa");
                return 0;
            }

            case WM_FILE_DIR_LIST: {
                if (!g_hFileList) return 0;
                EnterCriticalSection(&g_fileCs);
                std::string content = g_pendingDirContent;
                LeaveCriticalSection(&g_fileCs);

                if (content.rfind("ERROR:", 0) == 0) {
                    g_curPath = g_prevPath;
                    if (g_hAddressEdit) {
                        SetWindowTextW(g_hAddressEdit, g_curPath.empty() ? L"Danh sách ổ đĩa" : Utils::Utf8ToWide(g_curPath).c_str());
                    }
                    MessageBoxW(hwnd, Utils::Utf8ToWide(content).c_str(), L"Lỗi truy cập thư mục", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                SendMessageW(g_hFileList, LVM_DELETEALLITEMS, 0, 0);
                int idx = 0;

                InsertFileRow(idx++, L"..", L"[QUAY LẠI]", L"--");

                std::stringstream ss(content);
                std::string line;
                int fileCount = 0, dirCount = 0;

                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    if (line.rfind("[DIR] ", 0) == 0) {
                        std::string folderName = line.substr(6);
                        InsertFileRow(idx++, Utils::Utf8ToWide(folderName), L"[THƯ MỤC]", L"--");
                        dirCount++;
                    }

                    else if (line.rfind("[FILE] ", 0) == 0) {
                        std::string rest = line.substr(7);
                        size_t barPos = rest.rfind(" | ");
                        if (barPos != std::string::npos) {
                            std::string fileName = rest.substr(0, barPos);
                            std::string sizeStr = rest.substr(barPos + 3);
                            uint64_t byteVal = 0;
                            try {
                                byteVal = std::stoull(sizeStr);
                            } catch (...) {}
                            std::wstring formattedSize = (byteVal > 0) ? FormatBytes(byteVal) : Utils::Utf8ToWide(sizeStr);
                            InsertFileRow(idx++, Utils::Utf8ToWide(fileName), L"[TẬP TIN]", formattedSize);
                            fileCount++;
                        }
                    }
                }

                if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
                SetWindowTextW(hwnd, (L"LanRemote - Trình duyệt tệp (" + Utils::Utf8ToWide(g_curPath) + L")").c_str());

                if (g_hStatusLabel) {
                    std::wstring statusStr = L"✓ Thư mục: " + std::to_wstring(dirCount) + L"  •  Tệp tin: " + std::to_wstring(fileCount) + L"  •  Nhấp đúp hoặc chọn tệp và bấm 'Tải file về'";
                    SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                }
                return 0;
            }

            case WM_FILE_START_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                uint64_t total = g_totalFileSize;
                LeaveCriticalSection(&g_fileCs);

                ShowWindow(g_hProgressBar, SW_SHOW);
                SendMessageW(g_hProgressBar, PBM_SETRANGE32, 0, 100);
                SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

                std::wstring statusStr = L"⏳ Đang tải: " + Utils::Utf8ToWide(dlName) + L" (0 / " + FormatBytes(total) + L" - 0%)";
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                return 0;
            }

            case WM_FILE_CHUNK_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                uint64_t dl = g_downloadedBytes;
                uint64_t total = g_totalFileSize;
                LeaveCriticalSection(&g_fileCs);

                int percent = total > 0 ? (int)((dl * 100) / total) : 0;
                if (percent > 100) percent = 100;
                SendMessageW(g_hProgressBar, PBM_SETPOS, percent, 0);

                std::wstring statusStr = L"⏳ Đang tải: " + Utils::Utf8ToWide(dlName) + L" (" + FormatBytes(dl) + L" / " + FormatBytes(total) + L" - " + std::to_wstring(percent) + L"%)";
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                return 0;
            }

            case WM_FILE_END_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                uint64_t total = g_totalFileSize;
                LeaveCriticalSection(&g_fileCs);

                SendMessageW(g_hProgressBar, PBM_SETPOS, 100, 0);
                std::wstring statusStr = L"✓ Đã tải xong: " + Utils::Utf8ToWide(dlName) + L" (" + FormatBytes(total) + L") - Đã lưu tại thư mục app";
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                return 0;
            }

            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);

                if (code == BN_CLICKED) {
                    if (id == IDC_FILE_ROOT) {
                        NavigateRoot(hwnd);
                        return 0;
                    }
                    if (id == IDC_FILE_UP) {
                        NavigateUp(hwnd);
                        return 0;
                    }
                    if (id == IDC_FILE_REFRESH) {
                        RefreshCurrent(hwnd);
                        return 0;
                    }
                    if (id == IDC_FILE_DOWNLOAD) {
                        TriggerDownloadSelected(hwnd);
                        return 0;
                    }
                }
                break;
            }

            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == IDC_FILE_LIST) {
                    if (nmh->code == NM_DBLCLK && g_client) {
                        TriggerActionOnSelected(hwnd);
                        return 0;
                    }
                    if (nmh->code == NM_CUSTOMDRAW) {
                        LPNMLVCUSTOMDRAW pLVCD = (LPNMLVCUSTOMDRAW)lp;
                        if (pLVCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
                            return CDRF_NOTIFYITEMDRAW;
                        }
                        if (pLVCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                            int itemIdx = (int)pLVCD->nmcd.dwItemSpec;
                            bool isSel = (ListView_GetItemState(g_hFileList, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                            if (isSel) {
                                pLVCD->clrText = RGB(29, 78, 216);
                                pLVCD->clrTextBk = RGB(239, 246, 255);
                            } else {
                                pLVCD->clrText = RGB(30, 41, 59);
                                pLVCD->clrTextBk = (itemIdx % 2 == 1) ? RGB(248, 250, 252) : RGB(255, 255, 255);
                            }
                            return CDRF_DODEFAULT;
                        }
                    }
                }
                break;
            }

            case WM_KEYDOWN: {
                if (wp == VK_F5) {
                    RefreshCurrent(hwnd);
                    return 0;
                }
                if (wp == VK_BACK) {
                    NavigateUp(hwnd);
                    return 0;
                }
                if (wp == VK_RETURN) {
                    TriggerActionOnSelected(hwnd);
                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                DestroyWindow(hwnd);
                g_hFileWnd = NULL;
                g_hFileList = NULL;
                g_hBtnRoot = NULL;
                g_hBtnUp = NULL;
                g_hBtnRefresh = NULL;
                g_hBtnDownload = NULL;
                g_hAddressEdit = NULL;
                g_hProgressBar = NULL;
                g_hStatusLabel = NULL;
                if (g_hImageList) {
                    ImageList_Destroy(g_hImageList);
                    g_hImageList = NULL;
                }
                if (g_hFontNormal) DeleteObject(g_hFontNormal);
                if (g_hFontBtn) DeleteObject(g_hFontBtn);
                if (g_hBgBrush) DeleteObject(g_hBgBrush);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace FileExplorerWindow {

    void Show(HWND hParent, TcpClient* client) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_fileCs);
            g_csInit = true;
        }
        g_client = client;
        if (!g_hFileWnd) {

            WNDCLASSW wc = {};
            wc.lpfnWndProc = FileWndProc;
            wc.lpszClassName = L"FileExpClass";
            if (!g_hBgBrush) g_hBgBrush = CreateSolidBrush(RGB(243, 244, 246));
            wc.hbrBackground = g_hBgBrush;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassW(&wc);

            float scale = 1.0f;
            HDC screenDc = GetDC(NULL);
            if (screenDc) {
                scale = (float)GetDeviceCaps(screenDc, LOGPIXELSY) / 96.0f;
                ReleaseDC(NULL, screenDc);
            }
            int winW = (int)std::round(820.0f * scale);
            int winH = (int)std::round(620.0f * scale);

            int scrW = GetSystemMetrics(SM_CXSCREEN);
            int scrH = GetSystemMetrics(SM_CYSCREEN);
            int winX = (scrW - winW) / 2 + (int)(30 * scale);
            int winY = (scrH - winH) / 2 + (int)(30 * scale);
            if (winX < 40) winX = 40;
            if (winY < 40) winY = 40;

            CreateWindowExW(WS_EX_TOPMOST, L"FileExpClass", L"LanRemote - Trình duyệt tệp (Danh sách ổ đĩa)",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, winX, winY, winW, winH, hParent, NULL, GetModuleHandle(NULL), NULL);

            if (g_client && g_hFileWnd) {
                g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
            }
        } else {
            SetForegroundWindow(g_hFileWnd);
        }
    }

    void OnDriveListResponse(const std::string& content) {
        if (!g_csInit || !g_hFileWnd) return;
        EnterCriticalSection(&g_fileCs);
        g_pendingDriveContent = content;
        LeaveCriticalSection(&g_fileCs);
        PostMessageW(g_hFileWnd, WM_FILE_DRIVE_LIST, 0, 0);
    }

    void OnDirResponse(const std::string& content) {
        if (!g_csInit || !g_hFileWnd) return;
        EnterCriticalSection(&g_fileCs);
        g_pendingDirContent = content;
        LeaveCriticalSection(&g_fileCs);
        PostMessageW(g_hFileWnd, WM_FILE_DIR_LIST, 0, 0);
    }

    void OnFileInfo(const Protocol::FileInfoPayload& info) {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        std::string fileName = info.fileName;

        size_t slashPos = fileName.find_last_of("\\/");
        if (slashPos != std::string::npos) fileName = fileName.substr(slashPos + 1);

        g_downloadingFile = fileName;
        g_totalFileSize = info.fileSize;
        g_downloadedBytes = 0;

        g_fileMgr.OpenFileForWrite(g_downloadingFile);
        LeaveCriticalSection(&g_fileCs);

        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_START_DL, 0, 0);
    }

    void OnFileChunk(const char* data, int size) {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        g_fileMgr.WriteChunk(data, size);
        g_downloadedBytes += size;
        LeaveCriticalSection(&g_fileCs);

        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_CHUNK_DL, 0, 0);
    }

    void OnFileEnd() {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        g_fileMgr.CloseFile();
        LeaveCriticalSection(&g_fileCs);

        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_END_DL, 0, 0);
    }
}
