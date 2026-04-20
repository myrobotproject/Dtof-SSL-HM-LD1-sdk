#include "transport/packet_source.hpp"

#include "transport/packet_source_factory.hpp"

namespace hm_ld1 {

std::unique_ptr<PacketSource> CreatePacketSource(const CameraConfig& config) {
    if (config.transportType == TransportType::Udp) {
        return CreateUdpPacketSource();
    }
    if (config.transportType == TransportType::Uvc) {
        return CreateUvcPacketSource(config);
    }
    return CreateSerialPacketSource(config.serial.crcMode);
}

}  // namespace hm_ld1
