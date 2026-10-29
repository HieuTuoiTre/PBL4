#include "TaskManagerWindow.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cwchar>
#include <cmath>
#include <unordered_map>
#include <set>

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
    HWND g_hHeader = NULL;
    WNDPROC g_oldHeaderProc = NULL;
    WNDPROC g_oldListProc = NULL;
    WNDPROC g_oldSearchProc = NULL;
    HWND g_hHoveredBtn = NULL;
    HIMAGELIST g_hImageList = NULL;
    TcpClient* g_client = nullptr;

    CRITICAL_SECTION g_dataCs;
    bool g_csInit = false;
    Protocol::SysInfoPayload g_latestSys = {};
    std::vector<Protocol::ProcessInfoPayload> g_latestProcesses;
    std::wstring g_searchQuery = L"";

    // Cấu trúc mô hình hóa danh sách tiến trình dạng Cây gom nhóm (chuẩn Windows Task Manager)
    struct ProcessDisplayRow {
        bool isGroup;                     // true nếu là hàng cha gom nhóm (vd: Zalo 8 PIDs)
        bool isExpanded;                  // true nếu hàng cha đang mở rộng
        std::wstring groupKey;            // Key định danh nhóm chữ thường (vd: L"zalo.exe")
        std::wstring nameDisplay;         // Tên hiển thị cột 0: "\u25B6 Zalo.exe (8)" hoặc "       \u2514 Zalo.exe"
        std::wstring pidDisplay;          // Cột 1: "8 PIDs" hoặc "1240"
        std::wstring ramDisplay;          // Cột 2: "221 MB" hoặc "126 MB"
        std::wstring diskDisplay;         // Cột 3: "0 MB"
        uint32_t pid;                     // PID nếu là con đơn lẻ (hoặc 0)
        std::vector<uint32_t> allPids;    // Danh sách toàn bộ PID cần kill khi chọn hàng này
        std::wstring appDisplayName;      // Tên app sạch hiển thị trong thông báo xác nhận
    };

    std::vector<ProcessDisplayRow> g_currentDisplayRows;
    std::set<std::wstring> g_expandedGroups; // Lưu trạng thái mở rộng của các nhóm

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

    // Cuộn danh sách tiến trình bằng con lăn chuột mượt mà
    void ScrollTaskList(short zDelta) {
        if (!g_hTaskList) return;
        float scale = GetDpiScale(g_hTaskWnd ? g_hTaskWnd : g_hTaskList);
        int lines = -(zDelta / WHEEL_DELTA) * 3;
        int pixelScroll = lines * (int)std::round(30.0f * scale);
        ListView_Scroll(g_hTaskList, 0, pixelScroll);
    }

    LRESULT CALLBACK SearchEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_MOUSEWHEEL) {
            ScrollTaskList(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        return CallWindowProcW(g_oldSearchProc, hWnd, uMsg, wParam, lParam);
    }

    LRESULT CALLBACK TaskListSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_MOUSEWHEEL) {
            ScrollTaskList(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        return CallWindowProcW(g_oldListProc, hWnd, uMsg, wParam, lParam);
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

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            {
                Gdiplus::Graphics g(memDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
                g.FillRectangle(&whiteBrush, 0, 0, width, height);

                Gdiplus::Pen borderPen(Gdiplus::Color(255, 226, 232, 240), 1.0f);
                g.DrawLine(&borderPen, 0, height - 1, width, height - 1);

                int itemCount = Header_GetItemCount(hWnd);
                Gdiplus::FontFamily fontFamily(L"Segoe UI");
                Gdiplus::Font fontHeader(&fontFamily, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 51, 65, 85));

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
            }

            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        return CallWindowProcW(g_oldHeaderProc, hWnd, uMsg, wParam, lParam);
    }

    void RenderProcessListOnUI();

    void ToggleGroupExpand(int itemIndex) {
        if (itemIndex < 0 || itemIndex >= (int)g_currentDisplayRows.size()) return;
        const auto& row = g_currentDisplayRows[itemIndex];
        if (!row.isGroup) return;

        if (g_expandedGroups.count(row.groupKey)) {
            g_expandedGroups.erase(row.groupKey);
        } else {
            g_expandedGroups.insert(row.groupKey);
        }
        RenderProcessListOnUI();
    }

    void TriggerKillSelectedProcess(HWND hwnd) {
        if (!g_hTaskList || !g_client) return;

        int sel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1 || sel >= (int)g_currentDisplayRows.size()) {
            MessageBoxW(hwnd, L"Vui lòng chọn một tiến trình trong bảng trước khi bấm kết thúc tác vụ!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& row = g_currentDisplayRows[sel];
        if (row.allPids.empty()) return;

        if (row.isGroup) {
            // Hàng cha gom nhóm: Xác nhận kill TOÀN BỘ các tiến trình của ứng dụng
            std::wstring confirmMsg = L"Ứng dụng '" + row.appDisplayName + L"' đang chạy với " +
                                      std::to_wstring(row.allPids.size()) + L" tiến trình (" +
                                      row.ramDisplay + L" RAM).\n\n" +
                                      L"Bạn có chắc chắn muốn kết thúc (Kill) TOÀN BỘ " +
                                      std::to_wstring(row.allPids.size()) + L" tiến trình của ứng dụng này không?";
            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận kết thúc toàn bộ ứng dụng", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                for (uint32_t p : row.allPids) {
                    g_client->SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&p, sizeof(p));
                }
                g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
            }
        } else {
            // Hàng con hoặc ứng dụng đơn lẻ: Kill 1 PID
            std::wstring confirmMsg = L"Bạn có chắc chắn muốn kết thúc (Kill) tiến trình '" + row.appDisplayName + L"' (PID: " + std::to_wstring(row.pid) + L") không?";
            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận kết thúc tác vụ", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                uint32_t p = row.pid;
                g_client->SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&p, sizeof(p));
                g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
            }
        }
    }

    void RenderProcessListOnUI() {
        if (!g_hTaskWnd || !g_hTaskList) return;

        EnterCriticalSection(&g_dataCs);
        std::vector<Protocol::ProcessInfoPayload> fullList = g_latestProcesses;
        LeaveCriticalSection(&g_dataCs);

        std::vector<Protocol::ProcessInfoPayload> filteredList;
        std::wstring lowerQuery = g_searchQuery;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);

        for (const auto& item : fullList) {
            if (lowerQuery.empty()) {
                filteredList.push_back(item);
            } else {
                std::wstring nameW = Utils::Utf8ToWide(item.name);
                std::wstring lowerName = nameW;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
                if (lowerName.find(lowerQuery) != std::wstring::npos) {
                    filteredList.push_back(item);
                }
            }
        }

        // Nhóm các tiến trình theo tên file thực thi (.exe)
        struct TempGroup {
            std::wstring appName;
            std::wstring key;
            uint32_t totalRamMB = 0;
            uint32_t totalDiskMB = 0;
            std::vector<Protocol::ProcessInfoPayload> items;
        };

        std::vector<TempGroup> groups;
        std::unordered_map<std::wstring, size_t> keyToIdx;

        for (const auto& item : filteredList) {
            std::wstring nameW = Utils::Utf8ToWide(item.name);
            std::wstring lowerName = nameW;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

            auto it = keyToIdx.find(lowerName);
            if (it != keyToIdx.end()) {
                size_t idx = it->second;
                groups[idx].totalRamMB += item.ramUsageMB;
                groups[idx].totalDiskMB += item.diskUsageMB;
                groups[idx].items.push_back(item);
            } else {
                TempGroup grp;
                grp.appName = nameW;
                grp.key = lowerName;
                grp.totalRamMB = item.ramUsageMB;
                grp.totalDiskMB = item.diskUsageMB;
                grp.items.push_back(item);
                keyToIdx[lowerName] = groups.size();
                groups.push_back(grp);
            }
        }

        // Sắp xếp các nhóm ứng dụng theo tổng RAM giảm dần (những app nặng nhất như Edge, Zalo lên trên cùng)
        std::sort(groups.begin(), groups.end(), [](const TempGroup& a, const TempGroup& b) {
            if (a.totalRamMB != b.totalRamMB) return a.totalRamMB > b.totalRamMB;
            return a.appName < b.appName;
        });

        // Với mỗi nhóm, sắp xếp các tiến trình con bên trong theo RAM giảm dần
        for (auto& grp : groups) {
            std::sort(grp.items.begin(), grp.items.end(), [](const Protocol::ProcessInfoPayload& a, const Protocol::ProcessInfoPayload& b) {
                if (a.ramUsageMB != b.ramUsageMB) return a.ramUsageMB > b.ramUsageMB;
                return a.pid < b.pid;
            });
        }

        // Lưu lại vị trí hàng đang được chọn trước khi cập nhật
        std::wstring selectedRowId = L"";
        int curSel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (curSel >= 0 && curSel < (int)g_currentDisplayRows.size()) {
            const auto& prevRow = g_currentDisplayRows[curSel];
            if (prevRow.isGroup) {
                selectedRowId = L"grp:" + prevRow.groupKey;
            } else {
                selectedRowId = L"pid:" + std::to_wstring(prevRow.pid);
            }
        }

        // Xây dựng danh sách hiển thị phẳng g_currentDisplayRows
        g_currentDisplayRows.clear();
        for (const auto& grp : groups) {
            if (grp.items.size() == 1) {
                // Ứng dụng chỉ có 1 tiến trình: hiển thị phẳng
                ProcessDisplayRow row;
                row.isGroup = false;
                row.isExpanded = false;
                row.groupKey = grp.key;
                row.nameDisplay = grp.appName;
                row.pidDisplay = std::to_wstring(grp.items[0].pid);
                row.ramDisplay = std::to_wstring(grp.items[0].ramUsageMB) + L" MB";
                row.diskDisplay = std::to_wstring(grp.items[0].diskUsageMB) + L" MB";
                row.pid = grp.items[0].pid;
                row.allPids = { grp.items[0].pid };
                row.appDisplayName = grp.appName;
                g_currentDisplayRows.push_back(row);
            } else {
                // Ứng dụng đa tiến trình (như Edge, Zalo, Chrome...): Tạo hàng cha gom nhóm
                bool isExp = (g_expandedGroups.count(grp.key) > 0);
                std::vector<uint32_t> childPids;
                for (const auto& c : grp.items) childPids.push_back(c.pid);

                ProcessDisplayRow groupRow;
                groupRow.isGroup = true;
                groupRow.isExpanded = isExp;
                groupRow.groupKey = grp.key;
                // Dùng mã Unicode \u25BC (▼) và \u25B6 (▶) an toàn tuyệt đối, không lo lỗi font
                groupRow.nameDisplay = (isExp ? L"\u25BC " : L"\u25B6 ") + grp.appName + L" (" + std::to_wstring(grp.items.size()) + L")";
                groupRow.pidDisplay = std::to_wstring(grp.items.size()) + L" PIDs";
                groupRow.ramDisplay = std::to_wstring(grp.totalRamMB) + L" MB";
                groupRow.diskDisplay = std::to_wstring(grp.totalDiskMB) + L" MB";
                groupRow.pid = 0;
                groupRow.allPids = childPids;
                groupRow.appDisplayName = grp.appName;
                g_currentDisplayRows.push_back(groupRow);

                if (isExp) {
                    // Nếu đang mở rộng: Thêm các hàng con thụt lề
                    for (const auto& c : grp.items) {
                        ProcessDisplayRow childRow;
                        childRow.isGroup = false;
                        childRow.isExpanded = false;
                        childRow.groupKey = grp.key;
                        childRow.nameDisplay = L"       \u2514 " + grp.appName;
                        childRow.pidDisplay = std::to_wstring(c.pid);
                        childRow.ramDisplay = std::to_wstring(c.ramUsageMB) + L" MB";
                        childRow.diskDisplay = std::to_wstring(c.diskUsageMB) + L" MB";
                        childRow.pid = c.pid;
                        childRow.allPids = { c.pid };
                        childRow.appDisplayName = grp.appName + L" (PID: " + std::to_wstring(c.pid) + L")";
                        g_currentDisplayRows.push_back(childRow);
                    }
                }
            }
        }

        // Đồng bộ danh sách hiển thị vào Win32 ListView
        int count = (int)g_currentDisplayRows.size();
        SendMessageW(g_hTaskList, WM_SETREDRAW, FALSE, 0);

        int existingCount = (int)SendMessageW(g_hTaskList, LVM_GETITEMCOUNT, 0, 0);

        for (int i = 0; i < count; i++) {
            const auto& row = g_currentDisplayRows[i];

            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)row.nameDisplay.c_str();

            if (i >= existingCount) {
                SendMessageW(g_hTaskList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            } else {
                SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
            }

            lvi.iSubItem = 1;
            lvi.pszText = (LPWSTR)row.pidDisplay.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            lvi.iSubItem = 2;
            lvi.pszText = (LPWSTR)row.ramDisplay.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            lvi.iSubItem = 3;
            lvi.pszText = (LPWSTR)row.diskDisplay.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            // Khôi phục lựa chọn
            std::wstring rowId = row.isGroup ? (L"grp:" + row.groupKey) : (L"pid:" + std::to_wstring(row.pid));
            if (!selectedRowId.empty() && rowId == selectedRowId) {
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
            std::wstring statusStr = L"✓ Đang hiển thị " + std::to_wstring(groups.size()) + L" ứng dụng (" +
                                     std::to_wstring(fullList.size()) + L" tiến trình)";
            if (!g_searchQuery.empty()) {
                statusStr += L" (Lọc: \"" + g_searchQuery + L"\")";
            }
            statusStr += L"  •  Lăn chuột để xem danh sách  •  Nhấp đúp hoặc bấm \u25B6 để mở rộng  •  Chọn và bấm 'Kết thúc tác vụ' để đóng app";
            SetWindowTextW(g_hStatusLabel, statusStr.c_str());
        }
    }

    void PaintHeaderCards(HWND hwnd, HDC hdc, int width, int headerH) {
        float scale = GetDpiScale(hwnd);
        if (width <= 0 || headerH <= 0) return;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, headerH);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 243, 244, 246));
            g.FillRectangle(&bgBrush, 0, 0, width, headerH);

            EnterCriticalSection(&g_dataCs);
            Protocol::SysInfoPayload sys = g_latestSys;
            int procCount = (int)g_latestProcesses.size();
            LeaveCriticalSection(&g_dataCs);

            float padX = 16.0f * scale;
            float cardGap = 12.0f * scale;
            float cardY = 12.0f * scale;
            float cardH = (float)headerH - 24.0f * scale;
            float totalGaps = cardGap * 3.0f + padX * 2.0f;
            float cardW = (width - totalGaps) / 4.0f;
            if (cardW < 100.0f) cardW = 100.0f;

            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font fontTitle(&fontFamily, 11.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font fontVal(&fontFamily, 22.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font fontSub(&fontFamily, 10.5f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

            struct StatCard {
                std::wstring title;
                std::wstring value;
                int percent;
                Gdiplus::Color accentColor;
            };

            Gdiplus::Color green(255, 16, 185, 129);
            Gdiplus::Color yellow(255, 234, 179, 8);
            Gdiplus::Color red(255, 239, 68, 68);
            Gdiplus::Color blue(255, 37, 99, 235);

            auto GetUsageColor = [&](int p) {
                if (p >= 85) return red;
                if (p >= 65) return yellow;
                return green;
            };

            StatCard cards[4] = {
                { L"CPU USAGE", std::to_wstring(sys.cpuUsage) + L"%", sys.cpuUsage, GetUsageColor(sys.cpuUsage) },
                { L"RAM MEMORY", std::to_wstring(sys.ramUsage) + L"%", sys.ramUsage, GetUsageColor(sys.ramUsage) },
                { L"DISK ACTIVE", std::to_wstring(sys.diskUsage) + L"%", sys.diskUsage, GetUsageColor(sys.diskUsage) },
                { L"PROCESSES", std::to_wstring(procCount), -1, blue }
            };

            Gdiplus::GraphicsPath path;
            float radius = 8.0f * scale;

            Gdiplus::StringFormat sf;
            sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
            sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

            for (int i = 0; i < 4; i++) {
                float cx = padX + i * (cardW + cardGap);
                Gdiplus::RectF cr(cx, cardY, cardW, cardH);

                AddRoundRectPath(path, cr, radius);
                Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 255, 255, 255));
                g.FillPath(&cardBg, &path);
                Gdiplus::Pen cardBorder(Gdiplus::Color(255, 226, 232, 240), 1.0f);
                g.DrawPath(&cardBorder, &path);

                Gdiplus::RectF titleRect(cx + 14.0f * scale, cardY + 12.0f * scale, cardW - 28.0f * scale, 16.0f * scale);
                Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 100, 116, 139));
                g.DrawString(cards[i].title.c_str(), -1, &fontTitle, titleRect, &sf, &titleBrush);

                Gdiplus::RectF valRect(cx + 14.0f * scale, cardY + 30.0f * scale, cardW - 28.0f * scale, 28.0f * scale);
                Gdiplus::SolidBrush valBrush(cards[i].accentColor);
                g.DrawString(cards[i].value.c_str(), -1, &fontVal, valRect, &sf, &valBrush);

                if (cards[i].percent >= 0) {
                    float barH = 4.0f * scale;
                    float barY = cardY + cardH - barH - 12.0f * scale;
                    float barW = cardW - 28.0f * scale;

                    Gdiplus::RectF barBgRect(cx + 14.0f * scale, barY, barW, barH);
                    Gdiplus::GraphicsPath barBgPath;
                    AddRoundRectPath(barBgPath, barBgRect, 2.0f * scale);
                    Gdiplus::SolidBrush barBg(Gdiplus::Color(255, 241, 245, 249));
                    g.FillPath(&barBg, &barBgPath);

                    float fillW = barW * (float)cards[i].percent / 100.0f;
                    if (fillW > barW) fillW = barW;
                    if (fillW > 0.0f) {
                        Gdiplus::RectF barFillRect(cx + 14.0f * scale, barY, fillW, barH);
                        Gdiplus::GraphicsPath barFillPath;
                        AddRoundRectPath(barFillPath, barFillRect, 2.0f * scale);
                        Gdiplus::SolidBrush barFill(cards[i].accentColor);
                        g.FillPath(&barFill, &barFillPath);
                    }
                } else {
                    float dotSize = 6.0f * scale;
                    float dotX = cx + 14.0f * scale;
                    float dotY = cardY + cardH - dotSize - 12.0f * scale;
                    Gdiplus::SolidBrush dotBrush(blue);
                    g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

                    Gdiplus::RectF subRect(dotX + dotSize + 6.0f * scale, dotY - 2.0f * scale, cardW - 40.0f * scale, 14.0f * scale);
                    Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 100, 116, 139));
                    g.DrawString(L"Đang hoạt động", -1, &fontSub, subRect, &sf, &subBrush);
                }
            }
        }

        BitBlt(hdc, 0, 0, width, headerH, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
    }

    LRESULT CALLBACK TaskWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);
        int headerH = (int)std::round(105.0f * scale);

        switch (msg) {
            case WM_CREATE: {
                g_hTaskWnd = hwnd;

                g_hSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_SEARCH, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"🔍 Tìm kiếm tiến trình theo Tên hoặc PID...");
                g_oldSearchProc = (WNDPROC)SetWindowLongPtrW(g_hSearchEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditSubclassProc);

                g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"Làm mới",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_REFRESH, GetModuleHandle(NULL), NULL);

                g_hBtnKill = CreateWindowExW(0, L"BUTTON", L"✕ Kết thúc tác vụ",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_KILL, GetModuleHandle(NULL), NULL);

                g_hTaskList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_LIST, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hTaskList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

                g_oldListProc = (WNDPROC)SetWindowLongPtrW(g_hTaskList, GWLP_WNDPROC, (LONG_PTR)TaskListSubclassProc);

                g_hImageList = ImageList_Create(1, (int)std::round(30.0f * scale), ILC_COLOR32, 1, 1);
                ListView_SetImageList(g_hTaskList, g_hImageList, LVSIL_SMALL);

                g_hHeader = ListView_GetHeader(g_hTaskList);
                if (g_hHeader) {
                    g_oldHeaderProc = (WNDPROC)SetWindowLongPtrW(g_hHeader, GWLP_WNDPROC, (LONG_PTR)HeaderSubclassProc);
                }

                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, (int)(280 * scale), (LPWSTR)L"Tên tiến trình", 0, 0 };
                SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = (int)(85 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"PID"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
                lvc.cx = (int)(115 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"Bộ nhớ (RAM)"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
                lvc.cx = (int)(120 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"Ổ đĩa (Disk)"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

                g_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Đang kết nối nhận thông tin tiến trình...",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                    0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);

                g_hFontNormal = CreateFontW((int)(-13 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                g_hFontBtn = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendMessageW(g_hTaskList, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

                SetFocus(g_hTaskList);

                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                SetTimer(hwnd, 1, 3000, NULL);
                return 0;
            }

            case WM_MOUSEWHEEL: {
                ScrollTaskList(GET_WHEEL_DELTA_WPARAM(wp));
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;

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

                if (pDIS->CtlID == IDC_TASK_KILL) {
                    Gdiplus::Color fill = isPressed ? Gdiplus::Color(255, 185, 28, 28) : (isHover ? Gdiplus::Color(255, 220, 38, 38) : Gdiplus::Color(255, 239, 68, 68));
                    Gdiplus::SolidBrush fillBrush(fill);
                    g.FillPath(&fillBrush, &path);

                    Gdiplus::FontFamily ff(L"Segoe UI");
                    Gdiplus::Font font(&ff, 12.5f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
                    Gdiplus::StringFormat sf;
                    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    g.DrawString(L"✕ Kết thúc tác vụ", -1, &font, btnRect, &sf, &textBrush);
                } else if (pDIS->CtlID == IDC_TASK_REFRESH) {
                    Gdiplus::Color fill = isPressed ? Gdiplus::Color(255, 226, 232, 240) : (isHover ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 255, 255, 255));
                    Gdiplus::SolidBrush fillBrush(fill);
                    g.FillPath(&fillBrush, &path);

                    Gdiplus::Pen borderPen(Gdiplus::Color(255, 203, 213, 225), 1.0f);
                    g.DrawPath(&borderPen, &path);

                    Gdiplus::Color iconColor = Gdiplus::Color(255, 51, 65, 85);
                    float icX = btnRect.X + 20.0f * scale;
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
                    Gdiplus::RectF textRect(btnRect.X + 24.0f * scale, btnRect.Y, btnRect.Width - 24.0f * scale, btnRect.Height);
                    g.DrawString(L"Làm mới", -1, &font, textRect, &sf, &textBrush);
                }
                return TRUE;
            }

            case WM_GETMINMAXINFO: {
                MINMAXINFO* pMMI = (MINMAXINFO*)lp;
                pMMI->ptMinTrackSize.x = (LONG)std::round(760.0f * scale);
                pMMI->ptMinTrackSize.y = (LONG)std::round(520.0f * scale);
                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);

                PaintHeaderCards(hwnd, hdc, r.right, headerH);

                RECT bottomBg = { 0, headerH, r.right, r.bottom };
                if (!g_hBgBrush) g_hBgBrush = CreateSolidBrush(RGB(243, 244, 246));
                FillRect(hdc, &bottomBg, g_hBgBrush);

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
                int actionH = (int)std::round(44.0f * scale);
                int statusH = (int)std::round(24.0f * scale);
                int pad = (int)std::round(16.0f * scale);
                int gap = (int)std::round(10.0f * scale);
                int btnH = (int)std::round(32.0f * scale);

                int actionY = headerH + (actionH - btnH) / 2;
                int btnRefW = (int)std::round(105.0f * scale);
                int btnKillW = (int)std::round(150.0f * scale);

                int killX = w - pad - btnKillW;
                int refX = killX - gap - btnRefW;
                int searchW = refX - gap - pad;
                if (searchW < (int)(160 * scale)) searchW = (int)(160 * scale);

                MoveWindow(g_hSearchEdit, pad, actionY, searchW, btnH, TRUE);
                MoveWindow(g_hBtnRefresh, refX, actionY, btnRefW, btnH, TRUE);
                MoveWindow(g_hBtnKill, killX, actionY, btnKillW, btnH, TRUE);

                int listY = headerH + actionH;
                int listH = h - listY - statusH - pad;
                if (listH < 60) listH = 60;
                MoveWindow(g_hTaskList, pad, listY, w - pad * 2, listH, TRUE);

                int colPidW = (int)std::round(85.0f * scale);
                int colRamW = (int)std::round(115.0f * scale);
                int colDiskW = (int)std::round(115.0f * scale);
                int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 4;
                int colNameW = (w - pad * 2) - colPidW - colRamW - colDiskW - scrollW;
                if (colNameW < (int)(200.0f * scale)) colNameW = (int)(200.0f * scale);

                ListView_SetColumnWidth(g_hTaskList, 0, colNameW);
                ListView_SetColumnWidth(g_hTaskList, 1, colPidW);
                ListView_SetColumnWidth(g_hTaskList, 2, colRamW);
                ListView_SetColumnWidth(g_hTaskList, 3, colDiskW);

                int statusY = listY + listH + 4;
                MoveWindow(g_hStatusLabel, pad, statusY, w - pad * 2, statusH, TRUE);

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
                if (nmh->idFrom == IDC_TASK_LIST) {
                    if (nmh->code == NM_DBLCLK) {
                        int sel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                        if (sel >= 0 && sel < (int)g_currentDisplayRows.size()) {
                            if (g_currentDisplayRows[sel].isGroup) {
                                ToggleGroupExpand(sel);
                            } else {
                                TriggerKillSelectedProcess(hwnd);
                            }
                        }
                        return 0;
                    }
                    if (nmh->code == NM_CLICK) {
                        // Click vào vùng mũi tên \u25B6 / \u25BC ở đầu hàng
                        LVHITTESTINFO hti = {};
                        GetCursorPos(&hti.pt);
                        ScreenToClient(g_hTaskList, &hti.pt);
                        SendMessageW(g_hTaskList, LVM_SUBITEMHITTEST, 0, (LPARAM)&hti);
                        if (hti.iItem >= 0 && hti.iItem < (int)g_currentDisplayRows.size() && hti.iSubItem == 0) {
                            if (g_currentDisplayRows[hti.iItem].isGroup) {
                                RECT itemRc = {};
                                ListView_GetItemRect(g_hTaskList, hti.iItem, &itemRc, LVIR_LABEL);
                                if (hti.pt.x < itemRc.left + 28) {
                                    ToggleGroupExpand(hti.iItem);
                                    return 0;
                                }
                            }
                        }
                    }
                    if (nmh->code == NM_CUSTOMDRAW) {
                        LPNMLVCUSTOMDRAW pLVCD = (LPNMLVCUSTOMDRAW)lp;
                        if (pLVCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
                            return CDRF_NOTIFYITEMDRAW;
                        }
                        if (pLVCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                            int itemIdx = (int)pLVCD->nmcd.dwItemSpec;
                            bool isSel = (ListView_GetItemState(g_hTaskList, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                            if (isSel) {
                                pLVCD->clrText = RGB(29, 78, 216);      // Blue primary
                                pLVCD->clrTextBk = RGB(239, 246, 255);  // Soft blue highlight
                            } else if (itemIdx >= 0 && itemIdx < (int)g_currentDisplayRows.size() && g_currentDisplayRows[itemIdx].isGroup) {
                                pLVCD->clrText = RGB(15, 23, 42);       // Dark Slate đậm cho hàng cha
                                pLVCD->clrTextBk = (itemIdx % 2 == 1) ? RGB(241, 245, 249) : RGB(248, 250, 252);
                            } else {
                                pLVCD->clrText = RGB(71, 85, 105);      // Muted Slate cho hàng con
                                pLVCD->clrTextBk = RGB(255, 255, 255);  // White background
                            }
                            return CDRF_DODEFAULT;
                        }
                    }
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
                if (wp == VK_RETURN || wp == VK_SPACE) {
                    int sel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < (int)g_currentDisplayRows.size()) {
                        if (g_currentDisplayRows[sel].isGroup) {
                            ToggleGroupExpand(sel);
                            return 0;
                        }
                    }
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
            int winW = (int)std::round(840.0f * scale);
            int winH = (int)std::round(640.0f * scale);

            int scrW = GetSystemMetrics(SM_CXSCREEN);
            int scrH = GetSystemMetrics(SM_CYSCREEN);
            int winX = (scrW - winW) / 2;
            int winY = (scrH - winH) / 2;
            if (winX < 40) winX = 40;
            if (winY < 40) winY = 40;

            CreateWindowExW(WS_EX_TOPMOST, L"TaskClassW", L"LanRemote - Quản lý Tác vụ từ xa (Task Manager)",
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
