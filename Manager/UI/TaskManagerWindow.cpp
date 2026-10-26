#include "TaskManagerWindow.h"

#include "../../Shared/Utils.h"

#include <commctrl.h>

#include <gdiplus.h>

#include <vector>
#include <string>
#include <algorithm>
#include <cwchar>
#include <cmath>

#define WM_TASK_UPDATE_SYS  (WM_APP + 101)
#define WM_TASK_UPDATE_LIST (WM_APP + 102)

#define IDC_TASK_LIST    1001
#define IDC_TASK_REFRESH 1002
#define IDC_TASK_KILL    1003
#define IDC_TASK_SEARCH  1004

namespace {

    HWND g_hTaskWnd = NULL;
    HWND g_hTaskList = NULL;
    HWND g_hSearchEdit = NULL;
    HWND g_hBtnRefresh = NULL;
    HWND g_hBtnKill = NULL;
    HWND g_hStatusLabel = NULL;
    TcpClient* g_client = nullptr;

    CRITICAL_SECTION g_dataCs;
    bool g_csInit = false;
    Protocol::SysInfoPayload g_latestSys = {};
    std::vector<Protocol::ProcessInfoPayload> g_latestProcesses;
    std::wstring g_searchQuery = L"";

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

    void TriggerKillSelectedProcess(HWND hwnd) {
        if (!g_hTaskList || !g_client) return;

        int sel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1) {
            MessageBoxW(hwnd, L"Vui lòng nhấp chọn một tiến trình trong bảng trước khi bấm kết thúc tác vụ!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
            return;
        }

        wchar_t nameBuf[256] = {0};
        wchar_t pidBuf[32] = {0};
        LVITEMW lvi = {};
        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 256;
        SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);
        lvi.iSubItem = 1; lvi.pszText = pidBuf; lvi.cchTextMax = 32;
        SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

