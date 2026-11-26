#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>

#include "../Shared/CRC32.h"
#include "../Shared/Protocol.h"
#include "../Shared/PathSanitizer.h"
#include "../Manager/FileTransfer/FileManager.h"
#include "../Client/FileTransfer/FileWorker.h"

namespace {
    int g_totalTests = 0;
    int g_passedTests = 0;
    int g_failedTests = 0;

    void PrintBanner(const std::string& title) {
        std::cout << "\n======================================================================\n";
        std::cout << "  " << title << "\n";
        std::cout << "======================================================================\n";
    }

    void ReportResult(const std::string& testName, bool passed, const std::string& detail) {
        g_totalTests++;
        if (passed) {
            g_passedTests++;
            std::cout << "  [PASS] " << std::left << std::setw(44) << testName 
                      << " | " << detail << "\n";
        } else {
            g_failedTests++;
            std::cout << "  [FAIL] " << std::left << std::setw(44) << testName 
                      << " | " << detail << "\n";
        }
    }
}

// ======================================================================
// GROUP 1: CRC32 Standard Test Vectors (IEEE 802.3 Polynomial 0xEDB88320)
// ======================================================================
void TestCrc32StandardVectors() {
    PrintBanner("GROUP 1: CRC32 Standard Test Vectors (IEEE 802.3)");

    // 1.1 Empty buffer
    {
        uint32_t crc = CRC32::Calculate("", 0);
        ReportResult("1.1 Empty Buffer Vector", crc == 0x00000000U,
                     "Expected: 0x00000000, Got: 0x" + std::to_string(crc));
    }

    // 1.2 Classic IEEE 802.3 standard vector "123456789" -> 0xCBF43926
    {
        const char* input = "123456789";
        uint32_t crc = CRC32::Calculate(input, strlen(input));
        char buf[32];
        sprintf_s(buf, "0x%08X", crc);
        ReportResult("1.2 Standard '123456789' Vector", crc == 0xCBF43926U,
                     "Expected: 0xCBF43926, Got: " + std::string(buf));
    }

    // 1.3 Pangram: "The quick brown fox jumps over the lazy dog" -> 0x414FA339
    {
        const char* input = "The quick brown fox jumps over the lazy dog";
        uint32_t crc = CRC32::Calculate(input, strlen(input));
        char buf[32];
        sprintf_s(buf, "0x%08X", crc);
        ReportResult("1.3 Quick Brown Fox Vector", crc == 0x414FA339U,
                     "Expected: 0x414FA339, Got: " + std::string(buf));
    }

    // 1.4 Single character "a" -> 0xE8B7BE43
    {
        const char* input = "a";
        uint32_t crc = CRC32::Calculate(input, 1);
        char buf[32];
        sprintf_s(buf, "0x%08X", crc);
        ReportResult("1.4 Single Character 'a' Vector", crc == 0xE8B7BE43U,
                     "Expected: 0xE8B7BE43, Got: " + std::string(buf));
    }

    // 1.5 Binary buffer of 256 bytes (values 0..255)
    {
        std::vector<uint8_t> binaryData(256);
        for (int i = 0; i < 256; ++i) binaryData[i] = static_cast<uint8_t>(i);
        uint32_t crc = CRC32::Calculate(binaryData.data(), binaryData.size());
        char buf[32];
        sprintf_s(buf, "0x%08X", crc);
        ReportResult("1.5 Binary 0..255 Sequence Vector", crc == 0x29058C73U,
                     "Expected: 0x29058C73, Got: " + std::string(buf));
    }
}

