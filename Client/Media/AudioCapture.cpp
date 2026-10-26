#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "AudioCapture.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace {
    // GUID định danh cho chuẩn IEEE FLOAT trong WAVEFORMATEXTENSIBLE
    static const GUID GUID_SUBTYPE_IEEE_FLOAT = 
        { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

    // Kích thước chunk chuẩn: 40ms ở 48.000 Hz, Stereo 16-bit
    // 48000 frames/s * 0.040 s = 960 frames
    // 960 frames * 2 channels * 2 bytes = 3840 bytes = 1920 int16 samples
    const size_t TARGET_CHUNK_BYTES = 3840;

    struct AudioFormatConverter {
        int inChannels = 2;
        int inSampleRate = 48000;
        int inBitsPerSample = 32;
        bool inIsFloat = true;

        double resampleRatio = 1.0;
        double resamplePos = 0.0;
        float lastInL = 0.0f;
        float lastInR = 0.0f;

        void Configure(const WAVEFORMATEX* pwfx) {
            inChannels = pwfx->nChannels;
            inSampleRate = pwfx->nSamplesPerSec;
            inBitsPerSample = pwfx->wBitsPerSample;

            inIsFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
            if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->cbSize >= 22) {
                const WAVEFORMATEXTENSIBLE* pExt = (const WAVEFORMATEXTENSIBLE*)pwfx;
                if (pExt->SubFormat == GUID_SUBTYPE_IEEE_FLOAT) {
                    inIsFloat = true;
                }
            }

            resampleRatio = (double)inSampleRate / 48000.0;
            resamplePos = 0.0;
            lastInL = 0.0f;
            lastInR = 0.0f;
        }

        void Process(const uint8_t* inBytes, int numFrames, bool isSilent, std::vector<int16_t>& outPcm16) {
            if (numFrames <= 0) return;
            int bytesPerFrame = inChannels * (inBitsPerSample / 8);

            // 1. Chuyển đổi các frame đầu vào về dạng Stereo Float [-1.0f, 1.0f]
            std::vector<std::pair<float, float>> inStereo(numFrames);
            if (isSilent || !inBytes) {
                for (int i = 0; i < numFrames; i++) {
                    inStereo[i] = { 0.0f, 0.0f };
                }
            } else {
                for (int f = 0; f < numFrames; f++) {
                    const uint8_t* framePtr = inBytes + f * bytesPerFrame;
                    float chSamples[8] = { 0.0f };
                    int maxCh = (std::min)(inChannels, 8);

                    for (int ch = 0; ch < maxCh; ch++) {
                        const uint8_t* samplePtr = framePtr + ch * (inBitsPerSample / 8);
                        if (inIsFloat) {
                            chSamples[ch] = *(const float*)samplePtr;
                        } else if (inBitsPerSample == 16) {
                            chSamples[ch] = *(const int16_t*)samplePtr / 32768.0f;
                        } else if (inBitsPerSample == 24) {
                            int32_t val = (int32_t)(samplePtr[0] | (samplePtr[1] << 8) | ((int8_t)samplePtr[2] << 16));
                            chSamples[ch] = val / 8388608.0f;
                        } else if (inBitsPerSample == 32) {
                            chSamples[ch] = *(const int32_t*)samplePtr / 2147483648.0f;
                        }
                    }

                    float left = 0.0f, right = 0.0f;
                    if (inChannels == 1) {
                        left = chSamples[0];
                        right = chSamples[0];
                    } else if (inChannels == 2) {
                        left = chSamples[0];
                        right = chSamples[1];
                    } else {
                        // Downmix âm thanh vòm (5.1 / 7.1) chuẩn về Stereo
                        left = chSamples[0] + 0.707f * chSamples[2] + 0.5f * chSamples[4];
                        right = chSamples[1] + 0.707f * chSamples[2] + 0.5f * chSamples[5];
                        float maxVal = (std::max)(std::abs(left), std::abs(right));
                        if (maxVal > 1.0f) {
                            left /= maxVal;
                            right /= maxVal;
                        }
                    }
                    inStereo[f] = { left, right };
                }
            }

            // 2. Resample sang 48.000 Hz và chuyển sang int16
            if (inSampleRate == 48000) {
                outPcm16.reserve(outPcm16.size() + numFrames * 2);
                for (int f = 0; f < numFrames; f++) {
                    int16_t l = (int16_t)std::clamp(inStereo[f].first * 32767.0f, -32768.0f, 32767.0f);
                    int16_t r = (int16_t)std::clamp(inStereo[f].second * 32767.0f, -32768.0f, 32767.0f);
                    outPcm16.push_back(l);
                    outPcm16.push_back(r);
                }
                lastInL = inStereo.back().first;
                lastInR = inStereo.back().second;
            } else {
                while (resamplePos < (double)numFrames) {
                    int idx0 = (int)std::floor(resamplePos);
                    double frac = resamplePos - idx0;

                    float l0 = (idx0 == 0) ? lastInL : inStereo[idx0 - 1].first;
                    float r0 = (idx0 == 0) ? lastInR : inStereo[idx0 - 1].second;
                    float l1 = inStereo[idx0].first;
                    float r1 = inStereo[idx0].second;

                    float lInterp = (float)((1.0 - frac) * l0 + frac * l1);
                    float rInterp = (float)((1.0 - frac) * r0 + frac * r1);

                    int16_t l = (int16_t)std::clamp(lInterp * 32767.0f, -32768.0f, 32767.0f);
                    int16_t r = (int16_t)std::clamp(rInterp * 32767.0f, -32768.0f, 32767.0f);
                    outPcm16.push_back(l);
                    outPcm16.push_back(r);

                    resamplePos += resampleRatio;
                }
                resamplePos -= (double)numFrames;
                lastInL = inStereo.back().first;
                lastInR = inStereo.back().second;
            }
        }
    };
}

