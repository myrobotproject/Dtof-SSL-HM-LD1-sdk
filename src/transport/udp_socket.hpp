#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hm_ld1 {

class UdpSocket {
public:
    ~UdpSocket();

    bool Open(const std::string& bindAddress, int port, const std::string& interfaceName, std::string* error);
    int Receive(uint8_t* buffer, size_t capacity, std::string* error);
    void Close();

private:
#ifdef _WIN32
    void* socket_ = reinterpret_cast<void*>(-1);
#else
    int fd_ = -1;
#endif
};

}  // namespace hm_ld1

