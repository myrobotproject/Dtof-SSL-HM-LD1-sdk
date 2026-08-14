#include "internal/uvc_timestamp.hpp"

namespace hm_ld1::internal {

void NormalizeUvcTimestamp(
    Measurement* measurement,
    RelativeMillisecondNormalizer* normalizer,
    uint64_t hostObservationUs,
    uint64_t* lastTimestampUs) {
    if (measurement == nullptr || normalizer == nullptr || lastTimestampUs == nullptr) {
        return;
    }

    uint64_t valueUs = hostObservationUs;
    if (measurement->clock.device.valid) {
        valueUs = normalizer->Normalize(measurement->clock.device.raw0, hostObservationUs);
        measurement->clock.device.unit = TimestampUnit::Microseconds;
    } else {
        measurement->clock.device.valid = true;
        measurement->clock.device.raw0 = 0;
        measurement->clock.device.raw1 = 0;
    }

    if (*lastTimestampUs != 0 && valueUs <= *lastTimestampUs) {
        valueUs = *lastTimestampUs + 1ull;
    }
    *lastTimestampUs = valueUs;
    measurement->clock.device.value = valueUs;
    measurement->clock.device.unit = TimestampUnit::Microseconds;
}

}  // namespace hm_ld1::internal
