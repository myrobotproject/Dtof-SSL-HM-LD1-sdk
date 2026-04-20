#pragma once

#include <string>
#include <vector>

#include "internal/frame_event.hpp"

namespace hm_ld1 {

enum class UvcParsedFrameType {
    None,
    Info,
    Depth,
    PointCloud,
};

struct UvcParsedFrame {
    UvcParsedFrameType type = UvcParsedFrameType::None;
    internal::SourceEvent event;
};

bool ParseUvcPayload(
    const std::vector<uint8_t>& payload,
    UvcStreamProfile profile,
    UvcParsedFrame* frame,
    std::string* error);

}  // namespace hm_ld1