namespace AudioCapture {

    std::function<void(const char*, int)> callback;
    std::atomic<bool> isRecording(false);
    std::thread audioThread;

    void CaptureLoop() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);

        IMMDeviceEnumerator* pEnumerator = NULL;
        IMMDevice* pDevice = NULL;
        IAudioClient* pCaptureAudioClient = NULL;
        IAudioCaptureClient* pCaptureClient = NULL;
        IAudioClient* pSilentRenderClient = NULL;
        IAudioRenderClient* pSilentRenderService = NULL;
        WAVEFORMATEX* pwfx = NULL;

        // 1. Tìm thiết bị âm thanh đầu ra mặc định (Loa / Render Endpoint)
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator)) || !pEnumerator) {
            CoUninitialize();
            return;
        }

        if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) || !pDevice) {
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        // 2. Kích hoạt Audio Client để lấy Mix Format gốc của thiết bị
        if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pCaptureAudioClient)) || !pCaptureAudioClient) {
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        if (FAILED(pCaptureAudioClient->GetMixFormat(&pwfx)) || !pwfx) {
            pCaptureAudioClient->Release();
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        // 3. Khởi tạo Silent Render Stream để giữ cho xung nhịp phần cứng WASAPI Loopback luôn chạy liên tục
        if (SUCCEEDED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pSilentRenderClient)) && pSilentRenderClient) {
            if (SUCCEEDED(pSilentRenderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pwfx, NULL))) {
                if (SUCCEEDED(pSilentRenderClient->GetService(__uuidof(IAudioRenderClient), (void**)&pSilentRenderService)) && pSilentRenderService) {
                    UINT32 bufferFrameCount = 0;
                    pSilentRenderClient->GetBufferSize(&bufferFrameCount);
                    BYTE* pData = NULL;
                    if (SUCCEEDED(pSilentRenderService->GetBuffer(bufferFrameCount, &pData)) && pData) {
                        memset(pData, 0, bufferFrameCount * pwfx->nBlockAlign);
                        pSilentRenderService->ReleaseBuffer(bufferFrameCount, AUDCLNT_BUFFERFLAGS_SILENT);
                    }
                    pSilentRenderClient->Start();
                }
            }
        }

        // 4. Khởi tạo Stream LOOPBACK để thu âm lại đầu ra loa
        if (FAILED(pCaptureAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, pwfx, NULL))) {
            if (pSilentRenderClient) { pSilentRenderClient->Stop(); pSilentRenderClient->Release(); }
            if (pSilentRenderService) pSilentRenderService->Release();
            CoTaskMemFree(pwfx);
            pCaptureAudioClient->Release();
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        if (FAILED(pCaptureAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient)) || !pCaptureClient) {
            if (pSilentRenderClient) { pSilentRenderClient->Stop(); pSilentRenderClient->Release(); }
            if (pSilentRenderService) pSilentRenderService->Release();
            CoTaskMemFree(pwfx);
            pCaptureAudioClient->Release();
            pDevice->Release();
            pEnumerator->Release();
            CoUninitialize();
            return;
        }

        AudioFormatConverter converter;
        converter.Configure(pwfx);

        pCaptureAudioClient->Start();

        std::vector<int16_t> accumPcm;
        accumPcm.reserve(1920 * 4);

        while (isRecording) {
            UINT32 packetLength = 0;
            if (FAILED(pCaptureClient->GetNextPacketSize(&packetLength))) break;

            while (packetLength != 0 && isRecording) {
                BYTE* pData = nullptr;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;

                if (SUCCEEDED(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL))) {
                    bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    converter.Process(pData, (int)numFramesAvailable, isSilent, accumPcm);
                    pCaptureClient->ReleaseBuffer(numFramesAvailable);
                }

                const size_t TARGET_SAMPLES = TARGET_CHUNK_BYTES / sizeof(int16_t);
                while (accumPcm.size() >= TARGET_SAMPLES) {
                    if (callback) {
                        callback((const char*)accumPcm.data(), (int)TARGET_CHUNK_BYTES);
                    }
                    accumPcm.erase(accumPcm.begin(), accumPcm.begin() + TARGET_SAMPLES);
                }

                if (FAILED(pCaptureClient->GetNextPacketSize(&packetLength))) break;
            }

            Sleep(5);
        }

        // Dọn dẹp tài nguyên
        pCaptureAudioClient->Stop();
        if (pSilentRenderClient) {
            pSilentRenderClient->Stop();
            pSilentRenderClient->Release();
        }
        if (pSilentRenderService) pSilentRenderService->Release();
        CoTaskMemFree(pwfx);
        pCaptureClient->Release();
        pCaptureAudioClient->Release();
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