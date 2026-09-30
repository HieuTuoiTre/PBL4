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
        // Khởi tạo môi trường COM cho Thread hiện tại
        CoInitializeEx(NULL, COINIT_MULTITHREADED);

        IMMDeviceEnumerator* pEnumerator = NULL;
        IMMDevice* pDevice = NULL;
        IAudioClient* pAudioClient = NULL;
        IAudioCaptureClient* pCaptureClient = NULL;
        WAVEFORMATEX* pwfx = NULL;

        // 1. Tìm thiết bị âm thanh đầu ra mặc định (Loa)
        CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
        pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

        // 2. Kích hoạt Audio Client
        pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
        pAudioClient->GetMixFormat(&pwfx);

        // 3. Khởi tạo Stream với cờ LOOPBACK (Thu lại những gì Loa đang phát)
        // Yêu cầu bộ đệm 1 giây (10,000,000 hecto-nanoseconds)
        pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, pwfx, NULL);

        // 4. Lấy Client để đọc dữ liệu
        pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
        pAudioClient->Start();

        while (isRecording) {
            UINT32 packetLength = 0;
            pCaptureClient->GetNextPacketSize(&packetLength);

            // Đọc liên tục chừng nào buffer còn dữ liệu
            while (packetLength != 0 && isRecording) {
                BYTE* pData;
                UINT32 numFramesAvailable;
                DWORD flags;

                pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);

                // Kích thước mảng byte = Số khung hình * kích thước 1 khung hình
                int bytesToRead = numFramesAvailable * pwfx->nBlockAlign;

                // Nếu có âm thanh (không phải cờ im lặng) và có callback, gửi qua mạng
                if (callback && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    callback((const char*)pData, bytesToRead);
                }

                pCaptureClient->ReleaseBuffer(numFramesAvailable);
                pCaptureClient->GetNextPacketSize(&packetLength);
            }
            // Ngủ một chút để tránh ngốn CPU (Chờ buffer Windows đầy lên)
            Sleep(10);
        }

        // Dọn dẹp tài nguyên COM
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