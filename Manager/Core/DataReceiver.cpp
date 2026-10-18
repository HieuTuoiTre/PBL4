#include "DataReceiver.h"
#include "../../Shared/Protocol.h"
#include "../UI/RemoteWindow.h"
#include "../UI/TaskManagerWindow.h"
#include "../UI/FileExplorerWindow.h"
#include "../Media/AudioPlayer.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

namespace {
    std::atomic<bool> g_isRunning(false);

    void ReceiveLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            if (hdr.type == Protocol::MSG_FILE_END) {
                FileExplorerWindow::OnFileEnd();
                continue;
            }

            if (hdr.size == 0 || hdr.size > 32 * 1024 * 1024) continue; // Bảo vệ giới hạn gói tối đa 32MB
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            switch (hdr.type) {
                case Protocol::MSG_VIDEO_FRAME: {
                    if (hdr.size <= sizeof(Protocol::VideoDeltaHeader)) break;
                    Protocol::VideoDeltaHeader delta;
                    memcpy(&delta, buf.data(), sizeof(delta));
                    int jpgSize = (int)hdr.size - (int)sizeof(delta);
                    RemoteWindow::UpdateFrame(delta, buf.data() + sizeof(delta), jpgSize);
                    break;
                }
                case Protocol::MSG_AUDIO_CHUNK: {
                    AudioPlayer::PlayChunk(buf.data(), (int)hdr.size);
                    break;
                }
                case Protocol::MSG_SYS_INFO_RESPONSE: {
                    if (hdr.size < sizeof(Protocol::SysInfoPayload)) break;
                    Protocol::SysInfoPayload sys;
                    memcpy(&sys, buf.data(), sizeof(sys));
                    TaskManagerWindow::UpdateSysInfo(sys);
                    break;
                }
                case Protocol::MSG_PROCESS_LIST_RESPONSE: {
                    if (hdr.size < sizeof(Protocol::ProcessInfoPayload)) break;
                    int count = (int)(hdr.size / sizeof(Protocol::ProcessInfoPayload));
                    const Protocol::ProcessInfoPayload* pList = (const Protocol::ProcessInfoPayload*)buf.data();
                    TaskManagerWindow::UpdateProcessList(pList, count);
                    break;
                }
                case Protocol::MSG_DRIVE_LIST_RESPONSE: {
                    std::string content(buf.begin(), buf.end());
                    FileExplorerWindow::OnDriveListResponse(content);
                    break;
                }
                case Protocol::MSG_DIR_RESPONSE: {
                    std::string content(buf.begin(), buf.end());
                    FileExplorerWindow::OnDirResponse(content);
                    break;
                }
                case Protocol::MSG_FILE_INFO: {
                    if (hdr.size < sizeof(Protocol::FileInfoPayload)) break;
                    Protocol::FileInfoPayload info;
                    memcpy(&info, buf.data(), sizeof(info));
                    info.fileName[sizeof(info.fileName) - 1] = '\0'; // Đảm bảo chuỗi kết thúc an toàn
                    FileExplorerWindow::OnFileInfo(info);
                    break;
                }
                case Protocol::MSG_FILE_CHUNK: {
                    FileExplorerWindow::OnFileChunk(buf.data(), (int)hdr.size);
                    break;
                }
                default:
                    break;
            }
        }
        g_isRunning = false;
    }
}

namespace DataReceiver {
    void Start(TcpClient* client) {
        if (g_isRunning) return;
        g_isRunning = true;
        std::thread(ReceiveLoop, client).detach();
    }

    void Stop() {
        g_isRunning = false;
    }
}