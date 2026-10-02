#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <gdiplus.h>
#include "ViewerWindow.h"
#include "MainWindow.h"
#include "Dialogs.h"
#include "UITheme.h"
#include "InputHandler.h"
#include "../Core/DataReceiver.h"
#include "../Media/AudioPlayer.h"
#include "../../Shared/Network.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <algorithm>
#include <vector>
#include <mutex>

namespace ViewerWindow {

constexpr int ID_VIEW_SESSION = 2001;
constexpr int ID_VIEW_DISCONNECT = 2002;
constexpr int ID_REMOTE_SCREEN = 2003;
constexpr int ID_TASK_TITLE = 2004;
constexpr int ID_TASK_LIST = 2005;
constexpr UINT_PTR ID_TIMER_TASKS = 2006;
constexpr int ID_MENU_KILL_PROCESS = 2007;

struct ViewerLayout {
    RECT session{};
    RECT disconnect{};
    RECT remote{};
    RECT taskPanel{};
    RECT taskTitle{};
    RECT taskList{};
};

struct ViewerState {
    HWND window = nullptr;
    HWND owner = nullptr;
    HWND session = nullptr;
    HWND disconnect = nullptr;
    HWND remoteScreen = nullptr;
    HWND taskTitle = nullptr;
    HWND taskList = nullptr;

    std::wstring ip;
    std::wstring machineName;
    TcpClient* client = nullptr;

    InputHandler inputHandler;
    DataReceiver receiver;
    AudioPlayer audioPlayer;

    WNDPROC originalScreenProc = nullptr;
    std::mutex frameMutex;
    std::vector<char> latestFrame;

