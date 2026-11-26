#include <iostream>
#include <cassert>
#include <cstring>
#include "../Shared/RFB/RfbDefines.h"
#include "../Shared/RFB/RfbMessages.h"
#include "../Shared/RFB/KeySymMapping.h"

using namespace Rfb;

int main() {
    std::cout << "[TEST] Starting RFB 3.8 Protocol struct verification...\n";

    // 1. Verify exact struct sizes matching RFC 6143
    assert(sizeof(RfbPixelFormat) == 16);
    assert(sizeof(RfbServerInitHeader) == 24);
    assert(sizeof(RfbClientInit) == 1);
    assert(sizeof(RfbSetPixelFormatMsg) == 20);
    assert(sizeof(RfbSetEncodingsHeader) == 4);
    assert(sizeof(RfbFramebufferUpdateReq) == 10);
    assert(sizeof(RfbKeyEvent) == 8);
    assert(sizeof(RfbPointerEvent) == 6);
    assert(sizeof(RfbCutTextHeader) == 8);
    assert(sizeof(RfbFramebufferUpdateHeader) == 4);
    assert(sizeof(RfbRectangleHeader) == 12);

    std::cout << "[PASS] All RFB 3.8 PDU header sizes match RFC 6143 exact byte sizes!\n";

    // 2. Verify KeySym Mapping
    assert(KeySymToWinVk(0xFF0D) == VK_RETURN);
    assert(KeySymToWinVk(0xFF1B) == VK_ESCAPE);
    assert(KeySymToWinVk(0xFF08) == VK_BACK);
    assert(KeySymToWinVk(0xFF09) == VK_TAB);
    assert(KeySymToWinVk(0xFF51) == VK_LEFT);
    assert(KeySymToWinVk(0xFF52) == VK_UP);
    assert(KeySymToWinVk(0xFF53) == VK_RIGHT);
    assert(KeySymToWinVk(0xFF54) == VK_DOWN);
    assert(KeySymToWinVk(0xFFFF) == VK_DELETE);
    assert(KeySymToWinVk(0xFFBE) == VK_F1);
    assert(KeySymToWinVk(0xFFC9) == VK_F12);

    // ASCII characters
    assert(KeySymToWinVk('a') == 'A');
    assert(KeySymToWinVk('A') == 'A');
    assert(KeySymToWinVk('1') == '1');

    std::cout << "[PASS] KeySym mapping verified successfully!\n";

    std::cout << "\n>>> ALL RFB PROTOCOL STRUCT TESTS PASSED (100%) <<<\n";
    return 0;
}
