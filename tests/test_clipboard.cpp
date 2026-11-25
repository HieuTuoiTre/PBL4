#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <cstring>

#include "../Shared/ClipboardManager.h"
#include "../Shared/Utils.h"
#include "../Shared/Protocol.h"
#include "../Shared/Crypto.h"
#include "../Shared/ControlEnvelope.h"

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
// GROUP 1: UTF-8 <-> UTF-16 Unicode & Vietnamese Diacritic Conversion Fidelity
// ======================================================================
void TestUnicodeFidelity() {
    PrintBanner("GROUP 1: Unicode & Vietnamese Diacritic Conversion Fidelity");

    // 1.1 Authoritative Dispatch Vietnamese String
    {
        std::string input = "Xin chào Việt Nam! 123 @#$";
        std::wstring wide = Utils::Utf8ToWide(input);
        std::string roundtrip = Utils::WideToUtf8(wide);
        ReportResult("1.1 Authoritative Vietnamese String", roundtrip == input,
                     "Original: " + input + " -> Bytes: " + std::to_string(roundtrip.size()));
    }

    // 1.2 Full Vietnamese Diacritic Character Set (All Vowel Accents)
    {
        std::string input = "Tiếng Việt: ắ, ằ, ẳ, ẵ, ặ, ê, ế, ề, ể, ễ, ệ, ô, ố, ồ, ổ, ỗ, ộ, ư, ứ, ừ, ử, ữ, ự, đ, Đ";
        std::wstring wide = Utils::Utf8ToWide(input);
        std::string roundtrip = Utils::WideToUtf8(wide);
        ReportResult("1.2 Full Vietnamese Vowel & Tone Set", roundtrip == input,
                     "Preserved " + std::to_string(wide.size()) + " WCHARs / " + std::to_string(roundtrip.size()) + " UTF-8 bytes");
    }

    // 1.3 Multi-Lingual International Scripts (Japanese, Chinese, Arabic)
    {
        std::string input = "日本語: こんにちは | 中文: 远程桌面 | العربية: مرحبا";
        std::wstring wide = Utils::Utf8ToWide(input);
        std::string roundtrip = Utils::WideToUtf8(wide);
        ReportResult("1.3 Multi-Lingual CJK & RTL Text", roundtrip == input,
                     "Multi-byte encoding roundtrip verified");
    }

    // 1.4 Emojis & 4-Byte UTF-8 Surrogate Pairs
    {
        std::string input = "Remote Desktop 🚀💻🔥✨🎉 High-DPI & Audio";
        std::wstring wide = Utils::Utf8ToWide(input);
        std::string roundtrip = Utils::WideToUtf8(wide);
        bool hasSurrogates = false;
        for (wchar_t wc : wide) {
            if (wc >= 0xD800 && wc <= 0xDFFF) {
                hasSurrogates = true;
                break;
            }
        }
        ReportResult("1.4 Emojis & UTF-16 Surrogate Pairs", roundtrip == input && hasSurrogates,
                     "Surrogate pairs correctly handled and reconstructed");
    }

    // 1.5 Special Punctuation, Slashes, XML & Newlines
    {
        std::string input = "Line1\r\nLine2\tTabbed\r\n\"Quotes\" & 'Apos' <tag val=\"1\"/> / \\ | ? ! @ # $ % ^ & * ( ) _ +";
        std::wstring wide = Utils::Utf8ToWide(input);
        std::string roundtrip = Utils::WideToUtf8(wide);
        ReportResult("1.5 Complex Whitespace & Control Symbols", roundtrip == input,
                     "Exact escape and control character preservation");
    }
}

