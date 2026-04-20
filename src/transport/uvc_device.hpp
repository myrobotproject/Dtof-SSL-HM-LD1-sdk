#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hm_ld1 {

class UvcDevice {
public:
    UvcDevice() = default;
    ~UvcDevice();

    bool Open(const std::string& devicePath, uint32_t width, uint32_t height, std::string* error);
    bool ReadFrame(std::vector<uint8_t>* payload, std::string* error);
    void Close();

    uint32_t activeWidth() const;
    uint32_t activeHeight() const;
    const std::string& devicePath() const;

private:
    struct Buffer {
        void* start = nullptr;
        size_t length = 0;
    };

    int fd_ = -1;
    uint32_t activeWidth_ = 0;
    uint32_t activeHeight_ = 0;
    std::string devicePath_;
    std::vector<Buffer> buffers_;
};

}  // namespace hm_ld1

