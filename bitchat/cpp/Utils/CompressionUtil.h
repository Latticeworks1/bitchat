#pragma once

#include <vector>
#include <cstdint>

class CompressionUtil {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data);
};