    std::mutex procMutex;
    std::vector<ProcessItem> processes;
    std::vector<ProcessItem> pendingProcesses;
    uint8_t latestCpuUsage = 0;
};

static ViewerState* gCurrentState = nullptr;

static ViewerLayout GetViewerLayout(HWND window) {
    RECT client{};
    GetClientRect(window, &client);

    constexpr int margin = 18;
    constexpr int gap = 14;
    constexpr int topHeight = 58;
    constexpr int taskTitleHeight = 54;
    const int width = client.right;
    const int height = client.bottom;
    const int usableWidth = std::max(300, width - (margin * 2) - gap);
    const int sideWidth = std::clamp(usableWidth / 3, 300, 420);
    const int streamWidth = std::max(1, usableWidth - sideWidth);
    const int workspaceTop = margin + topHeight + gap;
    const int workspaceBottom = std::max(workspaceTop + 1, height - margin);

    ViewerLayout layout{};
    layout.session = { margin, margin, margin + streamWidth, margin + topHeight };
    layout.disconnect = { margin + streamWidth + gap, margin, width - margin, margin + topHeight };
    layout.remote = { margin, workspaceTop, margin + streamWidth, workspaceBottom };
    layout.taskPanel = { margin + streamWidth + gap, workspaceTop, width - margin, workspaceBottom };
    layout.taskTitle = { layout.taskPanel.left, layout.taskPanel.top, layout.taskPanel.right, layout.taskPanel.top + taskTitleHeight };
    layout.taskList = { layout.taskPanel.left, layout.taskTitle.bottom, layout.taskPanel.right, layout.taskPanel.bottom };
    return layout;
}

static void ResizeViewerControls(HWND window, ViewerState* state) {
    const ViewerLayout layout = GetViewerLayout(window);
    const int taskWidth = layout.taskPanel.right - layout.taskPanel.left;
    const int taskHeight = std::max(1, static_cast<int>(layout.taskList.bottom - layout.taskList.top));

    MoveWindow(state->session, layout.session.left, layout.session.top,
               layout.session.right - layout.session.left, layout.session.bottom - layout.session.top, TRUE);
    MoveWindow(state->disconnect, layout.disconnect.left, layout.disconnect.top,
               layout.disconnect.right - layout.disconnect.left, layout.disconnect.bottom - layout.disconnect.top, TRUE);
    MoveWindow(state->remoteScreen, layout.remote.left, layout.remote.top,
               layout.remote.right - layout.remote.left, layout.remote.bottom - layout.remote.top, TRUE);
    MoveWindow(state->taskTitle, layout.taskTitle.left, layout.taskTitle.top,
               taskWidth, layout.taskTitle.bottom - layout.taskTitle.top, TRUE);
    MoveWindow(state->taskList, layout.taskList.left, layout.taskList.top, taskWidth, taskHeight, TRUE);

    const int nameWidth = taskWidth * 45 / 100;
    const int metricWidth = std::max(45, (taskWidth - nameWidth) / 3);
    ListView_SetColumnWidth(state->taskList, 0, nameWidth);
    ListView_SetColumnWidth(state->taskList, 1, metricWidth);
    ListView_SetColumnWidth(state->taskList, 2, metricWidth);
    ListView_SetColumnWidth(state->taskList, 3, metricWidth);
}

static void InitializeTaskListColumns(HWND list) {
    UITheme::AddListColumn(list, 0, L"Tên", 190);
    UITheme::AddListColumn(list, 1, L"CPU", 75, LVCFMT_RIGHT);
    UITheme::AddListColumn(list, 2, L"Bộ nhớ", 90, LVCFMT_RIGHT);
    UITheme::AddListColumn(list, 3, L"Đĩa", 70, LVCFMT_RIGHT);
}

void UpdateProcessList(const std::vector<ProcessItem>& processList) {
    if (!gCurrentState || !gCurrentState->taskList || !IsWindow(gCurrentState->taskList)) return;
    {
        std::lock_guard<std::mutex> lock(gCurrentState->procMutex);
        gCurrentState->processes = processList;
    }

    SendMessageW(gCurrentState->taskList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(gCurrentState->taskList);

    for (int i = 0; i < static_cast<int>(processList.size()); ++i) {
        UITheme::AddListRow(gCurrentState->taskList, i, processList[i].name.c_str());
        UITheme::SetListItemText(gCurrentState->taskList, i, 1, processList[i].cpu.c_str());
        UITheme::SetListItemText(gCurrentState->taskList, i, 2, processList[i].memory.c_str());
        UITheme::SetListItemText(gCurrentState->taskList, i, 3, processList[i].disk.c_str());
    }
    SendMessageW(gCurrentState->taskList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gCurrentState->taskList, nullptr, TRUE);
}

// Subclass window proc cho remoteScreen để vẽ hình ảnh và bắt thao tác chuột & phím
static LRESULT CALLBACK ScreenSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int canvasW = rc.right - rc.left;
        int canvasH = rc.bottom - rc.top;

        std::vector<char> frameCopy;
        {
            std::lock_guard<std::mutex> lock(state->frameMutex);
            frameCopy = state->latestFrame;
        }

        bool drawn = false;
        if (!frameCopy.empty() && canvasW > 0 && canvasH > 0) {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, frameCopy.size());
            if (hMem) {
                void* pMem = GlobalLock(hMem);
                if (pMem) {
                    memcpy(pMem, frameCopy.data(), frameCopy.size());
                    GlobalUnlock(hMem);

                    IStream* pStream = nullptr;
                    if (CreateStreamOnHGlobal(hMem, FALSE, &pStream) == S_OK) {
                        {
                            Gdiplus::Bitmap bitmap(pStream);
                            if (bitmap.GetLastStatus() == Gdiplus::Ok) {
                                int imgW = bitmap.GetWidth();
                                int imgH = bitmap.GetHeight();
                                if (imgW > 0 && imgH > 0) {
                                    state->inputHandler.SetRemoteResolution(imgW, imgH);
                                }

                                HDC memDC = CreateCompatibleDC(hdc);
                                HBITMAP memBmp = CreateCompatibleBitmap(hdc, canvasW, canvasH);
                                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

                                Gdiplus::Graphics g(memDC);
                                g.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
                                g.DrawImage(&bitmap, 0, 0, canvasW, canvasH);

                                BitBlt(hdc, 0, 0, canvasW, canvasH, memDC, 0, 0, SRCCOPY);
                                SelectObject(memDC, oldBmp);
                                DeleteObject(memBmp);
                                DeleteDC(memDC);
                                drawn = true;
                            }
                        } // bitmap is destroyed here before releasing pStream
                        pStream->Release();
                    }
                }
                GlobalFree(hMem);
            }
        }