// ======================================================================
// GROUP 2: CRC32 Incremental vs One-Shot Streaming Consistency
// ======================================================================
void TestCrc32IncrementalStreaming() {
    PrintBanner("GROUP 2: CRC32 Incremental vs One-Shot Streaming Consistency");

    // 2.1 Byte-by-byte incremental update matching one-shot
    {
        const char* input = "123456789";
        uint32_t runningCrc = 0;
        for (size_t i = 0; i < strlen(input); ++i) {
            runningCrc = CRC32::Update(runningCrc, &input[i], 1);
        }
        uint32_t oneShot = CRC32::Calculate(input, strlen(input));
        ReportResult("2.1 Byte-by-Byte Incremental vs One-Shot", runningCrc == oneShot && runningCrc == 0xCBF43926U,
                     "Running: 0x" + std::to_string(runningCrc) + " == OneShot: 0x" + std::to_string(oneShot));
    }

    // 2.2 Arbitrary chunk sizes (1B, 2B, 3B, 3B) on "123456789"
    {
        const char* input = "123456789";
        uint32_t runningCrc = 0;
        runningCrc = CRC32::Update(runningCrc, input, 1);       // "1"
        runningCrc = CRC32::Update(runningCrc, input + 1, 2);   // "23"
        runningCrc = CRC32::Update(runningCrc, input + 3, 3);   // "456"
        runningCrc = CRC32::Update(runningCrc, input + 6, 3);   // "789"
        ReportResult("2.2 Variable Chunk Slices Incremental", runningCrc == 0xCBF43926U,
                     "Got: 0xCBF43926");
    }

    // 2.3 Large 128KB buffer split across 64KB chunks
    {
        std::vector<char> largeData(128 * 1024);
        for (size_t i = 0; i < largeData.size(); ++i) {
            largeData[i] = static_cast<char>((i * 73 + 19) & 0xFF);
        }
        uint32_t oneShot = CRC32::Calculate(largeData.data(), largeData.size());

        uint32_t streamedCrc = 0;
        size_t chunkSize = 64 * 1024;
        for (size_t offset = 0; offset < largeData.size(); offset += chunkSize) {
            size_t len = (std::min)(chunkSize, largeData.size() - offset);
            streamedCrc = CRC32::Update(streamedCrc, largeData.data() + offset, len);
        }
        ReportResult("2.3 Large 128KB Streamed via 64KB Chunks", streamedCrc == oneShot,
                     "OneShot CRC == Streamed CRC (131072 bytes verified)");
    }

    // 2.4 Null and zero length edge cases
    {
        uint32_t initial = 0x12345678U;
        uint32_t res1 = CRC32::Update(initial, nullptr, 0);
        uint32_t res2 = CRC32::Update(initial, "data", 0);
        uint32_t res3 = CRC32::Update(initial, nullptr, 100);
        ReportResult("2.4 Null/Zero-Length Robustness", res1 == initial && res2 == initial && res3 == initial,
                     "Unchanged CRC on invalid input pointers/lengths");
    }
}

// ======================================================================
// GROUP 3: 64KB Chunking & Boundary Slicing Calculations
// ======================================================================
void TestChunkingCalculations() {
    PrintBanner("GROUP 3: 64KB Chunking & Boundary Slicing Calculations");

    const uint32_t CHUNK = Protocol::FILE_CHUNK_SIZE; // 65536

    auto CalcChunks = [CHUNK](uint64_t fileSize) -> uint32_t {
        if (fileSize == 0) return 0;
        return static_cast<uint32_t>((fileSize + CHUNK - 1) / CHUNK);
    };

    // 3.1 Boundary chunk count verification
    {
        bool p1 = (CalcChunks(0) == 0);
        bool p2 = (CalcChunks(1) == 1);
        bool p3 = (CalcChunks(CHUNK - 1) == 1);
        bool p4 = (CalcChunks(CHUNK) == 1);
        bool p5 = (CalcChunks(CHUNK + 1) == 2);
        bool p6 = (CalcChunks(CHUNK * 2) == 2);
        bool p7 = (CalcChunks(1024 * 1024) == 16);
        ReportResult("3.1 Chunk Count Calculation Boundaries", p1 && p2 && p3 && p4 && p5 && p6 && p7,
                     "0B->0, 1B->1, 64KB->1, 64KB+1->2, 1MB->16");
    }

    // 3.2 Slicing simulation on 150KB synthetic file (2 full chunks + 1 partial chunk)
    {
        uint64_t fileSize = 150 * 1024; // 153600 bytes
        uint32_t totalChunks = CalcChunks(fileSize);
        uint64_t accumulatedBytes = 0;
        bool validSlices = true;

        for (uint32_t i = 0; i < totalChunks; ++i) {
            uint64_t offset = i * CHUNK;
            uint32_t len = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(CHUNK), fileSize - offset));
            if (i == 0 && len != 65536) validSlices = false;
            if (i == 1 && len != 65536) validSlices = false;
            if (i == 2 && len != 22528) validSlices = false;
            accumulatedBytes += len;
        }

        ReportResult("3.2 150KB Slicing (64KB + 64KB + 22KB)", totalChunks == 3 && validSlices && accumulatedBytes == fileSize,
                     "Total chunks: " + std::to_string(totalChunks) + ", Sum: " + std::to_string(accumulatedBytes) + " bytes");
    }
}

