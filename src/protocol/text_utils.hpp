#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hm_ld1 {
namespace protocol_detail {

inline std::string TrimFixedString(const uint8_t* data, size_t size) {
    size_t end = 0;
    while (end < size && data[end] != 0) {
        ++end;
    }
    std::string text(reinterpret_cast<const char*>(data), end);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

}  // namespace protocol_detail
}  // namespace hm_ld1
