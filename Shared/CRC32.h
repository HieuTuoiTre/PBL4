#pragma once

#include <cstdint>
#include <cstddef>

class CRC32 {
public:
    // Calculates CRC32 checksum over the entire data buffer in one pass.
    static uint32_t Calculate(const void* data, size_t length);

    // Incrementally updates a running CRC32 checksum with a new chunk of data.
    // For the initial chunk, pass currentCrc = 0.
    static uint32_t Update(uint32_t currentCrc, const void* data, size_t length);
};
