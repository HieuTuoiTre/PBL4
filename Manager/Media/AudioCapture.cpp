#include "AudioCapture.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

namespace AudioCapture {
    std::function<void(const char*, int)> callback;
    std::atomic<bool> isRecording(false);
    std::thread audioThread;

    void CaptureLoop() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);

        IMMDeviceEnumerator* pEnumerator = NULL;
        IMMDevice* pDevice = NULL;
        IAudioClient* pAudioClient = NULL;
        IAudioCaptureClient* pCaptureClient = NULL;
        WAVEFORMATEX* pwfx = NULL;

        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator)) || !pEnumerator) {
            CoUninitialize();
            return;
        }
        if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) || !pDevice) {
            pEnumerator->Release();
            CoUninitialize();
            return;
        }
        if (FAILED(pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient)) || !pAudioClient) {
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }
        if (FAILED(pAudioClient->GetMixFormat(&pwfx)) || !pwfx) {
            pAudioClient->Release();
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        // Đặt buffer 100ms (1,000,000 hecto-nanoseconds) để giảm độ trễ
        if (FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, pwfx, NULL))) {
            CoTaskMemFree(pwfx);
            pAudioClient->Release();
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }
        if (FAILED(pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient)) || !pCaptureClient) {
            CoTaskMemFree(pwfx);
            pAudioClient->Release();
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        pAudioClient->Start();

        // Gom đủ khoảng ~40ms âm thanh mỗi lần gửi để tránh nghẽn waveOut
        const size_t TARGET_CHUNK_BYTES = (pwfx->nAvgBytesPerSec / 25);
        std::vector<char> accumBuffer;
        accumBuffer.reserve(TARGET_CHUNK_BYTES * 2);

        while (isRecording) {
            UINT32 packetLength = 0;
            if (FAILED(pCaptureClient->GetNextPacketSize(&packetLength))) break;

            while (packetLength != 0 && isRecording) {
                BYTE* pData = nullptr;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;

                if (SUCCEEDED(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL))) {
                    int bytesToRead = numFramesAvailable * pwfx->nBlockAlign;

                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData && bytesToRead > 0) {
                        accumBuffer.insert(accumBuffer.end(), (const char*)pData, (const char*)pData + bytesToRead);
                    } else if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) && !accumBuffer.empty()) {
                        if (callback) callback(accumBuffer.data(), (int)accumBuffer.size());
                        accumBuffer.clear();
                    }
                    pCaptureClient->ReleaseBuffer(numFramesAvailable);
                }

                if (accumBuffer.size() >= TARGET_CHUNK_BYTES) {
                    if (callback) callback(accumBuffer.data(), (int)accumBuffer.size());
                    accumBuffer.clear();
                }

                if (FAILED(pCaptureClient->GetNextPacketSize(&packetLength))) break;
            }
            Sleep(5);
        }

        pAudioClient->Stop();
        CoTaskMemFree(pwfx);
        pCaptureClient->Release();
        pAudioClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
    }

    bool Start(std::function<void(const char*, int)> onAudioData) {
        if (isRecording) return false;
        callback = onAudioData;
        isRecording = true;
        audioThread = std::thread(CaptureLoop);
        return true;
    }

    void Stop() {
        if (!isRecording) return;
        isRecording = false;
        if (audioThread.joinable()) {
            audioThread.join();
        }
    }
}