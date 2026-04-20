#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "internal/error_utils.hpp"

namespace hm_ld1 {
namespace udp_network_detail {

#ifndef _WIN32
struct ScopedFd {
    int fd = -1;

    ~ScopedFd() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

inline std::string FormatIpv4Bytes(const uint8_t* bytes) {
    char text[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, bytes, text, sizeof(text)) == nullptr) {
        return {};
    }
    return text;
}

inline std::string FormatIpv4Address(const in_addr& address) {
    char text[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address, text, sizeof(text)) == nullptr) {
        return {};
    }
    return text;
}

inline int PrefixLengthFromNetmask(const sockaddr* netmask) {
    if (netmask == nullptr || netmask->sa_family != AF_INET) {
        return 0;
    }
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(netmask);
    uint32_t value = ntohl(ipv4->sin_addr.s_addr);
    int prefixLength = 0;
    while ((value & 0x80000000u) != 0u) {
        ++prefixLength;
        value <<= 1u;
    }
    return prefixLength;
}

inline uint32_t PrefixLengthToMaskBits(int prefixLength) {
    if (prefixLength <= 0) {
        return 0u;
    }
    if (prefixLength >= 32) {
        return 0xFFFFFFFFu;
    }
    return 0xFFFFFFFFu << (32 - prefixLength);
}

inline int InferPrefixLength(const uint8_t* senderIpv4, const uint8_t* targetIpv4) {
    int commonBits = 0;
    for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
        const uint8_t xorValue = static_cast<uint8_t>(senderIpv4[byteIndex] ^ targetIpv4[byteIndex]);
        if (xorValue == 0) {
            commonBits += 8;
            continue;
        }
        for (int bit = 7; bit >= 0; --bit) {
            if (((xorValue >> bit) & 0x1u) != 0u) {
                const int rounded = (commonBits / 8) * 8;
                return std::max(8, rounded);
            }
            ++commonBits;
        }
    }
    if (commonBits >= 24) {
        return 24;
    }
    if (commonBits >= 16) {
        return 16;
    }
    if (commonBits >= 8) {
        return 8;
    }
    return 24;
}

inline bool CopyInterfaceName(const std::string& interfaceName, ifreq* request, std::string* error) {
    if (interfaceName.empty()) {
        internal::SetError(error, "Interface name must not be empty");
        return false;
    }
    if (interfaceName.size() >= IFNAMSIZ) {
        internal::SetError(error, "Interface name is too long: " + interfaceName);
        return false;
    }
    std::memset(request, 0, sizeof(*request));
    std::strncpy(request->ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);
    return true;
}
#endif

}  // namespace udp_network_detail
}  // namespace hm_ld1
