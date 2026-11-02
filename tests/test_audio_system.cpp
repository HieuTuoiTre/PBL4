#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <string>
#include <windows.h>
#include <mmsystem.h>

// Include AudioPlayer implementation logic for testing
#define AUDIO_RING_SIZE 32
#define SAMPLES_PER_CHUNK 960 // 40ms ở 24.000 Hz, Mono 16-bit (960 samples = 1920 bytes)
#define PREBUFFER_CHUNKS 2

namespace AudioTest {
    // -------------------------------------------------------------
    // 1. REPRODUCED CONVERTER LOGIC FROM AudioCapture.cpp (24kHz Mono)
    // -------------------------------------------------------------
    struct AudioFormatConverter {
        int inChannels = 2;
        int inSampleRate = 48000;
        int inBitsPerSample = 32;
        bool inIsFloat = true;

        double resampleRatio = 2.0; // inSampleRate / 24000.0
        double resamplePos = 0.0;
        float lastInMono = 0.0f;

        void Configure(int channels, int sampleRate, int bitsPerSample, bool isFloat) {
            inChannels = channels;
            inSampleRate = sampleRate;
            inBitsPerSample = bitsPerSample;
            inIsFloat = isFloat;
            resampleRatio = (double)inSampleRate / 24000.0;
            resamplePos = 0.0;
            lastInMono = 0.0f;
        }

        void Process(const uint8_t* inBytes, int numFrames, bool isSilent, std::vector<int16_t>& outPcm16) {
            if (numFrames <= 0) return;
            int bytesPerFrame = inChannels * (inBitsPerSample / 8);

            std::vector<float> inMono(numFrames);
            if (isSilent || !inBytes) {
                for (int i = 0; i < numFrames; i++) {
                    inMono[i] = 0.0f;
                }
            } else {
                for (int f = 0; f < numFrames; f++) {
                    const uint8_t* framePtr = inBytes + f * bytesPerFrame;
                    float chSamples[8] = { 0.0f };
                    int maxCh = std::min(inChannels, 8);

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

                    float mono = 0.0f;
                    if (inChannels == 1) {
                        mono = chSamples[0];
                    } else if (inChannels == 2) {
                        mono = 0.5f * (chSamples[0] + chSamples[1]);
                    } else {
                        mono = 0.35f * (chSamples[0] + chSamples[1]) + 0.5f * chSamples[2] + 0.25f * (chSamples[4] + chSamples[5]);
                        if (std::abs(mono) > 1.0f) {
                            mono = std::copysign(1.0f, mono);
                        }
                    }
                    inMono[f] = mono;
                }
            }

            while (resamplePos < (double)numFrames) {
                int idx0 = (int)std::floor(resamplePos);
                double frac = resamplePos - idx0;

                float m0 = inMono[idx0];
                float m1 = (idx0 + 1 < numFrames) ? inMono[idx0 + 1] : inMono[idx0];

                float mInterp = (float)((1.0 - frac) * m0 + frac * m1);

                int16_t sample = (int16_t)std::clamp(mInterp * 32767.0f, -32768.0f, 32767.0f);
                outPcm16.push_back(sample);

                resamplePos += resampleRatio;
            }
            resamplePos -= (double)numFrames;
            lastInMono = inMono.back();
        }
    };

    // -------------------------------------------------------------
    // 2. REPRODUCED RING BUFFER QUEUE LOGIC FROM AudioPlayer.cpp
    // -------------------------------------------------------------
    struct SimulatedPlayer {
        std::vector<int16_t> stagingBuffer;
        bool prebuffering = true;
        int audioIdx = 0;
        int totalChunksWritten = 0;
        int totalResets = 0;
        bool isMuted = false;

        // Mock hardware buffers
        struct MockHdr {
            DWORD dwFlags = 0;
            bool isPrepared = false;
        } headers[AUDIO_RING_SIZE];
        std::vector<int16_t> buffers[AUDIO_RING_SIZE];

        void Reset() {
            stagingBuffer.clear();
            prebuffering = true;
            audioIdx = 0;
            totalChunksWritten = 0;
            totalResets = 0;
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                headers[i] = MockHdr();
                buffers[i].resize(SAMPLES_PER_CHUNK, 0);
            }
        }

