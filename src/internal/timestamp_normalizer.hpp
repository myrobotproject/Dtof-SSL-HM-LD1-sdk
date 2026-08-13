#pragma once

#include <cstdint>
#include <optional>

namespace hm_ld1::internal {

uint64_t SystemTimeNowUs();

class RelativeMillisecondNormalizer {
public:
    explicit RelativeMillisecondNormalizer(uint64_t hostAnchorUs = 0);

    void Reset(uint64_t hostAnchorUs = 0);
    uint64_t Normalize(uint32_t rawMilliseconds);
    uint64_t Normalize(uint32_t rawMilliseconds, uint64_t hostNowUs);

private:
    uint64_t hostAnchorUs_ = 0;
    uint64_t restartAnchorUs_ = 0;
    bool initialized_ = false;
    uint32_t previousRawMilliseconds_ = 0;
    uint64_t logicalMilliseconds_ = 0;
    uint64_t lastOutputUs_ = 0;
    uint64_t lastHostObservationUs_ = 0;
    bool hasHostObservation_ = false;
};

class RelativeSecondNormalizer {
public:
    explicit RelativeSecondNormalizer(uint64_t hostAnchorUs = 0);

    void Reset(uint64_t hostAnchorUs = 0);
    uint64_t Normalize(uint32_t rawSeconds, uint32_t rawNanoseconds);
    uint64_t Normalize(uint32_t rawSeconds, uint32_t rawNanoseconds, uint64_t hostNowUs);

private:
    uint64_t hostAnchorUs_ = 0;
    uint64_t restartAnchorUs_ = 0;
    bool initialized_ = false;
    uint32_t previousRawSeconds_ = 0;
    uint32_t previousRawNanoseconds_ = 0;
    uint64_t logicalMicroseconds_ = 0;
    uint64_t lastOutputUs_ = 0;
    uint64_t lastHostObservationUs_ = 0;
    bool hasHostObservation_ = false;
};

class SerialTimestampNormalizer {
public:
    explicit SerialTimestampNormalizer(uint64_t hostAnchorUs = 0);

    void Reset(uint64_t hostAnchorUs = 0);
    uint64_t Normalize(uint32_t rawMilliseconds, uint64_t hostNowUs = 0);

private:
    RelativeMillisecondNormalizer relativeNormalizer_;
    bool initialized_ = false;
    uint32_t previousRawMilliseconds_ = 0;
    uint64_t lastOutputUs_ = 0;
};

std::optional<uint64_t> EpochSecondsNanosecondsToUs(uint32_t seconds, uint32_t nanoseconds);
bool IsNearHostEpoch(uint64_t timestampUs, uint64_t hostNowUs, uint64_t toleranceUs);

}  // namespace hm_ld1::internal
