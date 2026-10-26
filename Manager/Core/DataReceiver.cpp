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
    std::thread g_controlThread;
    std::thread g_videoThread;
    std::thread g_audioThread;

    // ========================================================
    // LUỒNG NHẬN CONTROL (8080): TASK MANAGER, FILE EXPLORER, SYSTEM INFO
    // ========================================================
    void ReceiveControlLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            if (hdr.type == Protocol::MSG_FILE_END) {
                FileExplorerWindow::OnFileEnd();
                continue;
            }

            if (hdr.size == 0 || hdr.size > 32 * 1024 * 1024) continue;
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            switch (hdr.type) {
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
                    info.fileName[sizeof(info.fileName) - 1] = '\0';
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
    }

    // ========================================================
    // LUỒNG NHẬN VIDEO (8081): KHUNG HÌNH MÀN HÌNH (JPEG)
    // ========================================================
    void ReceiveVideoLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            if (hdr.size == 0 || hdr.size > 32 * 1024 * 1024) continue;
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            if (hdr.type == Protocol::MSG_VIDEO_FRAME) {
                if (hdr.size > sizeof(Protocol::VideoDeltaHeader)) {
                    Protocol::VideoDeltaHeader delta;
                    memcpy(&delta, buf.data(), sizeof(delta));
                    int jpgSize = (int)hdr.size - (int)sizeof(delta);
                    RemoteWindow::UpdateFrame(delta, buf.data() + sizeof(delta), jpgSize);
                }
            }
        }
    }

    // ========================================================
    // LUỒNG NHẬN AUDIO (8082): ÂM THANH TRỰC TIẾP (PCM16 48KHZ STEREO)
    // ========================================================
    void ReceiveAudioLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            if (hdr.size == 0 || hdr.size > 1024 * 1024) continue;
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            if (hdr.type == Protocol::MSG_AUDIO_CHUNK) {
                AudioPlayer::PlayChunk(buf.data(), (int)hdr.size);
            }
        }
    }
}

namespace DataReceiver {
    void Start(TcpClient* controlClient, TcpClient* videoClient, TcpClient* audioClient) {
        if (g_isRunning) return;
        g_isRunning = true;
        g_controlThread = std::thread(ReceiveControlLoop, controlClient);
        g_videoThread = std::thread(ReceiveVideoLoop, videoClient);
        g_audioThread = std::thread(ReceiveAudioLoop, audioClient);
    }

    void Stop() {
        if (!g_isRunning) return;
        g_isRunning = false;
        if (g_controlThread.joinable()) g_controlThread.join();
        if (g_videoThread.joinable()) g_videoThread.join();
        if (g_audioThread.joinable()) g_audioThread.join();
    }
}