#include "protocol/serial_protocol.hpp"

#include "protocol/byte_utils.hpp"
#include "protocol/text_utils.hpp"

namespace hm_ld1 {

bool ParseSerialInfoPacket(const std::vector<uint8_t>& payload, DeviceInfo* info) {
    constexpr size_t kInfoPayloadSize = 1 + 16 + 16 + 16 + 16 + 4 + 1;
    if (info == nullptr || payload.size() < kInfoPayloadSize) {
        return false;
    }

    *info = DeviceInfo();
    size_t offset = 0;
    info->protocolVersion = payload[offset++];
    info->socVersion = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    info->socSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    info->sensorVersion = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    info->sensorSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    info->sensorTemperatureCelsius = protocol_detail::ReadLeFloat(payload.data() + offset);
    offset += 4;
    info->stateCode = payload[offset];
    return true;
}

bool ParseSerialDataFrame(const std::vector<uint8_t>& payload, internal::Measurement* measurement) {
    constexpr size_t kMinimumPayloadSize = 1 + 4 + (kCalibrationParameterCount * 4) + (kDepthPointCount * 2);
    if (measurement == nullptr || payload.size() < kMinimumPayloadSize) {
        return false;
    }

    *measurement = internal::Measurement();
    measurement->transportType = TransportType::Serial;

    size_t offset = 0;
    const uint8_t protocolVersion = payload[offset++];
    const uint32_t timestampUs = protocol_detail::ReadLe32(payload.data() + offset);
    offset += 4;

    std::array<float, kCalibrationParameterCount> parameters {};
    for (size_t index = 0; index < parameters.size(); ++index) {
        parameters[index] = protocol_detail::ReadLeFloat(payload.data() + offset);
        offset += 4;
    }
    measurement->calibration = internal::CalibrationFromRaw(parameters);

    measurement->depth.width = kDepthWidth;
    measurement->depth.height = kDepthHeight;
    measurement->depth.data.resize(kDepthPointCount);
    for (size_t index = 0; index < kDepthPointCount; ++index) {
        measurement->depth.data[index] = protocol_detail::ReadLe16(payload.data() + offset);
        offset += 2;
    }

    measurement->clock.device.valid = true;
    measurement->clock.device.value = timestampUs;
    measurement->clock.device.unit = TimestampUnit::Microseconds;
    measurement->clock.device.raw0 = timestampUs;
    measurement->clock.device.raw1 = 0;

    (void)protocolVersion;
    return true;
}

}  // namespace hm_ld1