        void PlayChunk(const char* rawData, int byteSize) {
            if (byteSize <= 0 || !rawData || isMuted) return;

            const int16_t* pcmSamples = (const int16_t*)rawData;
            int sampleCount = byteSize / (int)sizeof(int16_t);

            stagingBuffer.insert(stagingBuffer.end(), pcmSamples, pcmSamples + sampleCount);

            if (prebuffering) {
                if (stagingBuffer.size() < (size_t)(SAMPLES_PER_CHUNK * PREBUFFER_CHUNKS)) {
                    return; // Hold until prebuffer is ready
                }
                prebuffering = false;
            }

            while (stagingBuffer.size() >= SAMPLES_PER_CHUNK) {
                int targetSlot = -1;
                for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                    int idx = (audioIdx + i) % AUDIO_RING_SIZE;
                    MockHdr& h = headers[idx];
                    if (!h.isPrepared || (h.dwFlags & WHDR_DONE)) {
                        targetSlot = idx;
                        break;
                    }
                }

                if (targetSlot == -1) {
                    targetSlot = audioIdx; // overwrite oldest
                }

                MockHdr& hdr = headers[targetSlot];
                hdr.isPrepared = true;
                hdr.dwFlags = 0;

                buffers[targetSlot].assign(stagingBuffer.begin(), stagingBuffer.begin() + SAMPLES_PER_CHUNK);
                stagingBuffer.erase(stagingBuffer.begin(), stagingBuffer.begin() + SAMPLES_PER_CHUNK);

                totalChunksWritten++;
                audioIdx = (targetSlot + 1) % AUDIO_RING_SIZE;
            }
        }
    };
}