// ======================================================================
// GROUP 2: Dual-Tier Echo Loop Prevention State Machine
// ======================================================================
void TestEchoLoopStateEngine() {
    PrintBanner("GROUP 2: Dual-Tier Echo Loop Prevention State Machine");

    // 2.1 Tier 1: Re-Entrancy Suppression Flag
    {
        ClipboardManager::Reset();
        ClipboardManager::MarkSettingLocal();
        bool flagActive = ClipboardManager::IsSettingLocal();
        bool syncResult = ClipboardManager::ShouldSync("Fresh text");
        bool flagAfter = ClipboardManager::IsSettingLocal();

        ReportResult("2.1 Tier 1 Flag Suppression",
                     flagActive == true && syncResult == false && flagAfter == false,
                     "Flag active -> ShouldSync returned false -> Flag consumed");
    }

    // 2.2 Tier 2: Content Hash & Cache Deduplication
    {
        ClipboardManager::Reset();
        bool firstSync = ClipboardManager::ShouldSync("Duplicate String Test");
        bool secondSync = ClipboardManager::ShouldSync("Duplicate String Test");
        bool thirdSync = ClipboardManager::ShouldSync("Duplicate String Test");

        ReportResult("2.2 Tier 2 Duplicate Rejection",
                     firstSync == true && secondSync == false && thirdSync == false,
                     "First sync PASS, subsequent identical calls REJECTED");
    }

    // 2.3 Alternating Text Transitions (A -> B -> A)
    {
        ClipboardManager::Reset();
        bool syncA1 = ClipboardManager::ShouldSync("Text A");
        bool syncB1 = ClipboardManager::ShouldSync("Text B");
        bool syncB2 = ClipboardManager::ShouldSync("Text B");
        bool syncA2 = ClipboardManager::ShouldSync("Text A");

        ReportResult("2.3 Alternating Content Transitions",
                     syncA1 == true && syncB1 == true && syncB2 == false && syncA2 == true,
                     "Transitions A -> B -> B(drop) -> A(pass) handled cleanly");
    }

    // 2.4 Multi-Stage Ping-Pong Simulation (Manager <-> Client)
    {
        ClipboardManager::Reset();
        int managerPacketsSent = 0;
        int clientPacketsSent = 0;

        // Step 1: Manager copies "Data1" locally
        if (ClipboardManager::ShouldSync("Data1")) managerPacketsSent++;

        // Step 2: Client receives "Data1", marks local write, updates cache
        ClipboardManager::MarkSettingLocal();
        // Client OS delivers WM_CLIPBOARDUPDATE
        if (ClipboardManager::ShouldSync("Data1")) clientPacketsSent++;

        // Step 3: Windows OS fires duplicate WM_CLIPBOARDUPDATE on Client (OS quirk)
        if (ClipboardManager::ShouldSync("Data1")) clientPacketsSent++;

        ReportResult("2.4 Simulated Ping-Pong Storm Kill",
                     managerPacketsSent == 1 && clientPacketsSent == 0,
                     "Manager sent 1, Client sent 0 (Ping-pong terminated at hop 1)");
    }
}

// ======================================================================
// GROUP 3: Size Boundaries & Hard 1 MB Cutoff
// ======================================================================
void TestSizeBoundaries() {
    PrintBanner("GROUP 3: Size Boundaries & Hard 1 MB Cutoff");

    // 3.1 Small & Intermediate Payload Sizes
    {
        ClipboardManager::Reset();
        std::string smallText(10, 'A');
        std::string midText(65536, 'B'); // 64 KB
        bool okSmall = ClipboardManager::ShouldSync(smallText);
        bool okMid = ClipboardManager::ShouldSync(midText);

        ReportResult("3.1 Small & Intermediate Payload Sizes",
                     okSmall && okMid, "10B and 64KB payloads accepted");
    }

    // 3.2 Exact 1 MB Boundary (1,048,576 Bytes)
    {
        ClipboardManager::Reset();
        std::string exact1MB(ClipboardManager::MAX_CLIPBOARD_TEXT_BYTES, 'X');
        bool ok1MB = ClipboardManager::ShouldSync(exact1MB);

        ReportResult("3.2 Exact 1 MB Boundary Payload",
                     ok1MB, "Payload of exactly 1,048,576 bytes accepted");
    }

    // 3.3 Exceeding 1 MB Limit by 1 Byte (1,048,577 Bytes)
    {
        ClipboardManager::Reset();
        std::string overflow(ClipboardManager::MAX_CLIPBOARD_TEXT_BYTES + 1, 'Y');
        bool okOverflow = ClipboardManager::ShouldSync(overflow);
        bool writeOverflow = ClipboardManager::WriteText(NULL, overflow);

        ReportResult("3.3 1 MB + 1 Byte Hard Cutoff",
                     !okOverflow && !writeOverflow,
                     "1,048,577 bytes rejected by both ShouldSync and WriteText");
    }

    // 3.4 Multi-Megabyte Oversized Payload (2 MB)
    {
        ClipboardManager::Reset();
        std::string huge(2 * 1024 * 1024, 'Z');
        bool okHuge = ClipboardManager::ShouldSync(huge);
        bool writeHuge = ClipboardManager::WriteText(NULL, huge);

        ReportResult("3.4 Multi-Megabyte (2 MB) Rejection",
                     !okHuge && !writeHuge,
                     "2 MB payload rejected immediately without memory exhaustion");
    }
}

// ======================================================================
// GROUP 4: Empty & Whitespace Text Handling
// ======================================================================
void TestEmptyAndWhitespace() {
    PrintBanner("GROUP 4: Empty & Whitespace Text Handling");

    // 4.1 Empty String Handling
    {
        ClipboardManager::Reset();
        bool okEmpty = ClipboardManager::ShouldSync("");
        ReportResult("4.1 Empty String Rejection", !okEmpty,
                     "Empty string rejected (prevents cluttering control stream)");
    }

    // 4.2 Whitespace-Only Strings
    {
        ClipboardManager::Reset();
        std::string spaces = "    ";
        bool okFirst = ClipboardManager::ShouldSync(spaces);
        bool okSecond = ClipboardManager::ShouldSync(spaces);

        ReportResult("4.2 Whitespace String Deduplication",
                     okFirst && !okSecond,
                     "Whitespace accepted on initial sync, deduplicated on repeated sync");
    }

    // 4.3 Newline-Only Strings
    {
        ClipboardManager::Reset();
        std::string newlines = "\r\n\r\n";
        bool okFirst = ClipboardManager::ShouldSync(newlines);
        bool okSecond = ClipboardManager::ShouldSync(newlines);

        ReportResult("4.3 Newline-Only String Handling",
                     okFirst && !okSecond,
                     "Newlines preserved and deduplicated");
    }
}

