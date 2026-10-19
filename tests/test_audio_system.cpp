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
#define SAMPLES_PER_CHUNK 1920
#define PREBUFFER_CHUNKS 2

namespace AudioTest {
    // -------------------------------------------------------------
    // 1. REPRODUCED CONVERTER LOGIC FROM AudioCapture.cpp
    // -------------------------------------------------------------
    struct AudioFormatConverter {
        int inChannels = 2;
        int inSampleRate = 48000;
        int inBitsPerSample = 32;
        bool inIsFloat = true;

        double resampleRatio = 1.0;
        double resamplePos = 0.0;
        float lastInL = 0.0f;
        float lastInR = 0.0f;

        void Configure(int channels, int sampleRate, int bitsPerSample, bool isFloat) {
            inChannels = channels;
            inSampleRate = sampleRate;
            inBitsPerSample = bitsPerSample;
            inIsFloat = isFloat;
            resampleRatio = (double)inSampleRate / 48000.0;
            resamplePos = 0.0;
            lastInL = 0.0f;
            lastInR = 0.0f;
        }

        void Process(const uint8_t* inBytes, int numFrames, bool isSilent, std::vector<int16_t>& outPcm16) {
            if (numFrames <= 0) return;
            int bytesPerFrame = inChannels * (inBitsPerSample / 8);

            std::vector<std::pair<float, float>> inStereo(numFrames);
            if (isSilent || !inBytes) {
                for (int i = 0; i < numFrames; i++) {
                    inStereo[i] = { 0.0f, 0.0f };
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

                    float left = 0.0f, right = 0.0f;
                    if (inChannels == 1) {
                        left = chSamples[0];
                        right = chSamples[0];
                    } else if (inChannels == 2) {
                        left = chSamples[0];
                        right = chSamples[1];
                    } else {
                        left = chSamples[0] + 0.707f * chSamples[2] + 0.5f * chSamples[4];
                        right = chSamples[1] + 0.707f * chSamples[2] + 0.5f * chSamples[5];
                        float maxVal = std::max(std::abs(left), std::abs(right));
                        if (maxVal > 1.0f) {
                            left /= maxVal;
                            right /= maxVal;
                        }
                    }
                    inStereo[f] = { left, right };
                }
            }

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
            bool prepared = false;
            bool done = false;
        };
        MockHdr headers[AUDIO_RING_SIZE];

        void Reset() {
            stagingBuffer.clear();
            prebuffering = true;
            audioIdx = 0;
            totalChunksWritten = 0;
            totalResets = 0;
            isMuted = false;
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                headers[i] = { false, false };
            }
        }

