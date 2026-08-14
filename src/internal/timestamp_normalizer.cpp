#include "internal/timestamp_normalizer.hpp"

#include <chrono>
#include <cstdint>
#include <limits>

namespace hm_ld1::internal {
namespace {

constexpr uint64_t kMicrosecondsPerMillisecond = 1000ull;
constexpr uint64_t kMicrosecondsPerSecond = 1000000ull;
constexpr uint32_t kPpsPeriodMilliseconds = 1000u;
constexpr uint32_t kPpsWrapThresholdMilliseconds = 500u;
constexpr uint32_t kPpsUpperBoundaryMilliseconds = 900u;
constexpr uint32_t kPpsLowerBoundaryMilliseconds = 100u;
constexpr uint64_t kLargeRelativeJumpUs = 5ull * kMicrosecondsPerSecond;
constexpr uint64_t kHostJumpSlackUs = 500'000ull;
constexpr uint32_t kEpochLowerBoundSeconds = 946684800u;   // 2000-01-01 UTC
constexpr uint32_t kEpochUpperBoundSeconds = 4102444800u;  // 2100-01-01 UTC

uint64_t ResolveAnchor(uint64_t hostAnchorUs) {
    return hostAnchorUs != 0 ? hostAnchorUs : SystemTimeNowUs();
}

bool HostElapsedIsMuchSmaller(uint64_t rawDeltaUs, uint64_t hostElapsedUs) {
    return rawDeltaUs > kLargeRelativeJumpUs &&
        hostElapsedUs + kHostJumpSlackUs < rawDeltaUs / 4ull;
}

}  // namespace

uint64_t SystemTimeNowUs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

RelativeMillisecondNormalizer::RelativeMillisecondNormalizer(uint64_t hostAnchorUs) {
    Reset(hostAnchorUs);
}

void RelativeMillisecondNormalizer::Reset(uint64_t hostAnchorUs) {
    hostAnchorUs_ = hostAnchorUs;
    restartAnchorUs_ = hostAnchorUs_;
    initialized_ = false;
    previousRawMilliseconds_ = 0;
    logicalMilliseconds_ = 0;
    lastOutputUs_ = 0;
    lastHostObservationUs_ = 0;
    hasHostObservation_ = false;
}

uint64_t RelativeMillisecondNormalizer::Normalize(uint32_t rawMilliseconds) {
    return Normalize(rawMilliseconds, SystemTimeNowUs());
}

uint64_t RelativeMillisecondNormalizer::Normalize(uint32_t rawMilliseconds, uint64_t hostNowUs) {
    const uint64_t observationUs = ResolveAnchor(hostNowUs);
    if (!initialized_) {
        initialized_ = true;
        previousRawMilliseconds_ = rawMilliseconds;
        logicalMilliseconds_ = 0;
        if (hostAnchorUs_ == 0) {
            hostAnchorUs_ = observationUs;
            restartAnchorUs_ = observationUs;
        }
        lastOutputUs_ = hostAnchorUs_;
        lastHostObservationUs_ = observationUs;
        hasHostObservation_ = true;
        return lastOutputUs_;
    }

    const uint64_t hostElapsedUs = hasHostObservation_ && observationUs >= lastHostObservationUs_
        ? observationUs - lastHostObservationUs_
        : 0ull;

    uint64_t deltaMilliseconds = 0;
    if (rawMilliseconds >= previousRawMilliseconds_) {
        deltaMilliseconds = static_cast<uint64_t>(rawMilliseconds - previousRawMilliseconds_);
        const uint64_t rawDeltaUs = deltaMilliseconds * kMicrosecondsPerMillisecond;
        if (HostElapsedIsMuchSmaller(rawDeltaUs, hostElapsedUs)) {
            restartAnchorUs_ = observationUs > lastOutputUs_ ? observationUs : lastOutputUs_ + 1ull;
            hostAnchorUs_ = restartAnchorUs_;
            logicalMilliseconds_ = 0;
            previousRawMilliseconds_ = rawMilliseconds;
            lastHostObservationUs_ = observationUs;
            hasHostObservation_ = true;
            lastOutputUs_ = restartAnchorUs_;
            return lastOutputUs_;
        }
    } else {
        const uint32_t backward = previousRawMilliseconds_ - rawMilliseconds;
        if (backward > std::numeric_limits<uint32_t>::max() / 2u) {
            deltaMilliseconds =
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max() - previousRawMilliseconds_) +
                static_cast<uint64_t>(rawMilliseconds) + 1ull;
        } else if (previousRawMilliseconds_ >= kPpsUpperBoundaryMilliseconds &&
                   previousRawMilliseconds_ <= kPpsPeriodMilliseconds &&
                   rawMilliseconds <= kPpsLowerBoundaryMilliseconds &&
                   backward > kPpsWrapThresholdMilliseconds) {
            deltaMilliseconds =
                static_cast<uint64_t>(kPpsPeriodMilliseconds - previousRawMilliseconds_) +
                static_cast<uint64_t>(rawMilliseconds);
        } else if (backward <= kPpsWrapThresholdMilliseconds) {
            // A small backwards step is normally a reordered or duplicated frame.
            // Keep the raw predecessor so a later forward sample resumes normally.
            return ++lastOutputUs_;
        } else {
            restartAnchorUs_ = observationUs > lastOutputUs_ ? observationUs : lastOutputUs_ + 1ull;
            hostAnchorUs_ = restartAnchorUs_;
            logicalMilliseconds_ = 0;
            previousRawMilliseconds_ = rawMilliseconds;
            lastHostObservationUs_ = observationUs;
            hasHostObservation_ = true;
            lastOutputUs_ = restartAnchorUs_;
            return lastOutputUs_;
        }
    }

