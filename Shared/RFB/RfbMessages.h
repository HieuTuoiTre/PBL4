#pragma once

#include "RfbDefines.h"
#include <winsock2.h>

#pragma pack(push, 1)

namespace Rfb {

    /**
     * @brief RFC 6143 §7.4 - PIXEL_FORMAT (16 bytes)
     */
    struct RfbPixelFormat {
        uint8_t bitsPerPixel;     // 8, 16, 32 (typically 32 in modern systems)
        uint8_t depth;            // Color depth (typically 24)
        uint8_t bigEndianFlag;    // 0 = Little-Endian, 1 = Big-Endian
        uint8_t trueColorFlag;    // 1 = TrueColor, 0 = ColorMap
        uint16_t redMax;          // Max red value (Big-Endian in network packets)
        uint16_t greenMax;        // Max green value
        uint16_t blueMax;         // Max blue value
        uint8_t redShift;         // Number of shifts for red (e.g. 16 for BGRX/RGBX)
        uint8_t greenShift;       // Number of shifts for green (e.g. 8)
        uint8_t blueShift;        // Number of shifts for blue (e.g. 0)
        uint8_t padding[3];       // 3 bytes padding
    };

    /**
     * @brief RFC 6143 §7.3.1 - ClientInit (1 byte)
     */
    struct RfbClientInit {
        uint8_t sharedFlag;       // Non-zero = shared desktop, 0 = exclusive access
    };

    /**
     * @brief RFC 6143 §7.3.2 - ServerInit Header (24 bytes + name string)
     */
    struct RfbServerInitHeader {
        uint16_t framebufferWidth;  // Big-Endian
        uint16_t framebufferHeight; // Big-Endian
        RfbPixelFormat pixelFormat; // 16 bytes
        uint32_t nameLength;        // Big-Endian
    };

    /**
     * @brief RFC 6143 §7.5.1 - SetPixelFormat (20 bytes)
     */
    struct RfbSetPixelFormatMsg {
        uint8_t messageType;        // 0 = CLIENT_SET_PIXEL_FORMAT
        uint8_t padding[3];
        RfbPixelFormat format;
    };

    /**
     * @brief RFC 6143 §7.5.2 - SetEncodings Header (4 bytes + encodings array)
     */
    struct RfbSetEncodingsHeader {
        uint8_t messageType;        // 2 = CLIENT_SET_ENCODINGS
        uint8_t padding;
        uint16_t numEncodings;      // Big-Endian
    };

    /**
     * @brief RFC 6143 §7.5.3 - FramebufferUpdateRequest (10 bytes)
     */
    struct RfbFramebufferUpdateReq {
        uint8_t messageType;        // 3 = CLIENT_FRAMEBUFFER_UPDATE_REQUEST
        uint8_t incremental;        // 0 = Full screen, 1 = Incremental changes only
        uint16_t x;                 // Big-Endian
        uint16_t y;                 // Big-Endian
        uint16_t width;             // Big-Endian
        uint16_t height;            // Big-Endian
    };

    /**
     * @brief RFC 6143 §7.5.4 - KeyEvent (8 bytes)
     */
    struct RfbKeyEvent {
        uint8_t messageType;        // 4 = CLIENT_KEY_EVENT
        uint8_t downFlag;           // 1 = KeyDown, 0 = KeyUp
        uint16_t padding;
        uint32_t key;               // Big-Endian X11 KeySym
    };

    /**
     * @brief RFC 6143 §7.5.5 - PointerEvent (6 bytes)
     */
    struct RfbPointerEvent {
        uint8_t messageType;        // 5 = CLIENT_POINTER_EVENT
        uint8_t buttonMask;         // Bitmask of PointerButtonMask
        uint16_t x;                 // Big-Endian
        uint16_t y;                 // Big-Endian
    };

    /**
     * @brief RFC 6143 §7.5.6 & §7.6.4 - ClientCutText / ServerCutText (8 bytes + text)
     */
    struct RfbCutTextHeader {
        uint8_t messageType;        // 6 = CLIENT_CUT_TEXT or 3 = SERVER_CUT_TEXT
        uint8_t padding[3];
        uint32_t length;            // Big-Endian text length
    };