int main() {
    int passed = 0;
    int failed = 0;

    auto TEST = [&](const std::string& name, bool condition) {
        if (condition) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    std::cout << "========================================================\n";
    std::cout << "     AUDIO SYSTEM AUTOMATED TEST SUITE (24kHz Mono)     \n";
    std::cout << "========================================================\n\n";

    // TEST 1: Standard 48kHz Stereo Float -> 24kHz 16-bit Mono PCM
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 32, true);
        // 40ms of 48kHz is 1920 frames -> downsampled to 24kHz Mono produces exactly 960 int16 samples
        std::vector<float> input(1920 * 2, 0.5f);
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 1920, false, output);

        TEST("Test 1: 48kHz Stereo -> 24kHz Mono conversion size check (40ms = 960 samples)", output.size() == 960);
        bool valCorrect = (output[0] >= 16383 && output[0] <= 16384);
        TEST("Test 1b: 24kHz Mono Float amplitude quantization check", valCorrect);
    }

    // TEST 2: Resampling from 44.1kHz to 24kHz (Common Laptop Audio)
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 44100, 32, true);
        // 441 frames at 44.1kHz is exactly 10ms -> should produce ~240 samples
        std::vector<float> input(441 * 2, 0.25f);
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 441, false, output);

        TEST("Test 2: 44.1kHz to 24kHz Resampling (10ms = ~240 samples)", output.size() >= 239 && output.size() <= 241);
    }

    // TEST 3: Downmixing 5.1 Surround (6 channels) to Mono
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(6, 48000, 32, true);
        // 1920 frames (40ms) at 48kHz -> should produce 960 samples at 24kHz
        std::vector<float> input(1920 * 6, 0.0f);
        for (int i = 0; i < 1920; i++) {
            input[i * 6 + 0] = 0.8f; // FL
            input[i * 6 + 1] = 0.0f; // FR
            input[i * 6 + 2] = 0.4f; // Center
        }
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 1920, false, output);

        TEST("Test 3: 5.1 Surround downmix output size is mono 960 samples", output.size() == 960);
        TEST("Test 3b: Mono downmix includes FrontLeft + Center contribution (>0)", output[0] > 0);
    }

    // TEST 4: Silence Handling (AUDCLNT_BUFFERFLAGS_SILENT)
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 32, true);
        std::vector<int16_t> output;
        conv.Process(nullptr, 1920, true, output);

        TEST("Test 4: Silence produces exact frames (1920 frames 48kHz = 960 samples 24kHz)", output.size() == 960);
        bool allZero = true;
        for (int16_t s : output) {
            if (s != 0) { allZero = false; break; }
        }
        TEST("Test 4b: Silence output contains only 0s (no uninitialized garbage)", allZero);
    }

    // TEST 5: 16-bit Integer PCM to 16-bit Mono PCM
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 16, false); // 16-bit int
        std::vector<int16_t> input(1920 * 2, 10000);
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 1920, false, output);

        TEST("Test 5: 16-bit PCM pass-through size (960 samples)", output.size() == 960);
        TEST("Test 5b: 16-bit PCM preserves value (~10000)", std::abs(output[0] - 10000) < 5);
    }

    // TEST 6: AudioPlayer Jitter Buffer & Burst Stress Test (24kHz Mono: 960 samples/chunk)
    {
        AudioTest::SimulatedPlayer player;
        player.Reset();

        // Feed 1 chunk (40ms = 960 samples) -> Should be held in pre-buffering (PREBUFFER_CHUNKS = 2)
        std::vector<int16_t> chunk(960, 1000);
        player.PlayChunk((const char*)chunk.data(), (int)chunk.size() * sizeof(int16_t));
        TEST("Test 6a: Jitter buffer holds 1st chunk during pre-buffering", player.totalChunksWritten == 0);

        // Feed 2nd chunk -> Pre-buffering threshold reached, both chunks written to hardware!
        player.PlayChunk((const char*)chunk.data(), (int)chunk.size() * sizeof(int16_t));
        TEST("Test 6b: Pre-buffering triggers release of 2 chunks", player.totalChunksWritten == 2);

        // Feed a burst of 15 chunks (simulating network congestion arrival)
        for (int i = 0; i < 15; i++) {
            player.PlayChunk((const char*)chunk.data(), (int)chunk.size() * sizeof(int16_t));
        }
        TEST("Test 6c: Burst of 15 chunks queued smoothly without reset (17 chunks total)", player.totalChunksWritten == 17);
    }

    // TEST 7: Ring Buffer Overflow Safety
    {
        AudioTest::SimulatedPlayer player;
        player.Reset();
        std::vector<int16_t> chunk(960, 500);

        // Write 40 chunks while marking none as done (simulating severe driver lag)
        for (int i = 0; i < 40; i++) {
            player.PlayChunk((const char*)chunk.data(), (int)chunk.size() * sizeof(int16_t));
        }
        TEST("Test 7: Overwriting oldest without crashing or deadlocking", player.totalChunksWritten == 40);
    }

    // TEST 8: Signal Continuity & Harmonic Cleanliness
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 32, true);

        // Generate a continuous 440 Hz Sine wave split across 5 separate packets (40ms each = 1920 frames each)
        const double freq = 440.0;
        const double phaseInc = 2.0 * 3.14159265358979323846 * freq / 48000.0;
        double phase = 0.0;

        std::vector<int16_t> totalPcm;
        for (int pkt = 0; pkt < 5; pkt++) {
            std::vector<float> pktIn(1920 * 2);
            for (int i = 0; i < 1920; i++) {
                float val = (float)std::sin(phase);
                pktIn[i * 2] = val;
                pktIn[i * 2 + 1] = val;
                phase += phaseInc;
                if (phase > 2.0 * 3.14159265358979323846) phase -= 2.0 * 3.14159265358979323846;
            }
            conv.Process((const uint8_t*)pktIn.data(), 1920, false, totalPcm);
        }

        // Check continuity across boundary between packet 0 and packet 1 (at sample index 959 to 960)
        int16_t sample959 = totalPcm[959];
        int16_t sample960 = totalPcm[960];
        int diff = std::abs((int)sample960 - (int)sample959);

        // For 440Hz at 24kHz, max delta between adjacent samples is ~3780
        TEST("Test 8: Phase continuity across chunk boundaries (no click/step)", diff < 4000);
    }

    std::cout << "\n========================================================\n";
    std::cout << "TEST RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (failed == 0) ? 0 : 1;
}
