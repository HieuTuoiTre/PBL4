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

            if (hdr.size <= 0) continue;
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), hdr.size)) break;

            switch (hdr.type) {
                case Protocol::MSG_VIDEO_FRAME: {
                    Protocol::VideoDeltaHeader delta;
                    memcpy(&delta, buf.data(), sizeof(delta));
                    int jpgSize = hdr.size - sizeof(delta);
                    RemoteWindow::UpdateFrame(delta, buf.data() + sizeof(delta), jpgSize);
                    break;
                }
                case Protocol::MSG_AUDIO_CHUNK: {
                    AudioPlayer::PlayChunk(buf.data(), hdr.size);
                    break;
                }
                case Protocol::MSG_SYS_INFO_RESPONSE: {
                    Protocol::SysInfoPayload sys;
                    memcpy(&sys, buf.data(), sizeof(sys));
                    TaskManagerWindow::UpdateSysInfo(sys);
                    break;
                }
                case Protocol::MSG_PROCESS_LIST_RESPONSE: {
                    int count = hdr.size / sizeof(Protocol::ProcessInfoPayload);
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
                    Protocol::FileInfoPayload info;
                    memcpy(&info, buf.data(), sizeof(info));
                    FileExplorerWindow::OnFileInfo(info);
                    break;
                }
                case Protocol::MSG_FILE_CHUNK: {
                    FileExplorerWindow::OnFileChunk(buf.data(), hdr.size);
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