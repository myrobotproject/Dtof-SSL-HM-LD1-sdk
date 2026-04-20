#pragma once

#include <cstdint>
#include <cstring>

namespace hm_ld1 {
namespace protocol_detail {

inline uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8u));
}

inline uint32_t ReadLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) |
           (static_cast<uint32_t>(data[3]) << 24u);
}

inline float ReadLeFloat(const uint8_t* data) {
    const uint32_t bits = ReadLe32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace protocol_detail
}  // namespace hm_ld1

