#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "internal/frame_event.hpp"

namespace hm_ld1 {

bool ParseUdpDataFrame(const uint8_t* payload, size_t payloadSize, internal::Measurement* measurement, std::string* error);
bool ParseUdpDataFrame(const std::vector<uint8_t>& payload, internal::Measurement* measurement, std::string* error);

}  // namespace hm_ld1


