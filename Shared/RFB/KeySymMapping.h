#pragma once

#include <windows.h>
#include <cstdint>

/**
 * @brief Maps X11 KeySym (used by RFB/VNC) to Windows Virtual-Key code (VK_*).
 * @see <X11/keysymdef.h>
 */
inline WPARAM KeySymToWinVk(uint32_t keysym) {
    // 1. Standard ASCII Printable Range (0x0020 - 0x007E)
    if (keysym >= 0x0020 && keysym <= 0x007E) {
        if (keysym >= 'a' && keysym <= 'z') {
            return (WPARAM)(keysym - 'a' + 'A');
        }
        return (WPARAM)keysym;
    }

    // 2. Special / Function Keys (X11 0xFF00 - 0xFFFF)
    switch (keysym) {
        case 0xFF08: return VK_BACK;
        case 0xFF09: return VK_TAB;
        case 0xFF0D: return VK_RETURN;
        case 0xFF1B: return VK_ESCAPE;
        case 0xFF63: return VK_INSERT;
        case 0xFFFF: return VK_DELETE;
        case 0xFF50: return VK_HOME;
        case 0xFF57: return VK_END;
        case 0xFF55: return VK_PRIOR; // Page Up
        case 0xFF56: return VK_NEXT;  // Page Down
        case 0xFF51: return VK_LEFT;
        case 0xFF52: return VK_UP;
        case 0xFF53: return VK_RIGHT;
        case 0xFF54: return VK_DOWN;

        // Function Keys F1 - F12
        case 0xFFBE: return VK_F1;
        case 0xFFBF: return VK_F2;
        case 0xFFC0: return VK_F3;
        case 0xFFC1: return VK_F4;
        case 0xFFC2: return VK_F5;
        case 0xFFC3: return VK_F6;
        case 0xFFC4: return VK_F7;
        case 0xFFC5: return VK_F8;
        case 0xFFC6: return VK_F9;
        case 0xFFC7: return VK_F10;
        case 0xFFC8: return VK_F11;
        case 0xFFC9: return VK_F12;

        // Modifier Keys
        case 0xFFE1: return VK_LSHIFT;
        case 0xFFE2: return VK_RSHIFT;
        case 0xFFE3: return VK_LCONTROL;
        case 0xFFE4: return VK_RCONTROL;
        case 0xFFE9: return VK_LMENU;   // Alt
        case 0xFFEA: return VK_RMENU;
        case 0xFFEB: return VK_LWIN;
        case 0xFFEC: return VK_RWIN;

        // Lock Keys
        case 0xFFE5: return VK_CAPITAL; // Caps Lock
        case 0xFF7F: return VK_NUMLOCK;
        case 0xFF14: return VK_SCROLL;
        default: break;
    }

    return 0;
}

/**
 * @brief Maps Windows Virtual-Key code (VK_*) to X11 KeySym.
 */
inline uint32_t WinVkToKeySym(WPARAM vk, bool isShiftDown) {
    if (vk >= 'A' && vk <= 'Z') {
        return isShiftDown ? (uint32_t)vk : (uint32_t)(vk - 'A' + 'a');
    }
    if (vk >= '0' && vk <= '9') {
        return (uint32_t)vk;
    }

    switch (vk) {
        case VK_BACK:    return 0xFF08;
        case VK_TAB:     return 0xFF09;
        case VK_RETURN:  return 0xFF0D;
        case VK_ESCAPE:  return 0xFF1B;
        case VK_SPACE:   return 0x0020;
        case VK_INSERT:  return 0xFF63;
        case VK_DELETE:  return 0xFFFF;
        case VK_HOME:    return 0xFF50;
        case VK_END:     return 0xFF57;
        case VK_PRIOR:   return 0xFF55;
        case VK_NEXT:    return 0xFF56;
        case VK_LEFT:    return 0xFF51;
        case VK_UP:      return 0xFF52;
        case VK_RIGHT:   return 0xFF53;
        case VK_DOWN:    return 0xFF54;

        case VK_F1:      return 0xFFBE;
        case VK_F2:      return 0xFFBF;
        case VK_F3:      return 0xFFC0;
        case VK_F4:      return 0xFFC1;
        case VK_F5:      return 0xFFC2;
        case VK_F6:      return 0xFFC3;
        case VK_F7:      return 0xFFC4;
        case VK_F8:      return 0xFFC5;
        case VK_F9:      return 0xFFC6;
        case VK_F10:     return 0xFFC7;
        case VK_F11:     return 0xFFC8;
        case VK_F12:     return 0xFFC9;

        case VK_SHIFT:
        case VK_LSHIFT:  return 0xFFE1;
        case VK_RSHIFT:  return 0xFFE2;
        case VK_CONTROL:
        case VK_LCONTROL:return 0xFFE3;
        case VK_RCONTROL:return 0xFFE4;
        case VK_MENU:
        case VK_LMENU:   return 0xFFE9;
        case VK_RMENU:   return 0xFFEA;
        case VK_LWIN:    return 0xFFEB;
        case VK_RWIN:    return 0xFFEC;

        case VK_CAPITAL: return 0xFFE5;
        case VK_NUMLOCK: return 0xFF7F;
        case VK_SCROLL:  return 0xFF14;
        default: break;
    }

    return 0;
}
