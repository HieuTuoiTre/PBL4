#include "DataReceiver.h"
#include "../../Shared/Protocol.h"
#include "../../Shared/Crypto.h"
#include "../../Shared/ControlEnvelope.h"
#include "../../Shared/ClipboardManager.h"
#include "../UI/RemoteWindow.h"
#include "../UI/TaskManagerWindow.h"
#include "../UI/FileExplorerWindow.h"
#include "../UI/ChatWindow.h"
#include "../Media/AudioPlayer.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

namespace {
    std::atomic<bool> g_isRunning(false);
    std::thread g_controlThread;
    std::thread g_audioThread;

    void DispatchControlMessage(Protocol::MessageType type, const char* data, uint32_t size) {
        switch (type) {
            case Protocol::MSG_PONG:
                break;

            case Protocol::MSG_FILE_END: {
                if (data && size >= sizeof(Protocol::FileEndPayload)) {
                    Protocol::FileEndPayload endP;
                    memcpy(&endP, data, sizeof(endP));
                    FileExplorerWindow::OnFileEndPayload(endP);
                } else {
                    FileExplorerWindow::OnFileEnd();
                }
                break;
            }

            case Protocol::MSG_FILE_BATCH_START: {
                if (data && size >= sizeof(Protocol::FileBatchStartPayload)) {
                    Protocol::FileBatchStartPayload batch;
                    memcpy(&batch, data, sizeof(batch));
                    batch.rootName[sizeof(batch.rootName) - 1] = '\0';
                    FileExplorerWindow::OnFileBatchStart(batch);
                }
                break;
            }

            case Protocol::MSG_FILE_BATCH_END: {
                FileExplorerWindow::OnFileBatchEnd();
                break;
            }

            case Protocol::MSG_FILE_CANCEL: {
                FileExplorerWindow::OnFileCancel();
                break;
            }

            case Protocol::MSG_FILE_ERROR: {
                std::string errStr = (data && size > 0) ? std::string(data, size) : "Lỗi truyền tải tệp tin!";
                FileExplorerWindow::OnFileError(errStr);
                break;
            }

            case Protocol::MSG_SYS_INFO_RESPONSE: {
                if (data && size >= sizeof(Protocol::SysInfoPayload)) {
                    Protocol::SysInfoPayload sys;
                    memcpy(&sys, data, sizeof(sys));
                    TaskManagerWindow::UpdateSysInfo(sys);
                }
                break;
            }

            case Protocol::MSG_PROCESS_LIST_RESPONSE: {
                if (data && size >= sizeof(Protocol::ProcessInfoPayload)) {
                    int count = (int)(size / sizeof(Protocol::ProcessInfoPayload));
                    const Protocol::ProcessInfoPayload* pList = (const Protocol::ProcessInfoPayload*)data;
                    TaskManagerWindow::UpdateProcessList(pList, count);
                }
                break;
            }

            case Protocol::MSG_DRIVE_LIST_RESPONSE: {
                if (data && size > 0) {
                    std::string content(data, size);
                    FileExplorerWindow::OnDriveListResponse(content);
                }
                break;
            }

            case Protocol::MSG_DIR_RESPONSE: {
                if (data && size > 0) {
                    std::string content(data, size);
                    FileExplorerWindow::OnDirResponse(content);
                }
                break;
            }

            case Protocol::MSG_FILE_INFO: {
                if (data && size >= sizeof(Protocol::FileHeaderPayload)) {
                    Protocol::FileHeaderPayload header;
                    memcpy(&header, data, sizeof(header));
                    header.relativePath[sizeof(header.relativePath) - 1] = '\0';
                    FileExplorerWindow::OnFileHeader(header);
                } else if (data && size >= sizeof(Protocol::FileInfoPayload)) {
                    Protocol::FileInfoPayload info;
                    memcpy(&info, data, sizeof(info));
                    info.fileName[sizeof(info.fileName) - 1] = '\0';
                    FileExplorerWindow::OnFileInfo(info);
                }
                break;
            }

            case Protocol::MSG_FILE_CHUNK: {
                if (data && size > 0) {
                    FileExplorerWindow::OnFileChunk(data, (int)size);
                }
                break;
            }

            case Protocol::MSG_CHAT_MESSAGE: {
                if (data && size >= sizeof(Protocol::ChatMessagePayload)) {
                    Protocol::ChatMessagePayload chat;
                    memcpy(&chat, data, sizeof(chat));
                    chat.sender[sizeof(chat.sender) - 1] = '\0';
                    chat.message[sizeof(chat.message) - 1] = '\0';
                    ChatWindow::OnMessageReceived(chat);
                }
                break;
            }

            case Protocol::MSG_CLIPBOARD_TEXT: {
                if (data && size > 0 && size <= ClipboardManager::MAX_CLIPBOARD_TEXT_BYTES) {
                    std::string text(data, size);
                    ClipboardManager::WriteText(RemoteWindow::GetHwnd(), text);
                }
                break;
            }

            default:
                break;
        }
    }

    void ReceiveControlLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;
        std::vector<char> rawBuf;
        rawBuf.reserve(64 * 1024);
        std::vector<uint8_t> plainPayload;
        plainPayload.reserve(64 * 1024);

        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            if (hdr.size > ControlEnvelope::MAX_CONTROL_PAYLOAD) break;

            if (hdr.size > 0) {
                rawBuf.resize(hdr.size);
                if (!client->ReceiveExact(rawBuf.data(), (int)hdr.size)) break;
            } else {
                rawBuf.clear();
            }

            if (hdr.type == Protocol::MSG_ENCRYPTED_CONTROL) {
                const uint8_t* key = client->GetSessionKey();
                if (!key) break;

                Protocol::PacketHeader innerHdr;
                if (!ControlEnvelope::UnwrapPacket(key, (const uint8_t*)rawBuf.data(), hdr.size, innerHdr, plainPayload)) {
                    break;
                }

                const char* innerData = (innerHdr.size > 0) ? (const char*)plainPayload.data() : nullptr;
                DispatchControlMessage(innerHdr.type, innerData, innerHdr.size);
            } else {
                const char* rawData = (hdr.size > 0) ? rawBuf.data() : nullptr;
                DispatchControlMessage(hdr.type, rawData, hdr.size);
            }
        }
    }

    void ReceiveAudioLoop(TcpClient* client) {
        if (!client) return;
        Protocol::PacketHeader hdr;
        std::vector<char> buf;
        buf.reserve(8192);

        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            if (hdr.size == 0) continue;
            if (hdr.size > 1024 * 1024) break;

            buf.resize(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            if (hdr.type == Protocol::MSG_AUDIO_CHUNK) {
                AudioPlayer::PlayChunk(buf.data(), (int)hdr.size);
            }
        }
    }
}

namespace DataReceiver {
    void Start(TcpClient* controlClient, TcpClient* audioClient) {
        if (g_isRunning) return;
        g_isRunning = true;

        if (controlClient) {
            g_controlThread = std::thread(ReceiveControlLoop, controlClient);
        }
        if (audioClient) {
            g_audioThread = std::thread(ReceiveAudioLoop, audioClient);
        }
    }

    void Stop() {
        if (!g_isRunning) return;
        g_isRunning = false;

        if (g_controlThread.joinable()) g_controlThread.join();
        if (g_audioThread.joinable()) g_audioThread.join();
    }
}