// ======================================================================
// GROUP 4: Safe .part File Lifecycle & CRC32 Verification
// ======================================================================
void TestSafePartialFileLifecycle() {
    PrintBanner("GROUP 4: Safe .part File Lifecycle & CRC32 Verification");

    std::filesystem::path testDir = "tests_tmp_transfer";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    // 4.1 Successful file transfer with valid CRC32 match -> renamed to final
    {
        std::string finalPath = (testDir / "valid_file.bin").u8string();
        std::string partPath = finalPath + ".part";

        std::string payload = "Hello World PBL4 Remote Desktop Transfer! Verified with CRC32.";
        uint32_t expectedCrc = CRC32::Calculate(payload.data(), payload.size());

        FileManager mgr;
        bool opened = mgr.OpenForWrite(finalPath, payload.size(), expectedCrc);
        bool written = mgr.WriteChunk(payload.data(), (int)payload.size());
        bool crcValid = false;
        bool finalized = mgr.FinalizeFile(&crcValid);

        bool partExists = std::filesystem::exists(std::filesystem::u8path(partPath), ec);
        bool finalExists = std::filesystem::exists(std::filesystem::u8path(finalPath), ec);

        std::string readBack;
        {
            std::ifstream in(std::filesystem::u8path(finalPath), std::ios::binary);
            if (in.is_open()) {
                readBack.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
        }

        ReportResult("4.1 Valid CRC32 Transfer & Rename", opened && written && finalized && crcValid && !partExists && finalExists && readBack == payload,
                     "Part deleted, final exists, CRC32 verified, content exact");
    }

    // 4.2 Corrupt file transfer with CRC32 mismatch -> rejected and .part deleted
    {
        std::string finalPath = (testDir / "corrupt_file.bin").u8string();
        std::string partPath = finalPath + ".part";

        std::string payload = "Original genuine payload";
        uint32_t expectedCrc = 0x12345678U; // Deliberate mismatch

        FileManager mgr;
        bool opened = mgr.OpenForWrite(finalPath, payload.size(), expectedCrc);
        mgr.WriteChunk(payload.data(), (int)payload.size());
        bool crcValid = true;
        bool finalized = mgr.FinalizeFile(&crcValid);

        bool partExists = std::filesystem::exists(std::filesystem::u8path(partPath), ec);
        bool finalExists = std::filesystem::exists(std::filesystem::u8path(finalPath), ec);

        ReportResult("4.2 Corrupt CRC32 Rejection & .part Cleanup", opened && !finalized && !crcValid && !partExists && !finalExists,
                     "Finalize failed, crcValid=false, .part purged, final not created");
    }

    // 4.3 Cancelled file transfer midway -> .part purged immediately
    {
        std::string finalPath = (testDir / "cancelled_file.bin").u8string();
        std::string partPath = finalPath + ".part";

        std::string payloadChunk = "Some partial chunk received before user clicked Cancel";

        FileManager mgr;
        mgr.OpenForWrite(finalPath, 1000000, 0xAABBCCDD);
        mgr.WriteChunk(payloadChunk.data(), (int)payloadChunk.size());
        
        bool partExistedMidway = std::filesystem::exists(std::filesystem::u8path(partPath), ec);
        mgr.AbortAndCleanup();
        bool partExistsAfterCancel = std::filesystem::exists(std::filesystem::u8path(partPath), ec);
        bool finalExistsAfterCancel = std::filesystem::exists(std::filesystem::u8path(finalPath), ec);

        ReportResult("4.3 Cancellation Cleanup of .part File", partExistedMidway && !partExistsAfterCancel && !finalExistsAfterCancel,
                     "Active .part file immediately deleted upon AbortAndCleanup");
    }

    std::filesystem::remove_all(testDir, ec);
}

// ======================================================================
// GROUP 5: Recursive Directory Tree Traversal & Serialization
// ======================================================================
void TestDirectoryTreeTraversal() {
    PrintBanner("GROUP 5: Recursive Directory Tree Traversal & Serialization");

    std::filesystem::path srcRoot = "tests_tmp_tree_src";
    std::filesystem::path dstRoot = "tests_tmp_tree_dst";
    std::error_code ec;

    std::filesystem::remove_all(srcRoot, ec);
    std::filesystem::remove_all(dstRoot, ec);

    // Create nested mock directory tree
    std::filesystem::create_directories(srcRoot / "folderA" / "subfolderB", ec);
    std::filesystem::create_directories(srcRoot / "folderC", ec);

    auto WriteTestFile = [](const std::filesystem::path& p, const std::string& content) {
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), content.size());
    };

    WriteTestFile(srcRoot / "root_file.txt", "Root content 123");
    WriteTestFile(srcRoot / "folderA" / "fileA.txt", "Folder A content data");
    WriteTestFile(srcRoot / "folderA" / "subfolderB" / "fileB.bin", "Deeply nested file B binary payload");
    WriteTestFile(srcRoot / "folderC" / "fileC.log", "Log content in folder C");

    // 5.1 ScanDirectoryTree enumeration
    auto items = FileWorker::ScanDirectoryTree(srcRoot.u8string());
    
    int dirCount = 0;
    int fileCount = 0;
    uint64_t totalBytes = 0;
    for (const auto& it : items) {
        if (it.isDirectory) {
            dirCount++;
        } else {
            fileCount++;
            totalBytes += it.fileSize;
            assert(it.crc32 != 0); // Each regular file must have genuine non-zero CRC32
        }
    }

    ReportResult("5.1 Directory Tree Scan & Item Discovery", items.size() == 7 && dirCount == 3 && fileCount == 4,
                 "Found " + std::to_string(dirCount) + " dirs, " + std::to_string(fileCount) + " files (" + std::to_string(totalBytes) + " bytes)");

    // 5.2 Reconstruct tree at destination
    bool reconstructionOk = true;
    for (const auto& it : items) {
        std::filesystem::path target = dstRoot / std::filesystem::u8path(it.relativePath);
        if (it.isDirectory) {
            std::filesystem::create_directories(target, ec);
        } else {
            std::filesystem::create_directories(target.parent_path(), ec);
            std::ifstream in(std::filesystem::u8path(it.fullPath), std::ios::binary);
            std::ofstream out(target, std::ios::binary);
            out << in.rdbuf();
            out.close();

            uint32_t destCrc = FileWorker::CalculateFileCrc32(target.u8string());
            if (destCrc != it.crc32) {
                reconstructionOk = false;
            }
        }
    }

    ReportResult("5.2 Tree Reconstruction & Content Integrity", reconstructionOk,
                 "All nested relative paths created, 100% CRC32 fidelity across all files");

    std::filesystem::remove_all(srcRoot, ec);
    std::filesystem::remove_all(dstRoot, ec);
}

