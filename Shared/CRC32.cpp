#include "CRC32.h"

namespace {
    // Generate 256-entry lookup table for IEEE 802.3 CRC32 polynomial (0xEDB88320)
    struct CrcTable {
        uint32_t table[256];
        constexpr CrcTable() : table() {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t crc = i;
                for (int j = 0; j < 8; ++j) {
                    if (crc & 1) {
                        crc = (crc >> 1) ^ 0xEDB88320U;
                    } else {
                        crc >>= 1;
                    }
                }
                table[i] = crc;
            }
        }
    };

    static constexpr CrcTable g_crcTable;
}

uint32_t CRC32::Update(uint32_t currentCrc, const void* data, size_t length) {
    if (!data || length == 0) {
        return currentCrc;
    }

    const uint8_t* bytePtr = static_cast<const uint8_t*>(data);
    uint32_t crc = currentCrc ^ 0xFFFFFFFFU;

    for (size_t i = 0; i < length; ++i) {
        uint8_t tableIndex = static_cast<uint8_t>((crc ^ bytePtr[i]) & 0xFF);
        crc = g_crcTable.table[tableIndex] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFU;
}

uint32_t CRC32::Calculate(const void* data, size_t length) {
    return Update(0, data, length);
}
