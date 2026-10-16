#include "AudioCapture.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>

// Cấu trúc GUID cần thiết cho COM API
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

    // 1. Tìm thiết bị âm thanh đầu ra mặc định (Loa) và KIỂM TRA NGAY TẠI ĐÂY
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator)) || !pEnumerator) {
        CoUninitialize();
        return;
    }

    if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) || !pDevice) {
        pEnumerator->Release();
        CoUninitialize();
        return;
    }

    // 2. Kích hoạt Audio Client
    if (FAILED(pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient)) || !pAudioClient) {
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return;
    }

    pAudioClient->GetMixFormat(&pwfx);
    pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, pwfx, NULL);
    pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
    pAudioClient->Start();

    while (isRecording) {
        UINT32 packetLength = 0;
        pCaptureClient->GetNextPacketSize(&packetLength);

        while (packetLength != 0 && isRecording) {
            BYTE* pData;
            UINT32 numFramesAvailable;
            DWORD flags;

            pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
            int bytesToRead = numFramesAvailable * pwfx->nBlockAlign;

            if (callback && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                callback((const char*)pData, bytesToRead);
            }

            pCaptureClient->ReleaseBuffer(numFramesAvailable);
            pCaptureClient->GetNextPacketSize(&packetLength);
        }
        Sleep(10);
    }

    // Dọn dẹp tài nguyên COM chuẩn xác ở cuối hàm
    pAudioClient->Stop();
    CoTaskMemFree(pwfx);
    if (pCaptureClient) pCaptureClient->Release();
    pAudioClient->Release();
    pDevice->Release();
    pEnumerator->Release();
    CoUninitialize();
}

    bool Start(std::function<void(const char*, int)> onAudioData) {
        if (isRecording) return false;
        
        callback = onAudioData;
        isRecording = true;
        
        // Chạy vòng lặp WASAPI trên một luồng hoàn toàn độc lập
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