// ======================================================================
// GROUP 6: Atomic Cancellation Token Logic
// ======================================================================
void TestCancellationTokenLogic() {
    PrintBanner("GROUP 6: Atomic Cancellation Token Logic");

    std::atomic<bool> cancelToken(false);
    std::atomic<int> chunksProcessed(0);
    std::atomic<bool> workerCleanExit(false);

    // Simulate transfer worker thread
    std::thread worker([&cancelToken, &chunksProcessed, &workerCleanExit]() {
        const int totalChunks = 100;
        for (int i = 0; i < totalChunks; ++i) {
            if (cancelToken.load(std::memory_order_relaxed)) {
                workerCleanExit = true;
                return;
            }
            chunksProcessed++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        workerCleanExit = false;
    });

    // Wait until 5 chunks processed, then trigger cancel
    while (chunksProcessed.load() < 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cancelToken.store(true);
    worker.join();

    ReportResult("6.1 Atomic Cancellation Token Interruption", workerCleanExit && chunksProcessed.load() < 100,
                 "Worker halted at chunk " + std::to_string(chunksProcessed.load()) + " of 100 on cancel signal");
}

// ======================================================================
// GROUP 7: Path Traversal Prevention & Containment Verification (R-3)
// ======================================================================
void TestPathTraversalPrevention() {
    PrintBanner("GROUP 7: Path Traversal Prevention & Containment (R-3)");

    // 7.1 Rejection of simple parent traversal: ../outside.txt
    {
        bool safe = PathSanitizer::IsSafeRelativePath("../outside.txt");
        ReportResult("7.1 Rejection of '../outside.txt'", !safe,
                     "Traversing parent with forward slash rejected");
    }

    // 7.2 Rejection of Windows-style parent traversal: ..\outside.txt
    {
        bool safe = PathSanitizer::IsSafeRelativePath("..\\outside.txt");
        ReportResult("7.2 Rejection of '..\\outside.txt'", !safe,
                     "Traversing parent with backslash rejected");
    }

    // 7.3 Rejection of nested traversal: a/../../outside.txt
    {
        bool safe = PathSanitizer::IsSafeRelativePath("docs/../../outside.txt");
        ReportResult("7.3 Rejection of 'docs/../../outside.txt'", !safe,
                     "Nested parent traversal rejected");
    }

    // 7.4 Rejection of absolute paths (Windows drive and Unix root)
    {
        bool safeWin = PathSanitizer::IsSafeRelativePath("C:\\Windows\\System32\\cmd.exe");
        bool safeUnix = PathSanitizer::IsSafeRelativePath("/etc/passwd");
        ReportResult("7.4 Rejection of Absolute Paths", !safeWin && !safeUnix,
                     "C:\\... and /... absolute paths rejected");
    }

    // 7.5 Rejection of empty, dot or whitespace path
    {
        bool safeEmpty = PathSanitizer::IsSafeRelativePath("");
        bool safeDot = PathSanitizer::IsSafeRelativePath(".");
        ReportResult("7.5 Rejection of Empty/Dot Paths", !safeEmpty && !safeDot,
                     "Empty and '.' paths rejected");
    }

    // 7.6 Safe resolution and containment of valid nested paths
    {
        std::string outSafe;
        bool ok = PathSanitizer::ResolveSafePath("downloads", "folder/subfolder/file.txt", outSafe);
        bool contained = false;
        if (ok) {
            contained = PathSanitizer::IsPathContained("downloads", outSafe);
        }
        ReportResult("7.6 Safe Nested Path Containment", ok && contained,
                     "Valid nested path resolved safely inside base directory");
    }

    // 7.7 Safe resolution rejects attack vectors attempting to escape root
    {
        std::string outSafe;
        bool r1 = PathSanitizer::ResolveSafePath("downloads", "../escape.txt", outSafe);
        bool r2 = PathSanitizer::ResolveSafePath("downloads", "C:\\evil.dll", outSafe);
        bool r3 = PathSanitizer::ResolveSafePath("downloads", "sub/../../../outside.txt", outSafe);
        ReportResult("7.7 Attack Vector Rejection in ResolveSafePath", !r1 && !r2 && !r3,
                     "All traversal attack vectors rejected by ResolveSafePath");
    }

    // 7.8 SanitizeFileName strips path prefixes and dangerous characters
    {
        std::string s1 = PathSanitizer::SanitizeFileName("../../evil.exe");
        std::string s2 = PathSanitizer::SanitizeFileName("C:\\Windows\\cmd.exe");
        std::string s3 = PathSanitizer::SanitizeFileName("good_file.txt");
        ReportResult("7.8 SanitizeFileName Strips Directories",
                     s1 == "evil.exe" && s2 == "cmd.exe" && s3 == "good_file.txt",
                     "Filename sanitized to basename only");
    }
}

int main() {
    std::cout << "\n======================================================================\n";
    std::cout << "  LANREMOTE - FILE TRANSFER & INTEGRITY UNIT TEST SUITE (M3)\n";
    std::cout << "======================================================================\n";

    TestCrc32StandardVectors();
    TestCrc32IncrementalStreaming();
    TestChunkingCalculations();
    TestSafePartialFileLifecycle();
    TestDirectoryTreeTraversal();
    TestCancellationTokenLogic();
    TestPathTraversalPrevention();

    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "TEST RESULTS: " << g_passedTests << " / " << g_totalTests << " PASSED";
    if (g_failedTests > 0) {
        std::cout << " (" << g_failedTests << " FAILED)\n";
    } else {
        std::cout << " (ALL PASSED)\n";
    }
    std::cout << "----------------------------------------------------------------------\n\n";

    return (g_failedTests == 0) ? 0 : 1;
}
