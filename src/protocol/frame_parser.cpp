#include "protocol/frame_parser.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "protocol/byte_utils.hpp"

namespace hm_ld1 {
namespace {

constexpr std::array<uint8_t, 2> kDeviceSync = {0xA5, 0x5A};

struct Crc8Profile {
    const char* name;
    uint8_t poly;
    uint8_t init;
    uint8_t xorOut;
    bool reflectIn;
    bool reflectOut;
};

const std::array<Crc8Profile, 4> kCrcProfiles = {{
    {"crc8", 0x07, 0x00, 0x00, false, false},
    {"crc8_itu", 0x07, 0x00, 0x55, false, false},
    {"maxim", 0x31, 0x00, 0x00, true, true},
    {"rohc", 0x07, 0xFF, 0x00, true, true},
}};

uint8_t Reflect8(uint8_t value) {
    value = static_cast<uint8_t>(((value & 0xF0u) >> 4u) | ((value & 0x0Fu) << 4u));
    value = static_cast<uint8_t>(((value & 0xCCu) >> 2u) | ((value & 0x33u) << 2u));
    value = static_cast<uint8_t>(((value & 0xAAu) >> 1u) | ((value & 0x55u) << 1u));
    return value;
}

uint8_t ComputeCrc8(const uint8_t* data, size_t size, const Crc8Profile& profile) {
    uint8_t crc = profile.init;
    for (size_t index = 0; index < size; ++index) {
        uint8_t value = data[index];
        if (profile.reflectIn) {
            value = Reflect8(value);
        }
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x80u) != 0u) {
                crc = static_cast<uint8_t>((crc << 1u) ^ profile.poly);
            } else {
                crc = static_cast<uint8_t>(crc << 1u);
            }
        }
    }
    if (profile.reflectOut) {
        crc = Reflect8(crc);
    }
    return static_cast<uint8_t>(crc ^ profile.xorOut);
}

}  // namespace

FrameParser::FrameParser(std::string crcMode) : crcMode_(std::move(crcMode)) {}

void FrameParser::Append(const uint8_t* data, size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
}

bool FrameParser::TryPop(ProtocolFrame* frame) {
    for (;;) {
        if (buffer_.size() < 6) {
            return false;
        }

        const auto syncPos = std::search(buffer_.begin(), buffer_.end(), kDeviceSync.begin(), kDeviceSync.end());
        if (syncPos == buffer_.end()) {
            const bool keepLast = !buffer_.empty() && buffer_.back() == kDeviceSync[0];
            const size_t removeCount = keepLast ? buffer_.size() - 1 : buffer_.size();
            discardedBytes_ += removeCount;
            if (keepLast) {
                const uint8_t last = buffer_.back();
                buffer_.assign(1, last);
            } else {
                buffer_.clear();
            }
            return false;
        }

        if (syncPos != buffer_.begin()) {
            discardedBytes_ += static_cast<size_t>(std::distance(buffer_.begin(), syncPos));
            buffer_.erase(buffer_.begin(), syncPos);
            if (buffer_.size() < 6) {
                return false;
            }
        }

        const uint16_t length = protocol_detail::ReadLe16(buffer_.data() + 2);
        if (length < 2 || length > 16 * 1024) {
            ++badLengthCount_;
            buffer_.erase(buffer_.begin());
            continue;
        }

        const size_t totalSize = 4 + static_cast<size_t>(length);
        if (buffer_.size() < totalSize) {
            return false;
        }

        const uint8_t* crcInput = buffer_.data() + 4;
        const size_t crcInputSize = static_cast<size_t>(length) - 1;
        const uint8_t receivedCrc = buffer_[totalSize - 1];
        if (!ValidateCrc(crcInput, crcInputSize, receivedCrc)) {
            ++crcFailureCount_;
            buffer_.erase(buffer_.begin());
            continue;
        }

        frame->msgId = buffer_[4];
        const size_t msgDataSize = static_cast<size_t>(length) - 2;
        frame->msgData.assign(buffer_.begin() + 5, buffer_.begin() + 5 + static_cast<std::ptrdiff_t>(msgDataSize));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(totalSize));
        ++okFrameCount_;
        return true;
    }
}

size_t FrameParser::crcFailures() const {
    return crcFailureCount_;
}

size_t FrameParser::badLengths() const {
    return badLengthCount_;
}

size_t FrameParser::discardedBytes() const {
    return discardedBytes_;
}

size_t FrameParser::okFrames() const {
    return okFrameCount_;
}

const std::string& FrameParser::activeCrcName() const {
    return activeCrcName_;
}

bool FrameParser::ValidateCrc(const uint8_t* data, size_t size, uint8_t received) {
    if (crcMode_ == "none") {
        activeCrcName_ = "none";
        return true;
    }
    if (crcMode_ != "auto") {
        const int profileIndex = FindProfileIndex(crcMode_);
        if (profileIndex < 0) {
            return false;
        }
        const Crc8Profile& profile = kCrcProfiles[static_cast<size_t>(profileIndex)];
        activeCrcName_ = profile.name;
        return ComputeCrc8(data, size, profile) == received;
    }
    if (lockedProfileIndex_ >= 0) {
        const Crc8Profile& profile = kCrcProfiles[static_cast<size_t>(lockedProfileIndex_)];
        activeCrcName_ = profile.name;
        return ComputeCrc8(data, size, profile) == received;
    }
    for (size_t index = 0; index < kCrcProfiles.size(); ++index) {
        const Crc8Profile& profile = kCrcProfiles[index];
        if (ComputeCrc8(data, size, profile) == received) {
            lockedProfileIndex_ = static_cast<int>(index);
            activeCrcName_ = profile.name;
            return true;
        }
    }
    return false;
}

int FrameParser::FindProfileIndex(const std::string& name) {
    for (size_t index = 0; index < kCrcProfiles.size(); ++index) {
        if (name == kCrcProfiles[index].name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

}  // namespace hm_ld1