        if (wcslen(pidBuf) > 0) {
            wchar_t* endPtr = nullptr;
            uint32_t pid = (uint32_t)wcstoul(pidBuf, &endPtr, 10);
            if (endPtr != pidBuf && pid > 0) {

                std::wstring confirmMsg = L"Bạn có chắc chắn muốn tắt (Kill) tiến trình '" + std::wstring(nameBuf) + L"' (PID: " + std::wstring(pidBuf) + L") không?";
                if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận kết thúc tác vụ", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {

                    g_client->SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&pid, sizeof(pid));

                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
            }
        }
    }

    void RenderProcessListOnUI() {
        if (!g_hTaskWnd || !g_hTaskList) return;

        EnterCriticalSection(&g_dataCs);
        std::vector<Protocol::ProcessInfoPayload> fullList = g_latestProcesses;
        LeaveCriticalSection(&g_dataCs);

        std::vector<Protocol::ProcessInfoPayload> pList;
        std::wstring lowerQuery = g_searchQuery;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);

        for (const auto& item : fullList) {
            if (lowerQuery.empty()) {
                pList.push_back(item);
            } else {
                std::wstring nameW = Utils::Utf8ToWide(item.name);
                std::wstring lowerName = nameW;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
                if (lowerName.find(lowerQuery) != std::wstring::npos) {
                    pList.push_back(item);
                }
            }
        }

        int count = (int)pList.size();

        std::wstring selectedPid = L"";
        int curSel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (curSel != -1) {
            wchar_t buf[32] = {0};
            LVITEMW lvi = {};
            lvi.iSubItem = 1; lvi.pszText = buf; lvi.cchTextMax = 32;
            SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, curSel, (LPARAM)&lvi);
            selectedPid = buf;
        }

        SendMessageW(g_hTaskList, WM_SETREDRAW, FALSE, 0);

        int existingCount = (int)SendMessageW(g_hTaskList, LVM_GETITEMCOUNT, 0, 0);

        for (int i = 0; i < count; i++) {
            std::wstring nameW = Utils::Utf8ToWide(pList[i].name);
            std::wstring pidW = std::to_wstring(pList[i].pid);
            std::wstring ramW = std::to_wstring(pList[i].ramUsageMB) + L" MB";
            std::wstring diskW = std::to_wstring(pList[i].diskUsageMB) + L" MB";

            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)nameW.c_str();

            if (i >= existingCount) {
                SendMessageW(g_hTaskList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            } else {
                SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
            }

            lvi.iSubItem = 1;
            lvi.pszText = (LPWSTR)pidW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            lvi.iSubItem = 2;
            lvi.pszText = (LPWSTR)ramW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            lvi.iSubItem = 3;
            lvi.pszText = (LPWSTR)diskW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            if (!selectedPid.empty() && pidW == selectedPid) {
                LVITEMW stateItem = {};
                stateItem.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                stateItem.state = LVIS_SELECTED | LVIS_FOCUSED;
                SendMessageW(g_hTaskList, LVM_SETITEMSTATE, i, (LPARAM)&stateItem);
            }
        }

        for (int i = existingCount - 1; i >= count; i--) {
            SendMessageW(g_hTaskList, LVM_DELETEITEM, i, 0);
        }

        SendMessageW(g_hTaskList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_hTaskList, NULL, FALSE);

        if (g_hStatusLabel) {
            std::wstring statusStr = L"Hiển thị: " + std::to_wstring(count) + L" / " + std::to_wstring(fullList.size()) + L" tiến trình";
            if (!g_searchQuery.empty()) {
                statusStr += L" (Lọc: \"" + g_searchQuery + L"\")";
            }
            statusStr += L" | F5: Làm mới | Phím Del hoặc Nhấp đúp: Kết thúc tác vụ";
            SetWindowTextW(g_hStatusLabel, statusStr.c_str());
        }
    }

    void PaintHeaderCards(HWND hwnd, HDC hdc, int width, int headerH) {
        float scale = GetDpiScale(hwnd);
        if (width <= 0 || headerH <= 0) return;

        Gdiplus::Bitmap memBmp(width, headerH, PixelFormat32bppRGB);
        Gdiplus::Graphics g(&memBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 25, 30));
        g.FillRectangle(&bgBrush, 0, 0, width, headerH);

        Gdiplus::Pen borderPen(Gdiplus::Color(255, 45, 47, 56), 1.0f);
        g.DrawLine(&borderPen, 0.0f, (float)(headerH - 1), (float)width, (float)(headerH - 1));

        EnterCriticalSection(&g_dataCs);
        Protocol::SysInfoPayload sys = g_latestSys;
        int procCount = (int)g_latestProcesses.size();
        LeaveCriticalSection(&g_dataCs);

        float padX = 10.0f * scale;
        float cardGap = 8.0f * scale;
        float cardY = 8.0f * scale;
        float cardH = (float)headerH - 16.0f * scale;
        float totalGaps = cardGap * 3.0f + padX * 2.0f;
        float cardW = (width - totalGaps) / 4.0f;
        if (cardW < 80.0f) cardW = 80.0f;

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font fontTitle(&fontFamily, 11.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontVal(&fontFamily, 20.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        struct StatCard {
            std::wstring title;
            std::wstring value;
            int percent;
            Gdiplus::Color accentColor;
        };

        Gdiplus::Color green(255, 34, 197, 94);
        Gdiplus::Color yellow(255, 234, 179, 8);
        Gdiplus::Color red(255, 239, 68, 68);
        Gdiplus::Color blue(255, 59, 130, 246);

        auto GetUsageColor = [&](int p) {
            if (p >= 85) return red;
            if (p >= 65) return yellow;
            return green;
        };

        StatCard cards[4] = {
            { L"CPU", std::to_wstring(sys.cpuUsage) + L"%", sys.cpuUsage, GetUsageColor(sys.cpuUsage) },
            { L"RAM", std::to_wstring(sys.ramUsage) + L"%", sys.ramUsage, GetUsageColor(sys.ramUsage) },
            { L"ĐĨA (DISK)", std::to_wstring(sys.diskUsage) + L"%", sys.diskUsage, GetUsageColor(sys.diskUsage) },
            { L"TIẾN TRÌNH", std::to_wstring(procCount), -1, blue }
        };

        Gdiplus::GraphicsPath path;
        float radius = 6.0f * scale;

        Gdiplus::StringFormat sf;
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);

        for (int i = 0; i < 4; i++) {
            float cx = padX + i * (cardW + cardGap);
            Gdiplus::RectF cr(cx, cardY, cardW, cardH);

            AddRoundRectPath(path, cr, radius);
            Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 34, 36, 44));
            g.FillPath(&cardBg, &path);
            Gdiplus::Pen cardBorder(Gdiplus::Color(255, 52, 55, 68), 1.0f);
            g.DrawPath(&cardBorder, &path);

            Gdiplus::RectF titleRect(cx + 10.0f * scale, cardY + 8.0f * scale, cardW - 20.0f * scale, 18.0f * scale);
            Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 156, 163, 175));
            g.DrawString(cards[i].title.c_str(), -1, &fontTitle, titleRect, &sf, &titleBrush);

            Gdiplus::RectF valRect(cx + 10.0f * scale, cardY + 28.0f * scale, cardW - 20.0f * scale, 28.0f * scale);
            Gdiplus::SolidBrush valBrush(cards[i].accentColor);
            g.DrawString(cards[i].value.c_str(), -1, &fontVal, valRect, &sf, &valBrush);

            if (cards[i].percent >= 0) {
                float barH = 4.0f * scale;
                float barY = cardY + cardH - barH - 8.0f * scale;
                float barW = cardW - 20.0f * scale;
                Gdiplus::SolidBrush barBg(Gdiplus::Color(255, 48, 51, 62));
                g.FillRectangle(&barBg, cx + 10.0f * scale, barY, barW, barH);

                float fillW = barW * (float)cards[i].percent / 100.0f;
                if (fillW > barW) fillW = barW;
                Gdiplus::SolidBrush barFill(cards[i].accentColor);
                g.FillRectangle(&barFill, cx + 10.0f * scale, barY, fillW, barH);
            }
        }

        Gdiplus::Graphics gScreen(hdc);
        gScreen.DrawImage(&memBmp, 0, 0, width, headerH);
    }

    LRESULT CALLBACK TaskWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);
        int headerH = (int)std::round(90.0f * scale);

        switch (msg) {

            case WM_CREATE: {
                g_hTaskWnd = hwnd;

                g_hSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_SEARCH, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Tìm kiếm tiến trình (Tên hoặc PID)...");

                g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"Làm mới (F5)",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_REFRESH, GetModuleHandle(NULL), NULL);

                g_hBtnKill = CreateWindowExW(0, L"BUTTON", L"Kết thúc tác vụ",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_KILL, GetModuleHandle(NULL), NULL);

                g_hTaskList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_LIST, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hTaskList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, (int)(260 * scale), (LPWSTR)L"Tên Tiến Trình", 0, 0 };
                SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = (int)(75 * scale); lvc.pszText = (LPWSTR)L"PID"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
                lvc.cx = (int)(100 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"RAM"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
                lvc.cx = (int)(105 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"Đĩa (MB/s)"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

                g_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Đang tải dữ liệu tiến trình...",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                    0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);

                HFONT hFont = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                HFONT hFontBtn = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessageW(g_hBtnRefresh, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hBtnKill, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                SetTimer(hwnd, 1, 3000, NULL);
                return 0;
            }

            case WM_GETMINMAXINFO: {
                MINMAXINFO* pMMI = (MINMAXINFO*)lp;
                pMMI->ptMinTrackSize.x = (LONG)std::round(720.0f * scale);
                pMMI->ptMinTrackSize.y = (LONG)std::round(500.0f * scale);
                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);
                if (ps.rcPaint.top < headerH) {
                    PaintHeaderCards(hwnd, hdc, r.right, headerH);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_TIMER:
                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                return 0;

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                int actionH = (int)std::round(38.0f * scale);
                int statusH = (int)std::round(22.0f * scale);
                int gap = (int)std::round(8.0f * scale);
                int btnH = (int)std::round(30.0f * scale);

                int actionY = headerH + (actionH - btnH) / 2;
                int btnRefW = (int)std::round(115.0f * scale);
                int btnKillW = (int)std::round(135.0f * scale);

                int killX = w - gap - btnKillW;
                int refX = killX - gap - btnRefW;
                int searchW = refX - gap - gap;
                if (searchW < (int)(150 * scale)) searchW = (int)(150 * scale);

                MoveWindow(g_hSearchEdit, gap, actionY, searchW, btnH, TRUE);
                MoveWindow(g_hBtnRefresh, refX, actionY, btnRefW, btnH, TRUE);
                MoveWindow(g_hBtnKill, killX, actionY, btnKillW, btnH, TRUE);

                int listY = headerH + actionH;
                int listH = h - listY - statusH - gap;
                if (listH < 50) listH = 50;
                MoveWindow(g_hTaskList, gap, listY, w - gap * 2, listH, TRUE);

                int colPidW = (int)std::round(75.0f * scale);
                int colRamW = (int)std::round(100.0f * scale);
                int colDiskW = (int)std::round(105.0f * scale);
                int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 4;
                int colNameW = (w - gap * 2) - colPidW - colRamW - colDiskW - scrollW;
                if (colNameW < (int)(180.0f * scale)) colNameW = (int)(180.0f * scale);

                ListView_SetColumnWidth(g_hTaskList, 0, colNameW);
                ListView_SetColumnWidth(g_hTaskList, 1, colPidW);
                ListView_SetColumnWidth(g_hTaskList, 2, colRamW);
                ListView_SetColumnWidth(g_hTaskList, 3, colDiskW);

                int statusY = listY + listH + 2;
                MoveWindow(g_hStatusLabel, gap, statusY, w - gap * 2, statusH, TRUE);

                RECT headerRect = { 0, 0, w, headerH };
                InvalidateRect(hwnd, &headerRect, FALSE);
                return 0;
            }

            case WM_TASK_UPDATE_SYS: {
                RECT headerRect = { 0, 0, 3000, headerH };
                InvalidateRect(hwnd, &headerRect, FALSE);
                return 0;
            }

            case WM_TASK_UPDATE_LIST: {
                RenderProcessListOnUI();
                return 0;
            }

            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);

                if (id == IDC_TASK_REFRESH && code == BN_CLICKED) {
                    if (g_client) {
                        g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                        g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                    }
                    return 0;
                }

                if (id == IDC_TASK_KILL && code == BN_CLICKED) {
                    TriggerKillSelectedProcess(hwnd);
                    return 0;
                }

                if (id == IDC_TASK_SEARCH && code == EN_CHANGE) {
                    wchar_t buf[128] = {0};
                    GetWindowTextW(g_hSearchEdit, buf, 128);
                    g_searchQuery = buf;
                    RenderProcessListOnUI();
                    return 0;
                }
                break;
            }

            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == IDC_TASK_LIST && nmh->code == NM_DBLCLK) {
                    TriggerKillSelectedProcess(hwnd);
                    return 0;
                }
                break;
            }

            case WM_KEYDOWN: {
                if (wp == VK_F5) {

                    if (g_client) {
                        g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                        g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                    }
                    return 0;
                }
                if (wp == VK_DELETE) {

                    TriggerKillSelectedProcess(hwnd);
                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
                g_hTaskWnd = NULL;
                g_hTaskList = NULL;
                g_hSearchEdit = NULL;
                g_hBtnRefresh = NULL;
                g_hBtnKill = NULL;
                g_hStatusLabel = NULL;
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace TaskManagerWindow {

    void Show(HWND hParent, TcpClient* client) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_dataCs);
            g_csInit = true;
        }
        g_client = client;
        if (!g_hTaskWnd) {

            WNDCLASSW wc = {};
            wc.lpfnWndProc = TaskWndProc;
            wc.lpszClassName = L"TaskClassW";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
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
            int winX = (scrW - winW) / 2;
            int winY = (scrH - winH) / 2;
            if (winX < 40) winX = 40;
            if (winY < 40) winY = 40;

            CreateWindowExW(WS_EX_TOPMOST, L"TaskClassW", L"Task Manager - Quản lý tác vụ từ xa",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, winX, winY, winW, winH, hParent, NULL, GetModuleHandle(NULL), NULL);
        } else {
            SetForegroundWindow(g_hTaskWnd);
        }
    }

    void UpdateSysInfo(const Protocol::SysInfoPayload& sys) {
        if (!g_csInit || !g_hTaskWnd) return;
        EnterCriticalSection(&g_dataCs);
        g_latestSys = sys;
        LeaveCriticalSection(&g_dataCs);
        PostMessageW(g_hTaskWnd, WM_TASK_UPDATE_SYS, 0, 0);
    }

    void UpdateProcessList(const Protocol::ProcessInfoPayload* pList, int count) {
        if (!g_csInit || !g_hTaskWnd || count <= 0 || !pList) return;
        EnterCriticalSection(&g_dataCs);
        g_latestProcesses.assign(pList, pList + count);
        LeaveCriticalSection(&g_dataCs);
        PostMessageW(g_hTaskWnd, WM_TASK_UPDATE_LIST, 0, 0);
    }
}