    /**
     * @brief RFC 6143 §7.6.1 - FramebufferUpdate Header (4 bytes)
     */
    struct RfbFramebufferUpdateHeader {
        uint8_t messageType;        // 0 = SERVER_FRAMEBUFFER_UPDATE
        uint8_t padding;
        uint16_t numRectangles;     // Big-Endian
    };

    /**
     * @brief RFC 6143 §7.6.1 - Rectangle Header (12 bytes)
     */
    struct RfbRectangleHeader {
        uint16_t x;                 // Big-Endian
        uint16_t y;                 // Big-Endian
        uint16_t width;             // Big-Endian
        uint16_t height;            // Big-Endian
        int32_t encodingType;       // Big-Endian EncodingType
    };

} // namespace Rfb

#pragma pack(pop)

#include <vector>

namespace Rfb {

    /**
     * @brief Builds a standard FramebufferUpdate header for N rectangles.
     */
    inline RfbFramebufferUpdateHeader BuildUpdateHeader(uint16_t numRectangles) {
        RfbFramebufferUpdateHeader hdr;
        hdr.messageType = SERVER_FRAMEBUFFER_UPDATE;
        hdr.padding = 0;
        hdr.numRectangles = htons(numRectangles);
        return hdr;
    }

    /**
     * @brief Builds a Tight/JPEG Rectangle payload for standard VNC viewers.
     */
    inline std::vector<char> BuildTightJpegRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* jpegData, size_t jpegLen) {
        std::vector<char> buf;
        buf.reserve(sizeof(RfbRectangleHeader) + 4 + jpegLen);

        RfbRectangleHeader rh;
        rh.x = htons(x);
        rh.y = htons(y);
        rh.width = htons(w);
        rh.height = htons(h);
        rh.encodingType = htonl(ENCODING_TIGHT);

        const char* rhPtr = reinterpret_cast<const char*>(&rh);
        buf.insert(buf.end(), rhPtr, rhPtr + sizeof(rh));

        // Tight Compression Control byte: 0x90 indicates JPEG sub-encoding
        buf.push_back((char)0x90);

        // Compact length encoding (1, 2, or 3 bytes) as specified by TightVNC
        if (jpegLen < 128) {
            buf.push_back((char)(jpegLen & 0x7F));
        } else if (jpegLen < 16384) {
            buf.push_back((char)((jpegLen & 0x7F) | 0x80));
            buf.push_back((char)((jpegLen >> 7) & 0xFF));
        } else {
            buf.push_back((char)((jpegLen & 0x7F) | 0x80));
            buf.push_back((char)(((jpegLen >> 7) & 0x7F) | 0x80));
            buf.push_back((char)((jpegLen >> 14) & 0xFF));
        }

        buf.insert(buf.end(), jpegData, jpegData + jpegLen);
        return buf;
    }

    /**
     * @brief Builds a Raw Rectangle payload (RFC 6143 §7.7.1, Encoding Type 0)
     *        Used for RealVNC Connect Viewer and any standard VNC client that does not support Tight.
     */
    inline std::vector<char> BuildRawRectangle(
        uint16_t x, uint16_t y, uint16_t w, uint16_t h,
        const uint32_t* screenPixels, int screenWidth
    ) {
        std::vector<char> buf;
        size_t pixelBytes = (size_t)w * h * sizeof(uint32_t);
        buf.reserve(sizeof(RfbRectangleHeader) + pixelBytes);

        RfbRectangleHeader rh;
        rh.x = htons(x);
        rh.y = htons(y);
        rh.width = htons(w);
        rh.height = htons(h);
        rh.encodingType = htonl(ENCODING_RAW); // 0 = RFC 6143 Raw Encoding

        const char* rhPtr = reinterpret_cast<const char*>(&rh);
        buf.insert(buf.end(), rhPtr, rhPtr + sizeof(rh));

        // Copy each row of the rectangle from screenPixels
        for (uint16_t row = 0; row < h; ++row) {
            const char* rowStart = reinterpret_cast<const char*>(screenPixels + (size_t)(y + row) * screenWidth + x);
            buf.insert(buf.end(), rowStart, rowStart + (size_t)w * sizeof(uint32_t));
        }

        return buf;
    }

} // namespace Rfb