    logicalMilliseconds_ += deltaMilliseconds;
    previousRawMilliseconds_ = rawMilliseconds;
    lastHostObservationUs_ = observationUs;
    hasHostObservation_ = true;
    const uint64_t candidate = hostAnchorUs_ + logicalMilliseconds_ * kMicrosecondsPerMillisecond;
    lastOutputUs_ = candidate > lastOutputUs_ ? candidate : lastOutputUs_ + 1ull;
    return lastOutputUs_;
}

uint32_t RelativeMillisecondNormalizer::LastAcceptedRawMilliseconds() const {
    return previousRawMilliseconds_;
}

RelativeSecondNormalizer::RelativeSecondNormalizer(uint64_t hostAnchorUs) {
    Reset(hostAnchorUs);
}

void RelativeSecondNormalizer::Reset(uint64_t hostAnchorUs) {
    hostAnchorUs_ = hostAnchorUs;
    restartAnchorUs_ = hostAnchorUs_;
    initialized_ = false;
    previousRawSeconds_ = 0;
    previousRawNanoseconds_ = 0;
    logicalMicroseconds_ = 0;
    lastOutputUs_ = 0;
    lastHostObservationUs_ = 0;
    hasHostObservation_ = false;
}

uint64_t RelativeSecondNormalizer::Normalize(uint32_t rawSeconds, uint32_t rawNanoseconds) {
    return Normalize(rawSeconds, rawNanoseconds, SystemTimeNowUs());
}

uint64_t RelativeSecondNormalizer::Normalize(
    uint32_t rawSeconds,
    uint32_t rawNanoseconds,
    uint64_t hostNowUs) {
    const uint64_t observationUs = ResolveAnchor(hostNowUs);
    if (!initialized_) {
        initialized_ = true;
        previousRawSeconds_ = rawSeconds;
        previousRawNanoseconds_ = rawNanoseconds;
        logicalMicroseconds_ = 0;
        if (hostAnchorUs_ == 0) {
            hostAnchorUs_ = observationUs;
            restartAnchorUs_ = observationUs;
        }
        lastOutputUs_ = hostAnchorUs_;
        lastHostObservationUs_ = observationUs;
        hasHostObservation_ = true;
        return lastOutputUs_;
    }

    const uint64_t hostElapsedUs = hasHostObservation_ && observationUs >= lastHostObservationUs_
        ? observationUs - lastHostObservationUs_
        : 0ull;

    uint64_t deltaSeconds = 0;
    if (rawSeconds >= previousRawSeconds_) {
        deltaSeconds = static_cast<uint64_t>(rawSeconds - previousRawSeconds_);
    } else {
        const uint32_t backward = previousRawSeconds_ - rawSeconds;
        if (backward > std::numeric_limits<uint32_t>::max() / 2u) {
            deltaSeconds =
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max() - previousRawSeconds_) +
                static_cast<uint64_t>(rawSeconds) + 1ull;
        } else if (backward <= 1u) {
            // A one-second backwards step can be packet reordering; do not
            // move the host anchor until the stream shows a larger reset.
            return ++lastOutputUs_;
        } else {
            restartAnchorUs_ = observationUs;
            if (restartAnchorUs_ <= lastOutputUs_) {
                restartAnchorUs_ = lastOutputUs_ + 1ull;
            }
            hostAnchorUs_ = restartAnchorUs_;
            logicalMicroseconds_ = 0;
            previousRawSeconds_ = rawSeconds;
            previousRawNanoseconds_ = rawNanoseconds;
            lastHostObservationUs_ = observationUs;
            hasHostObservation_ = true;
            lastOutputUs_ = restartAnchorUs_;
            return lastOutputUs_;
        }
    }

    const int64_t nanosecondDelta =
        static_cast<int64_t>(rawNanoseconds) - static_cast<int64_t>(previousRawNanoseconds_);
    const int64_t deltaMicroseconds =
        static_cast<int64_t>(deltaSeconds * kMicrosecondsPerSecond) + nanosecondDelta / 1000ll;
    if (deltaMicroseconds < 0) {
        return ++lastOutputUs_;
    }

    if (HostElapsedIsMuchSmaller(static_cast<uint64_t>(deltaMicroseconds), hostElapsedUs)) {
        restartAnchorUs_ = observationUs > lastOutputUs_ ? observationUs : lastOutputUs_ + 1ull;
        hostAnchorUs_ = restartAnchorUs_;
        logicalMicroseconds_ = 0;
        previousRawSeconds_ = rawSeconds;
        previousRawNanoseconds_ = rawNanoseconds;
        lastHostObservationUs_ = observationUs;
        hasHostObservation_ = true;
        lastOutputUs_ = restartAnchorUs_;
        return lastOutputUs_;
    }

    logicalMicroseconds_ += static_cast<uint64_t>(deltaMicroseconds);
    previousRawSeconds_ = rawSeconds;
    previousRawNanoseconds_ = rawNanoseconds;
    lastHostObservationUs_ = observationUs;
    hasHostObservation_ = true;
    const uint64_t candidate = hostAnchorUs_ + logicalMicroseconds_;
    lastOutputUs_ = candidate > lastOutputUs_ ? candidate : lastOutputUs_ + 1ull;
    return lastOutputUs_;
}