        if (!drawn) {
            UITheme::DrawRoundedPanel(hdc, rc, RGB(30, 30, 30), RGB(15, 15, 15));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(56, 189, 248));
            SelectObject(hdc, UITheme::GetNormalFont());
            DrawTextW(hdc, L"[Đang chờ luồng tín hiệu màn hình từ máy Client...]", -1, &rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        return TRUE;

    case WM_MOUSEMOVE:
        state->inputHandler.HandleMouseMove(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        state->inputHandler.HandleMouseButton(hwnd, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_LBUTTONUP:
        state->inputHandler.HandleMouseButton(hwnd, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_RBUTTONDOWN:
        SetFocus(hwnd);
        state->inputHandler.HandleMouseButton(hwnd, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_RBUTTONUP:
        state->inputHandler.HandleMouseButton(hwnd, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_MBUTTONDOWN:
        SetFocus(hwnd);
        state->inputHandler.HandleMouseButton(hwnd, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_MIDDLE, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_MBUTTONUP:
        state->inputHandler.HandleMouseButton(hwnd, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_MIDDLE, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        state->inputHandler.HandleMouseWheel(hwnd, pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
        break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        state->inputHandler.HandleKeyEvent(static_cast<UINT>(wParam), true);
        break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        state->inputHandler.HandleKeyEvent(static_cast<UINT>(wParam), false);
        break;
    }

    return CallWindowProcW(state->originalScreenProc, hwnd, msg, wParam, lParam);
}

static void DrawOwnerControl(const DRAWITEMSTRUCT* draw, ViewerState* state) {
    RECT rect = draw->rcItem;
    HDC dc = draw->hDC;
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    wchar_t controlText[512]{};
    GetWindowTextW(draw->hwndItem, controlText, static_cast<int>(std::size(controlText)));

    SetBkMode(dc, TRANSPARENT);
    if (draw->CtlID == ID_VIEW_SESSION) {
        UITheme::DrawRoundedPanel(dc, rect, RGB(30, 64, 175), RGB(30, 64, 175));
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, UITheme::GetBoldFont());
        rect.left += 20;
        DrawTextW(dc, controlText, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (draw->CtlID == ID_VIEW_DISCONNECT) {
        UITheme::DrawRoundedPanel(dc, rect, pressed ? RGB(187, 45, 59) : RGB(220, 53, 69), RGB(220, 53, 69));
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, UITheme::GetBoldFont());
        DrawTextW(dc, L"Ngắt kết nối", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (draw->CtlID == ID_TASK_TITLE) {
        UITheme::FillSolid(dc, rect, RGB(255, 255, 255));
        SetTextColor(dc, RGB(15, 23, 42));
        SelectObject(dc, UITheme::GetBoldFont());
        rect.left += 16;
        DrawTextW(dc, L"Tiến trình đang chạy", -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static LRESULT CALLBACK ViewerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<ViewerState*>(create->lpCreateParams);
        state->window = window;
        gCurrentState = state;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        std::wstring sessionText = L"Địa chỉ IP / Tên máy: " + state->ip + L" / " + state->machineName;

        state->session = CreateWindowExW(0, L"STATIC", sessionText.c_str(), WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                         0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_VIEW_SESSION), nullptr, nullptr);
        state->disconnect = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_VIEW_DISCONNECT), nullptr, nullptr);
        state->remoteScreen = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                                              0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_REMOTE_SCREEN), nullptr, nullptr);
        state->taskTitle = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                           0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_TASK_TITLE), nullptr, nullptr);
        state->taskList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                          0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_TASK_LIST), nullptr, nullptr);

        SetWindowLongPtrW(state->remoteScreen, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->originalScreenProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(state->remoteScreen, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ScreenSubclassProc)));

        UITheme::SetControlFont(state->session);
        UITheme::SetControlFont(state->disconnect);
        UITheme::SetControlFont(state->remoteScreen);
        UITheme::SetControlFont(state->taskTitle);
        UITheme::SetControlFont(state->taskList);
        ListView_SetExtendedListViewStyle(state->taskList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        InitializeTaskListColumns(state->taskList);
        ResizeViewerControls(window, state);

        state->audioPlayer.Initialize(44100, 2, 16);

        // Khởi động luồng nhận gói tin
        if (state->client) {
            state->inputHandler.SetClient(state->client);
            state->receiver.SetCallbacks(
                [state](const std::vector<char>& frameData) {
                    std::lock_guard<std::mutex> lock(state->frameMutex);
                    state->latestFrame = frameData;
                },
                [state](const Protocol::SysInfoPayload& sysInfo) {
                    state->latestCpuUsage = sysInfo.cpuUsage;
                },
                [state](const std::vector<Protocol::ProcessInfoPayload>& procList) {
                    std::vector<ProcessItem> items;
                    items.reserve(procList.size());
                    for (const auto& p : procList) {
                        ProcessItem item;
                        item.pid = p.pid;
                        item.name = Utils::Utf8ToWide(p.name);
                        item.cpu = std::to_wstring(state->latestCpuUsage) + L"%";
                        item.memory = std::to_wstring(p.ramUsageMB) + L" MB";
                        item.disk = std::to_wstring(p.diskUsageMB) + L" MB";
                        item.usageLevel = (p.ramUsageMB > 500) ? 3 : (p.ramUsageMB > 200 ? 2 : 1);
                        items.push_back(item);
                    }
                    {
                        std::lock_guard<std::mutex> lock(state->procMutex);
                        state->pendingProcesses = std::move(items);
                    }
                    if (state->window && IsWindow(state->window)) {
                        PostMessageW(state->window, WM_DATA_PROCLIST, 0, 0);
                    }
                },
                [state](const std::vector<char>& audioData) {
                    state->audioPlayer.PlayChunk(audioData.data(), static_cast<int>(audioData.size()));
                }
            );
            state->receiver.Start(state->client, window);

            // Bắt đầu timer định kỳ gửi yêu cầu cập nhật tiến trình và thông số hệ thống
            SetTimer(window, ID_TIMER_TASKS, 2000, nullptr);
            state->client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
            state->client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
        }

        return 0;
    }
    case WM_TIMER:
        if (wParam == ID_TIMER_TASKS && state && state->client) {
            state->client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
            state->client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
        }
        return 0;
    case WM_SIZE:
        if (state) {
            ResizeViewerControls(window, state);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_VIEW_DISCONNECT && HIWORD(wParam) == BN_CLICKED) {
            if (state && state->client) {
                state->client->SendPacket(Protocol::MSG_DISCONNECT, nullptr, 0);
            }
            DestroyWindow(window);
        } else if (LOWORD(wParam) == ID_MENU_KILL_PROCESS) {
            if (state && state->taskList && state->client) {
                int selected = ListView_GetNextItem(state->taskList, -1, LVNI_SELECTED);
                std::lock_guard<std::mutex> lock(state->procMutex);
                if (selected >= 0 && selected < static_cast<int>(state->processes.size())) {
                    uint32_t pidToKill = state->processes[selected].pid;
                    state->client->SendPacket(Protocol::MSG_KILL_PROCESS, reinterpret_cast<const char*>(&pidToKill), sizeof(pidToKill));
                }
            }
        }
        return 0;
    case WM_CONTEXTMENU: {
        HWND target = reinterpret_cast<HWND>(wParam);
        if (state && target == state->taskList) {
            int selected = ListView_GetNextItem(state->taskList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, ID_MENU_KILL_PROCESS, L"Dừng tiến trình (Kill Process)");
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, window, nullptr);
                DestroyMenu(hMenu);
            }
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification->idFrom == ID_TASK_LIST && notification->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                return CDRF_NOTIFYSUBITEMDRAW;
            }
            if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM) && draw->iSubItem > 0) {
                const int row = static_cast<int>(draw->nmcd.dwItemSpec);
                std::lock_guard<std::mutex> lock(state->procMutex);
                if (state && row >= 0 && row < static_cast<int>(state->processes.size())) {
                    draw->clrTextBk = UITheme::UsageColor(state->processes[row].usageLevel);
                    draw->clrText = RGB(15, 23, 42);
                }
                return CDRF_NEWFONT;
            }
        }
        return 0;
    }
    case WM_DRAWITEM:
        DrawOwnerControl(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), state);
        return TRUE;

    case WM_DATA_FRAME:
        if (state) {
            state->receiver.ResetFramePending();
            if (state->remoteScreen && IsWindow(state->remoteScreen)) {
                RedrawWindow(state->remoteScreen, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
        }
        return 0;

    case WM_DATA_PROCLIST:
        if (state) {
            std::vector<ProcessItem> items;
            {
                std::lock_guard<std::mutex> lock(state->procMutex);
                items = state->pendingProcesses;
            }
            UpdateProcessList(items);
        }
        return 0;

    case WM_DATA_DISCONNECT:
        Dialogs::ShowInfo(window, L"Ngắt kết nối", L"Kết nối tới Client đã bị ngắt hoặc mất tín hiệu từ xa!");
        DestroyWindow(window);
        return 0;

    case WM_CLOSE:
        if (state && state->client) {
            state->client->SendPacket(Protocol::MSG_DISCONNECT, nullptr, 0);
        }
        DestroyWindow(window);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        UITheme::FillSolid(dc, client, RGB(238, 242, 247));
        const ViewerLayout layout = GetViewerLayout(window);
        UITheme::DrawRoundedPanel(dc, layout.taskPanel, RGB(255, 255, 255), RGB(203, 213, 225));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        if (state) {
            KillTimer(window, ID_TIMER_TASKS);
            HWND owner = state->owner;
            state->receiver.Stop();
            state->audioPlayer.Cleanup();
            if (state->client) {
                state->client->Close();
                delete state->client;
                state->client = nullptr;
            }
            gCurrentState = nullptr;
            delete state;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);

            // Hiện lại giao diện chính (Trang chủ)
            if (IsWindow(owner)) {
                EnableWindow(owner, TRUE);
                ShowWindow(owner, SW_SHOWNORMAL);
                SetWindowPos(owner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetForegroundWindow(owner);
            }
            MainWindow::Show(true);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool Register(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ViewerWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;
    return RegisterClassExW(&wc) != 0;
}

void CreateViewer(HWND owner, const std::wstring& ip, const std::wstring& machineName, TcpClient* client) {
    if (!client) return;

    auto* state = new ViewerState{};
    state->owner = owner;
    state->ip = ip;
    state->machineName = machineName;
    state->client = client;

    HWND viewer = CreateWindowExW(0, kWindowClassName, L"Điều khiển máy tính từ xa",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), state);
    if (!viewer) {
        client->Close();
        delete client;
        delete state;
        Dialogs::ShowError(owner, L"Không thể mở cửa sổ điều khiển từ xa.", false);
        MainWindow::Show(true);
        return;
    }

    ShowWindow(owner, SW_HIDE);
    ShowWindow(viewer, SW_MAXIMIZE);
    UpdateWindow(viewer);
}

} // namespace ViewerWindow

