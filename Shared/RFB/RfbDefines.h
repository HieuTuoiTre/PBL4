#pragma once

#include <cstdint>
#include <cstddef>

namespace Rfb {

    // RFC 6143 §7.1.1 - Protocol Version handshake string (12 bytes including \n)
    constexpr char VERSION_3_8_STRING[13] = "RFB 003.008\n";
    constexpr size_t VERSION_STRING_LENGTH = 12;

    // Standard VNC TCP Port
    constexpr int PORT_VNC = 5900;

    // RFC 6143 §7.1.2 & §7.2.2 - Security Types
    enum SecurityType : uint8_t {
        SECURITY_INVALID  = 0,
        SECURITY_NONE     = 1,
        SECURITY_VNC_AUTH = 2
    };

    // RFC 6143 §7.1.3 - Security Result
    enum SecurityResult : uint32_t {
        SECURITY_RESULT_OK     = 0,
        SECURITY_RESULT_FAILED = 1
    };

    // RFC 6143 §7.5 - Client-to-Server Message Types
    enum ClientMsgType : uint8_t {
        CLIENT_SET_PIXEL_FORMAT           = 0,
        CLIENT_SET_ENCODINGS              = 2,
        CLIENT_FRAMEBUFFER_UPDATE_REQUEST = 3,
        CLIENT_KEY_EVENT                  = 4,
        CLIENT_POINTER_EVENT              = 5,
        CLIENT_CUT_TEXT                   = 6
    };

    // RFC 6143 §7.6 - Server-to-Client Message Types
    enum ServerMsgType : uint8_t {
        SERVER_FRAMEBUFFER_UPDATE         = 0,
        SERVER_SET_COLOUR_MAP_ENTRIES     = 1,
        SERVER_BELL                       = 2,
        SERVER_CUT_TEXT                   = 3
    };

    // RFC 6143 §7.7 - Encodings
    enum EncodingType : int32_t {
        ENCODING_RAW               = 0,
        ENCODING_COPYRECT          = 1,
        ENCODING_RRE               = 2,
        ENCODING_HEXTILE           = 5,
        ENCODING_TIGHT             = 7,
        ENCODING_ZRLE              = 16,

        // Pseudo-Encodings
        ENCODING_COMPRESS_LEVEL_0  = -256,
        ENCODING_JPEG_QUALITY_0    = -32,
        ENCODING_DESKTOP_SIZE      = -223
    };

    // RFC 6143 §7.5.5 - Pointer Event button mask flags
    enum PointerButtonMask : uint8_t {
        BUTTON_NONE   = 0,
        BUTTON_LEFT   = 1 << 0, // 0x01
        BUTTON_MIDDLE = 1 << 1, // 0x02
        BUTTON_RIGHT  = 1 << 2, // 0x04
        WHEEL_UP      = 1 << 3, // 0x08
        WHEEL_DOWN    = 1 << 4  // 0x10
    };

} // namespace Rfb
