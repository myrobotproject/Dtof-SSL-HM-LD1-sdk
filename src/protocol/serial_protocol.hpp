#pragma once

#include <cstdint>
#include <vector>

#include "internal/frame_event.hpp"

namespace hm_ld1 {

constexpr uint8_t kInfoMsgId = 0x21;
constexpr uint8_t kDataMsgId = 0x22;

bool ParseSerialInfoPacket(const std::vector<uint8_t>& payload, DeviceInfo* info);
bool ParseSerialDataFrame(const std::vector<uint8_t>& payload, internal::Measurement* measurement);

}  // namespace hm_ld1


