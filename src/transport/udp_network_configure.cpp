#include "transport/udp_network.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#include "internal/error_utils.hpp"
#include "transport/udp_network_utils.hpp"

namespace hm_ld1 {

bool GetInterfaceIpv4Info(const std::string& interfaceName, InterfaceIpv4Info* info, std::string* error) {
    if (info == nullptr) {
        internal::SetError(error, "Interface IPv4 output pointer is null");
        return false;
    }
    internal::ClearError(error);
#ifdef _WIN32
    (void)interfaceName;
    *info = InterfaceIpv4Info {};
    internal::SetError(error, "UDP interface inspection is not implemented on Windows");
    return false;
#else
    *info = InterfaceIpv4Info {};
    if (if_nametoindex(interfaceName.c_str()) == 0) {
        internal::SetError(error, "Unknown network interface: " + interfaceName);
        return false;
    }

    info->exists = true;

    ifaddrs* addressList = nullptr;
    if (getifaddrs(&addressList) != 0) {
        internal::SetError(error, "getifaddrs failed, errno=" + std::to_string(errno));
        return false;
    }

    for (ifaddrs* item = addressList; item != nullptr; item = item->ifa_next) {
        if (item->ifa_name == nullptr || interfaceName != item->ifa_name || item->ifa_addr == nullptr) {
            continue;
        }
        if (item->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        info->hasIpv4 = true;
        info->address = udp_network_detail::FormatIpv4Address(
            reinterpret_cast<sockaddr_in*>(item->ifa_addr)->sin_addr);
        info->prefixLength = udp_network_detail::PrefixLengthFromNetmask(item->ifa_netmask);
        break;
    }

    freeifaddrs(addressList);
    return true;
#endif
}

bool ConfigureInterfaceIpv4(
    const std::string& interfaceName,
    const std::string& address,
    int prefixLength,
    std::string* error) {
    internal::ClearError(error);
#ifdef _WIN32
    (void)interfaceName;
    (void)address;
    (void)prefixLength;
    internal::SetError(error, "UDP interface configuration is not implemented on Windows");
    return false;
#else
    if (prefixLength < 0 || prefixLength > 32) {
        internal::SetError(error, "IPv4 prefix length must be in the range [0, 32]");
        return false;
    }

    udp_network_detail::ScopedFd fd;
    fd.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd.fd < 0) {
        internal::SetError(error, "socket(AF_INET) failed, errno=" + std::to_string(errno));
        return false;
    }

    ifreq addressRequest {};
    if (!udp_network_detail::CopyInterfaceName(interfaceName, &addressRequest, error)) {
        return false;
    }

    sockaddr_in ipv4Address {};
    ipv4Address.sin_family = AF_INET;
    if (inet_pton(AF_INET, address.c_str(), &ipv4Address.sin_addr) != 1) {
        internal::SetError(error, "Invalid IPv4 address: " + address);
        return false;
    }
    std::memcpy(&addressRequest.ifr_addr, &ipv4Address, sizeof(ipv4Address));
    if (ioctl(fd.fd, SIOCSIFADDR, &addressRequest) != 0) {
        if (errno == EPERM || errno == EACCES) {
            internal::SetError(
                error,
                "Configuring interface IPv4 requires root or CAP_NET_ADMIN on " + interfaceName);
        } else {
            internal::SetError(error, "SIOCSIFADDR failed, errno=" + std::to_string(errno));
        }
        return false;
    }

    ifreq netmaskRequest {};
    if (!udp_network_detail::CopyInterfaceName(interfaceName, &netmaskRequest, error)) {
        return false;
    }

    sockaddr_in netmaskAddress {};
    netmaskAddress.sin_family = AF_INET;
    netmaskAddress.sin_addr.s_addr = htonl(udp_network_detail::PrefixLengthToMaskBits(prefixLength));
    std::memcpy(&netmaskRequest.ifr_netmask, &netmaskAddress, sizeof(netmaskAddress));
    if (ioctl(fd.fd, SIOCSIFNETMASK, &netmaskRequest) != 0) {
        internal::SetError(error, "SIOCSIFNETMASK failed, errno=" + std::to_string(errno));
        return false;
    }

    ifreq flagsRequest {};
    if (!udp_network_detail::CopyInterfaceName(interfaceName, &flagsRequest, error)) {
        return false;
    }
    if (ioctl(fd.fd, SIOCGIFFLAGS, &flagsRequest) != 0) {
        internal::SetError(error, "SIOCGIFFLAGS failed, errno=" + std::to_string(errno));
        return false;
    }
    flagsRequest.ifr_flags = static_cast<short>(flagsRequest.ifr_flags | IFF_UP);
    if (ioctl(fd.fd, SIOCSIFFLAGS, &flagsRequest) != 0) {
        internal::SetError(error, "SIOCSIFFLAGS failed, errno=" + std::to_string(errno));
        return false;
    }

    return true;
#endif
}

}  // namespace hm_ld1
