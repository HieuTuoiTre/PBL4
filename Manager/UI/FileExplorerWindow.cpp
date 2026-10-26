#include "FileExplorerWindow.h"
#include "../../Shared/Utils.h"
#include "../FileTransfer/FileManager.h"
#include <commctrl.h>
#include <sstream>

#define WM_FILE_DRIVE_LIST (WM_APP + 201)
#define WM_FILE_DIR_LIST   (WM_APP + 202)
#define WM_FILE_START_DL   (WM_APP + 203)
#define WM_FILE_END_DL     (WM_APP + 204)

namespace {
    HWND g_hFileWnd = NULL;
    HWND g_hFileList = NULL;
    TcpClient* g_client = nullptr;
    std::string g_curPath = "";
    FileManager g_fileMgr;
    std::string g_downloadingFile = "";

    CRITICAL_SECTION g_fileCs;
    bool g_csInit = false;
    std::string g_pendingDriveContent;
    std::string g_pendingDirContent;

    void InsertFileRow(int index, const std::wstring& name, const std::wstring& typeOrSize) {
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = index;
        lvi.pszText = (LPWSTR)name.c_str();
        SendMessageW(g_hFileList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 1;
        lvi.pszText = (LPWSTR)typeOrSize.c_str();
        SendMessageW(g_hFileList, LVM_SETITEMTEXTW, index, (LPARAM)&lvi);
    }

    LRESULT CALLBACK FileWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                g_hFileWnd = hwnd;
                g_hFileList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                    0, 0, 0, 0, hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hFileList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, 330, (LPWSTR)L"Tên Thư mục / Tập tin", 0, 0 };
                SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = 150; lvc.pszText = (LPWSTR)L"Loại / Kích thước";
                SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

                g_curPath = "";
                return 0;
            }
            case WM_SIZE:
                MoveWindow(g_hFileList, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
                return 0;

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
                        InsertFileRow(idx++, Utils::Utf8ToWide(line), L"[Ổ ĐĨA]");
                    }
                }
                return 0;
            }

            case WM_FILE_DIR_LIST: {
                if (!g_hFileList) return 0;
                EnterCriticalSection(&g_fileCs);
                std::string content = g_pendingDirContent;
                LeaveCriticalSection(&g_fileCs);

                if (content.rfind("ERROR:", 0) == 0) {
                    MessageBoxW(hwnd, Utils::Utf8ToWide(content).c_str(), L"Lỗi truy cập", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                SendMessageW(g_hFileList, LVM_DELETEALLITEMS, 0, 0);
                int idx = 0;
                InsertFileRow(idx++, L"..", L"[QUAY LẠI]");

                std::stringstream ss(content);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("[DIR] ", 0) == 0) {
                        std::string folderName = line.substr(6);
                        InsertFileRow(idx++, Utils::Utf8ToWide(folderName), L"[THƯ MỤC]");
                    } else if (line.rfind("[FILE] ", 0) == 0) {
                        std::string rest = line.substr(7);
                        size_t barPos = rest.find(" | ");
                        if (barPos != std::string::npos) {
                            std::string fileName = rest.substr(0, barPos);
                            std::string fileSize = rest.substr(barPos + 3);
                            InsertFileRow(idx++, Utils::Utf8ToWide(fileName), Utils::Utf8ToWide(fileSize));
                        }
                    }
                }
                return 0;
            }

            case WM_FILE_START_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                LeaveCriticalSection(&g_fileCs);
                std::wstring title = L"Đang tải: " + Utils::Utf8ToWide(dlName) + L"...";
                SetWindowTextW(hwnd, title.c_str());
                return 0;
            }

            case WM_FILE_END_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                LeaveCriticalSection(&g_fileCs);
                std::wstring title = L"File Explorer - " + Utils::Utf8ToWide(g_curPath);
                SetWindowTextW(hwnd, title.c_str());
                std::wstring msg = L"Đã tải xong file: " + Utils::Utf8ToWide(dlName) + L"\n(Lưu tại thư mục chạy Manager.exe)";
                MessageBoxW(hwnd, msg.c_str(), L"Tải thành công", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == 2 && nmh->code == NM_DBLCLK && g_client) {
                    int sel = (int)SendMessageW(g_hFileList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (sel != -1) {
                        wchar_t nameBuf[260] = {0}, typeBuf[128] = {0};
                        LVITEMW lvi = {};
                        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 260;
                        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);
                        lvi.iSubItem = 1; lvi.pszText = typeBuf; lvi.cchTextMax = 128;
                        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

                        std::string itemName = Utils::WideToUtf8(nameBuf);
                        std::wstring itemType = typeBuf;

                        if (itemName == "..") {
                            if (g_curPath.length() <= 3) {
                                g_curPath = "";
                                SetWindowTextW(hwnd, L"File Explorer - Danh sách ổ đĩa");
                                g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
                            } else {
                                if (g_curPath.back() == '\\') g_curPath.pop_back();
                                size_t pos = g_curPath.find_last_of("\\/");
                                if (pos != std::string::npos) {
                                    g_curPath = g_curPath.substr(0, pos);
                                    if (g_curPath.length() == 2 && g_curPath[1] == ':') g_curPath += "\\";
                                }
                                std::wstring title = L"File Explorer - " + Utils::Utf8ToWide(g_curPath);
                                SetWindowTextW(hwnd, title.c_str());
                                g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
                            }
                        }
                        else if (itemType == L"[Ổ ĐĨA]") {
                            g_curPath = itemName;
                            std::wstring title = L"File Explorer - " + Utils::Utf8ToWide(g_curPath);
                            SetWindowTextW(hwnd, title.c_str());
                            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
                        }
                        else if (itemType == L"[THƯ MỤC]") {
                            if (!g_curPath.empty() && g_curPath.back() != '\\') g_curPath += "\\";
                            g_curPath += itemName;
                            std::wstring title = L"File Explorer - " + Utils::Utf8ToWide(g_curPath);
                            SetWindowTextW(hwnd, title.c_str());
                            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
                        }
                        else {
                            std::wstring confirmMsg = L"Bạn có muốn tải file '" + std::wstring(nameBuf) + L"' về máy không?";
                            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Tải File", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                std::string fullPath = g_curPath;
                                if (!fullPath.empty() && fullPath.back() != '\\') fullPath += "\\";
                                fullPath += itemName;
                                g_client->SendPacket(Protocol::MSG_FILE_DOWNLOAD_REQ, fullPath.c_str(), (int)fullPath.size());
                            }
                        }
                    }
                }
                return 0;
            }
            case WM_CLOSE:
                DestroyWindow(hwnd);
                g_hFileWnd = NULL;
                g_hFileList = NULL;
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
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassW(&wc);

            g_hFileWnd = CreateWindowExW(WS_EX_TOPMOST, L"FileExpClass", L"File Explorer - Danh sách ổ đĩa (Nhấp đúp để mở/tải)",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 150, 150, 520, 500, hParent, NULL, GetModuleHandle(NULL), NULL);

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
        g_downloadingFile = info.fileName;
        g_fileMgr.OpenFileForWrite(g_downloadingFile);
        LeaveCriticalSection(&g_fileCs);
        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_START_DL, 0, 0);
    }

    void OnFileChunk(const char* data, int size) {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        g_fileMgr.WriteChunk(data, size);
        LeaveCriticalSection(&g_fileCs);
    }

    void OnFileEnd() {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        g_fileMgr.CloseFile();
        LeaveCriticalSection(&g_fileCs);
        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_END_DL, 0, 0);
    }
}