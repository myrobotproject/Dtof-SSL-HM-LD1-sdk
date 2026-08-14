#pragma once

#include <cstdint>

#include "internal/frame_event.hpp"
#include "internal/timestamp_normalizer.hpp"

namespace hm_ld1::internal {

void NormalizeUvcTimestamp(
    Measurement* measurement,
    RelativeMillisecondNormalizer* normalizer,
    uint64_t hostObservationUs,
    uint64_t* lastTimestampUs);

}  // namespace hm_ld1::internal