SerialTimestampNormalizer::SerialTimestampNormalizer(uint64_t hostAnchorUs)
    : relativeNormalizer_(hostAnchorUs) {}

void SerialTimestampNormalizer::Reset(uint64_t hostAnchorUs) {
    relativeNormalizer_.Reset(hostAnchorUs);
    initialized_ = false;
    previousRawMilliseconds_ = 0;
    lastOutputUs_ = 0;
}

uint64_t SerialTimestampNormalizer::Normalize(uint32_t rawMilliseconds, uint64_t hostNowUs) {
    bool smallBackwardReorder = false;
    if (initialized_ && rawMilliseconds < previousRawMilliseconds_) {
        const uint32_t backward = previousRawMilliseconds_ - rawMilliseconds;
        smallBackwardReorder = backward <= kPpsWrapThresholdMilliseconds;
    }

    if (initialized_ && !smallBackwardReorder) {
        const bool ppsToCounter = previousRawMilliseconds_ <= kPpsPeriodMilliseconds &&
            rawMilliseconds > 1500u;
        const bool is32BitWrap = previousRawMilliseconds_ >= 0xfffff000u &&
            rawMilliseconds <= kPpsPeriodMilliseconds;
        const bool counterToPpsOrRestart = previousRawMilliseconds_ > kPpsPeriodMilliseconds &&
            !is32BitWrap && rawMilliseconds <= kPpsPeriodMilliseconds;
        if (ppsToCounter || counterToPpsOrRestart) {
            relativeNormalizer_.Reset(ResolveAnchor(hostNowUs));
        }
    }

    const uint64_t observationUs = ResolveAnchor(hostNowUs);
    uint64_t valueUs = relativeNormalizer_.Normalize(rawMilliseconds, observationUs);
    if (lastOutputUs_ != 0 && valueUs <= lastOutputUs_) {
        valueUs = lastOutputUs_ + 1ull;
    }
    initialized_ = true;
    previousRawMilliseconds_ = relativeNormalizer_.LastAcceptedRawMilliseconds();
    lastOutputUs_ = valueUs;
    return valueUs;
}

std::optional<uint64_t> EpochSecondsNanosecondsToUs(uint32_t seconds, uint32_t nanoseconds) {
    if (nanoseconds >= 1000000000u ||
        seconds < kEpochLowerBoundSeconds ||
        seconds > kEpochUpperBoundSeconds) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(seconds) * kMicrosecondsPerSecond +
        static_cast<uint64_t>(nanoseconds) / 1000ull;
}

bool IsNearHostEpoch(uint64_t timestampUs, uint64_t hostNowUs, uint64_t toleranceUs) {
    const uint64_t difference = timestampUs >= hostNowUs ? timestampUs - hostNowUs : hostNowUs - timestampUs;
    return difference <= toleranceUs;
}

}  // namespace hm_ld1::internal