// ======================================================================
// GROUP 5: Hash Integrity & Sensitivity (FNV-1a)
// ======================================================================
void TestHashIntegrity() {
    PrintBanner("GROUP 5: Hash Integrity & Sensitivity (FNV-1a)");

    // 5.1 Hash Determinism
    {
        std::string text = "Deterministic Hash Verification String";
        uint32_t h1 = ClipboardManager::CalculateHash(text);
        uint32_t h2 = ClipboardManager::CalculateHash(text);
        ReportResult("5.1 Hash Determinism", h1 == h2 && h1 != 0,
                     "Identical string produces consistent hash: 0x" + std::to_string(h1));
    }

    // 5.2 Single-Bit & Single-Character Sensitivity
    {
        uint32_t h1 = ClipboardManager::CalculateHash("PBL4 Remote");
        uint32_t h2 = ClipboardManager::CalculateHash("PBL4 remote");
        uint32_t h3 = ClipboardManager::CalculateHash("PBL4 RemotE");

        ReportResult("5.2 Case & Character Sensitivity",
                     (h1 != h2) && (h2 != h3) && (h1 != h3),
                     "Case differences yield unique hashes");
    }

    // 5.3 Vietnamese Diacritic Sensitivity
    {
        uint32_t h1 = ClipboardManager::CalculateHash("Tiếng Việt");
        uint32_t h2 = ClipboardManager::CalculateHash("Tieng Viet");

        ReportResult("5.3 Diacritic Accent Sensitivity",
                     h1 != h2, "Accented vs unaccented text hashes differ");
    }
}

// ======================================================================
// GROUP 6: Protocol Framing & Control Stream Encryption Round-Trip
// ======================================================================
void TestProtocolAndEncryption() {
    PrintBanner("GROUP 6: Protocol Framing & Control Stream Encryption");

    // 6.1 Packet Header Structure Verification
    {
        Protocol::PacketHeader hdr;
        hdr.type = Protocol::MSG_CLIPBOARD_TEXT;
        hdr.size = 1234;

        ReportResult("6.1 PacketHeader Serialization Size",
                     sizeof(hdr) == 5 && hdr.type == 25,
                     "Header size is exactly 5 bytes; MSG_CLIPBOARD_TEXT == 25");
    }

    // 6.2 ControlEnvelope ChaCha20-Poly1305 Round-Trip with Clipboard Payload
    {
        uint8_t sessionKey[32];
        for (int i = 0; i < 32; ++i) sessionKey[i] = (uint8_t)(i * 7 + 3);

        std::string payload = "Xin chào Việt Nam! 123 @#$";
        std::vector<uint8_t> cipher;

        bool wrapOk = ControlEnvelope::WrapPacket(
            sessionKey, Protocol::MSG_CLIPBOARD_TEXT, payload.data(), (int)payload.size(), cipher
        );

        Protocol::PacketHeader innerHdr;
        std::vector<uint8_t> plainPayload;

        bool unwrapOk = ControlEnvelope::UnwrapPacket(
            sessionKey, cipher.data(), cipher.size(), innerHdr, plainPayload
        );

        std::string recovered((const char*)plainPayload.data(), plainPayload.size());

        ReportResult("6.2 Encrypted Clipboard Envelope Round-Trip",
                     wrapOk && unwrapOk && innerHdr.type == Protocol::MSG_CLIPBOARD_TEXT && recovered == payload,
                     "ChaCha20-Poly1305 round-trip preserved 100% data fidelity");
    }
}

// ======================================================================
// Main Test Runner Entry Point
// ======================================================================
int main() {
    std::cout << "\n======================================================================\n";
    std::cout << "          PBL4 CLIPBOARD SYNCHRONIZATION TEST SUITE (M2)              \n";
    std::cout << "======================================================================\n";

    TestUnicodeFidelity();
    TestEchoLoopStateEngine();
    TestSizeBoundaries();
    TestEmptyAndWhitespace();
    TestHashIntegrity();
    TestProtocolAndEncryption();

    PrintBanner("TEST SUITE SUMMARY");
    std::cout << "  Total Tests Executed : " << g_totalTests << "\n";
    std::cout << "  Passed Tests         : " << g_passedTests << " (" << (g_totalTests ? (g_passedTests * 100 / g_totalTests) : 0) << "%)\n";
    std::cout << "  Failed Tests         : " << g_failedTests << "\n";
    std::cout << "======================================================================\n";

    if (g_failedTests == 0) {
        std::cout << "  >>> ALL CLIPBOARD TESTS PASSED SUCCESSFULLY (100%) <<<\n";
        std::cout << "======================================================================\n\n";
        return 0;
    } else {
        std::cout << "  >>> TEST SUITE DETECTED FAILURES <<<\n";
        std::cout << "======================================================================\n\n";
        return 1;
    }
}
