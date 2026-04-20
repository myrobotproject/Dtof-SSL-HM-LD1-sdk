#pragma once

#include <memory>
#include <string>

#include "internal/frame_event.hpp"

namespace hm_ld1 {

class PacketSource {
public:
    virtual ~PacketSource() = default;

    virtual bool Open(const CameraConfig& config, std::string* error) = 0;
    virtual bool Poll(internal::SourceEvent* event, std::string* error) = 0;
    virtual void Close() = 0;

    virtual CameraStats Stats() const = 0;
    virtual std::string Describe(const CameraConfig& config) const = 0;
};

std::unique_ptr<PacketSource> CreatePacketSource(const CameraConfig& config);

}  // namespace hm_ld1


