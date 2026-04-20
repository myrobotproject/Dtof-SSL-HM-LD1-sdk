#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hm_ld1 {

class SerialPort {
public:
    ~SerialPort();

    bool Open(const std::string& portName, int baud, std::string* error);
    int ReadSome(uint8_t* buffer, size_t capacity, std::string* error);
    void Close();

private:
#ifdef _WIN32
    void* handle_ = reinterpret_cast<void*>(-1);
#else
    int fd_ = -1;
#endif
};

}  // namespace hm_ld1

