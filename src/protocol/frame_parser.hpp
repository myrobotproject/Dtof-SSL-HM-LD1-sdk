#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hm_ld1 {

struct ProtocolFrame {
    uint8_t msgId = 0;
    std::vector<uint8_t> msgData;
};

class FrameParser {
public:
    explicit FrameParser(std::string crcMode);

    void Append(const uint8_t* data, size_t size);
    bool TryPop(ProtocolFrame* frame);

    size_t crcFailures() const;
    size_t badLengths() const;
    size_t discardedBytes() const;
    size_t okFrames() const;
    const std::string& activeCrcName() const;

private:
    bool ValidateCrc(const uint8_t* data, size_t size, uint8_t received);
    static int FindProfileIndex(const std::string& name);

    std::string crcMode_;
    std::vector<uint8_t> buffer_;
    int lockedProfileIndex_ = -1;
    std::string activeCrcName_ = "unknown";
    size_t crcFailureCount_ = 0;
    size_t badLengthCount_ = 0;
    size_t discardedBytes_ = 0;
    size_t okFrameCount_ = 0;
};

}  // namespace hm_ld1

