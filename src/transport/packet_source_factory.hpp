#pragma once

#include <memory>
#include <string>

#include "transport/packet_source.hpp"

namespace hm_ld1 {

std::unique_ptr<PacketSource> CreateSerialPacketSource(std::string crcMode);
std::unique_ptr<PacketSource> CreateUdpPacketSource();
std::unique_ptr<PacketSource> CreateUvcPacketSource(CameraConfig config);

}  // namespace hm_ld1