        void PlayChunk(const char* rawData, int byteSize) {
            if (byteSize <= 0 || !rawData || isMuted) return;

            const int16_t* pcmSamples = (const int16_t*)rawData;
            int sampleCount = byteSize / (int)sizeof(int16_t);
            stagingBuffer.insert(stagingBuffer.end(), pcmSamples, pcmSamples + sampleCount);

            if (prebuffering) {
                if (stagingBuffer.size() < (size_t)(SAMPLES_PER_CHUNK * PREBUFFER_CHUNKS)) {
                    return;
                }
                prebuffering = false;
            }

            while (stagingBuffer.size() >= SAMPLES_PER_CHUNK) {
                int targetSlot = -1;
                for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                    int idx = (audioIdx + i) % AUDIO_RING_SIZE;
                    MockHdr& h = headers[idx];
                    if (!h.prepared || h.done) {
                        targetSlot = idx;
                        break;
                    }
                }

                if (targetSlot == -1) {
                    targetSlot = audioIdx; // Graceful overwrite
                }

                headers[targetSlot].prepared = true;
                headers[targetSlot].done = false;
                totalChunksWritten++;

                stagingBuffer.erase(stagingBuffer.begin(), stagingBuffer.begin() + SAMPLES_PER_CHUNK);
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
    std::cout << "         AUDIO SYSTEM AUTOMATED TEST SUITE              \n";
    std::cout << "========================================================\n\n";

    // TEST 1: Standard 48kHz Stereo Float -> 48kHz 16-bit PCM
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 32, true);
        std::vector<float> input(960 * 2, 0.5f); // 40ms of 0.5f constant
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 960, false, output);

        TEST("Test 1: 48kHz Float Stereo conversion size check", output.size() == 1920);
        bool valCorrect = (output[0] >= 16383 && output[0] <= 16384);
        TEST("Test 1b: 48kHz Float Stereo amplitude quantization check", valCorrect);
    }

    // TEST 2: Resampling from 44.1kHz to 48kHz (Common Laptop Audio)
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 44100, 32, true);
        // 441 frames at 44.1kHz is exactly 10ms -> should produce 480 frames (960 int16)
        std::vector<float> input(441 * 2, 0.25f);
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 441, false, output);

        int outFrames = (int)output.size() / 2;
        TEST("Test 2: 44.1kHz to 48kHz Resampling (10ms = ~480 frames)", outFrames >= 479 && outFrames <= 481);
    }

    // TEST 3: Downmixing 5.1 Surround (6 channels) to Stereo
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(6, 48000, 32, true);
        // FL = 0.8, FR = 0.0, Center = 0.4
        std::vector<float> input(960 * 6, 0.0f);
        for (int i = 0; i < 960; i++) {
            input[i * 6 + 0] = 0.8f;
            input[i * 6 + 1] = 0.0f;
            input[i * 6 + 2] = 0.4f;
        }
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 960, false, output);

        TEST("Test 3: 5.1 Surround downmix output size is stereo 1920 samples", output.size() == 1920);
        TEST("Test 3b: Left channel includes FrontLeft + Center", output[0] > output[1]);
        TEST("Test 3c: Right channel has Center contribution (>0)", output[1] > 0);
    }

    // TEST 4: Silence Handling (AUDCLNT_BUFFERFLAGS_SILENT)
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 32, true);
        std::vector<int16_t> output;
        conv.Process(nullptr, 960, true, output);

        TEST("Test 4: Silence produces exact frames (960 frames = 1920 samples)", output.size() == 1920);
        bool allZero = true;
        for (int16_t s : output) {
            if (s != 0) { allZero = false; break; }
        }
        TEST("Test 4b: Silence output contains only 0s (no uninitialized garbage)", allZero);
    }

    // TEST 5: 16-bit Integer PCM to 16-bit Stereo PCM
    {
        AudioTest::AudioFormatConverter conv;
        conv.Configure(2, 48000, 16, false); // 16-bit int
        std::vector<int16_t> input(960 * 2, 10000);
        std::vector<int16_t> output;
        conv.Process((const uint8_t*)input.data(), 960, false, output);

        TEST("Test 5: 16-bit PCM pass-through size", output.size() == 1920);
        TEST("Test 5b: 16-bit PCM preserves value (~10000)", std::abs(output[0] - 10000) < 5);
    }

    // TEST 6: AudioPlayer Jitter Buffer & Burst Stress Test
    {
        AudioTest::SimulatedPlayer player;
        player.Reset();

        // Feed 1 chunk (40ms) -> Should be held in pre-buffering (PREBUFFER_CHUNKS = 2)
        std::vector<int16_t> chunk(1920, 1000);
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
        std::vector<int16_t> chunk(1920, 500);

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

        // Generate a continuous 440 Hz Sine wave split across 5 separate packets
        const double freq = 440.0;
        const double phaseInc = 2.0 * 3.14159265358979323846 * freq / 48000.0;
        double phase = 0.0;

        std::vector<int16_t> totalPcm;
        for (int pkt = 0; pkt < 5; pkt++) {
            std::vector<float> pktIn(960 * 2);
            for (int i = 0; i < 960; i++) {
                float val = (float)std::sin(phase);
                pktIn[i * 2] = val;
                pktIn[i * 2 + 1] = val;
                phase += phaseInc;
                if (phase > 2.0 * 3.14159265358979323846) phase -= 2.0 * 3.14159265358979323846;
            }
            conv.Process((const uint8_t*)pktIn.data(), 960, false, totalPcm);
        }

        // Check continuity across boundary between packet 0 and packet 1 (at frame index 959 to 960)
        int16_t sample959 = totalPcm[959 * 2];
        int16_t sample960 = totalPcm[960 * 2];
        int diff = std::abs((int)sample960 - (int)sample959);

        // For 440Hz at 48kHz, max delta between adjacent samples is ~1890
        TEST("Test 8: Phase continuity across chunk boundaries (no click/step)", diff < 2000);
    }

    std::cout << "\n========================================================\n";
    std::cout << "TEST RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (failed == 0) ? 0 : 1;
}

