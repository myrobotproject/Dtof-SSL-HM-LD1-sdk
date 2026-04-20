#include "protocol/udp_protocol.hpp"

#include "internal/error_utils.hpp"
#include "protocol/byte_utils.hpp"

namespace hm_ld1 {

bool ParseUdpDataFrame(const uint8_t* payload, size_t payloadSize, internal::Measurement* measurement, std::string* error) {
    constexpr size_t kUdpHeaderSize = 2 + 2 + 4 + 2 + 4 + 4 + 2 + 2 + 2 + 1 + (kCalibrationParameterCount * 4);
    constexpr size_t kUdpPixelStride = 4;
    constexpr size_t kUdpPayloadSize = kUdpHeaderSize + (kDepthPointCount * kUdpPixelStride);

    if (measurement == nullptr) {
        internal::SetError(error, "UDP measurement output pointer is null");
        return false;
    }
    internal::ClearError(error);
    if (payloadSize != kUdpPayloadSize) {
        internal::SetError(error, "Unexpected UDP payload size");
        return false;
    }
    if (payload == nullptr) {
        internal::SetError(error, "UDP payload pointer is null");
        return false;
    }

    *measurement = internal::Measurement();
    measurement->transportType = TransportType::Udp;

    size_t offset = 0;
    const uint16_t checkSum = protocol_detail::ReadLe16(payload + offset);
    offset += 2;
    const uint16_t seqNum = protocol_detail::ReadLe16(payload + offset);
    offset += 2;
    const uint32_t startPixel = protocol_detail::ReadLe32(payload + offset);
    offset += 4;
    const uint16_t pixelNumber = protocol_detail::ReadLe16(payload + offset);
    offset += 2;
    const uint32_t timestampSeconds = protocol_detail::ReadLe32(payload + offset);
    offset += 4;
    const uint32_t timestampNanoseconds = protocol_detail::ReadLe32(payload + offset);
    offset += 4;
    const uint16_t width = protocol_detail::ReadLe16(payload + offset);
    offset += 2;
    const uint16_t height = protocol_detail::ReadLe16(payload + offset);
    offset += 2;
    const uint16_t frameRate = protocol_detail::ReadLe16(payload + offset);
    offset += 2;
    const uint8_t protocolVersion = payload[offset++];

    (void)checkSum;
    (void)seqNum;
    (void)frameRate;
    (void)protocolVersion;

    if (startPixel != 0) {
        internal::SetError(error, "UDP partial frames are not supported");
        return false;
    }
    if (pixelNumber != kDepthPointCount) {
        internal::SetError(error, "UDP pixelNumber must equal 1200");
        return false;
    }
    if (width != kDepthWidth || height != kDepthHeight) {
        internal::SetError(error, "UDP width/height do not match the 40x30 protocol frame size");
        return false;
    }

    std::array<float, kCalibrationParameterCount> parameters {};
    for (size_t index = 0; index < parameters.size(); ++index) {
        parameters[index] = protocol_detail::ReadLeFloat(payload + offset);
        offset += 4;
    }
    measurement->calibration = internal::CalibrationFromRaw(parameters);

    measurement->depth.width = kDepthWidth;
    measurement->depth.height = kDepthHeight;
    measurement->depth.data.resize(kDepthPointCount);

    measurement->confidence.valid = true;
    measurement->confidence.width = kDepthWidth;
    measurement->confidence.height = kDepthHeight;
    measurement->confidence.bitDepth = 8;
    measurement->confidence.values.resize(kDepthPointCount);

    for (size_t index = 0; index < kDepthPointCount; ++index) {
        measurement->depth.data[index] = protocol_detail::ReadLe16(payload + offset);
        measurement->confidence.values[index] = payload[offset + 2];
        offset += kUdpPixelStride;
    }

    measurement->clock.device.valid = true;
    measurement->clock.device.value =
        static_cast<uint64_t>(timestampSeconds) * 1000000ull + static_cast<uint64_t>(timestampNanoseconds) / 1000ull;
    measurement->clock.device.unit = TimestampUnit::Microseconds;
    measurement->clock.device.raw0 = timestampSeconds;
    measurement->clock.device.raw1 = timestampNanoseconds;
    return true;
}

bool ParseUdpDataFrame(const std::vector<uint8_t>& payload, internal::Measurement* measurement, std::string* error) {
    return ParseUdpDataFrame(payload.data(), payload.size(), measurement, error);
}

}  // namespace hm_ld1
