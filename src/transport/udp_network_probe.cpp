#include "transport/udp_network.hpp"

#ifndef _WIN32
#include <algorithm>
#include <array>

#include <arpa/inet.h>
#include <errno.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/ip.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <sys/socket.h>
#endif

#include "internal/error_utils.hpp"
#include "transport/udp_network_utils.hpp"

namespace hm_ld1 {

bool ProbeSensorTargetIpv4(
    const std::string& interfaceName,
    int expectedUdpPort,
    int timeoutMs,
    UdpAutoConfigProbeResult* result,
    std::string* error) {
    if (result == nullptr) {
        internal::SetError(error, "UDP auto-configuration result pointer is null");
        return false;
    }
    *result = UdpAutoConfigProbeResult {};
    internal::ClearError(error);
#ifdef _WIN32
    (void)interfaceName;
    (void)expectedUdpPort;
    (void)timeoutMs;
    internal::SetError(error, "UDP auto-detection is not implemented on Windows");
    return false;
#else
    const unsigned int interfaceIndex = if_nametoindex(interfaceName.c_str());
    if (interfaceIndex == 0) {
        internal::SetError(error, "Unknown network interface: " + interfaceName);
        return false;
    }

    udp_network_detail::ScopedFd fd;
    fd.fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd.fd < 0) {
        if (errno == EPERM || errno == EACCES) {
            internal::SetError(
                error,
                "UDP auto-detection requires root or CAP_NET_RAW on interface " + interfaceName);
        } else {
            internal::SetError(error, "raw socket failed, errno=" + std::to_string(errno));
        }
        return false;
    }

    sockaddr_ll bindAddress {};
    bindAddress.sll_family = AF_PACKET;
    bindAddress.sll_protocol = htons(ETH_P_ALL);
    bindAddress.sll_ifindex = static_cast<int>(interfaceIndex);
    if (bind(fd.fd, reinterpret_cast<const sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0) {
        internal::SetError(error, "raw bind failed, errno=" + std::to_string(errno));
        return false;
    }

    std::array<uint8_t, 2048> buffer {};
    int remainingTimeoutMs = timeoutMs;
    while (remainingTimeoutMs > 0) {
        pollfd pollDescriptor {};
        pollDescriptor.fd = fd.fd;
        pollDescriptor.events = POLLIN;
        const int waitMs = std::min(remainingTimeoutMs, 250);
        const int pollResult = poll(&pollDescriptor, 1, waitMs);
        if (pollResult < 0) {
            internal::SetError(error, "raw poll failed, errno=" + std::to_string(errno));
            return false;
        }
        remainingTimeoutMs -= waitMs;
        if (pollResult == 0) {
            continue;
        }

        const ssize_t bytesRead = recv(fd.fd, buffer.data(), buffer.size(), 0);
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            internal::SetError(error, "raw recv failed, errno=" + std::to_string(errno));
            return false;
        }
        if (bytesRead < 14) {
            continue;
        }

        const uint16_t etherType = static_cast<uint16_t>((buffer[12] << 8u) | buffer[13]);
        if (etherType == ETH_P_ARP) {
            if (bytesRead < 42) {
                continue;
            }
            const uint8_t* arp = buffer.data() + 14;
            const uint16_t hardwareType = static_cast<uint16_t>((arp[0] << 8u) | arp[1]);
            const uint16_t protocolType = static_cast<uint16_t>((arp[2] << 8u) | arp[3]);
            const uint8_t hardwareLength = arp[4];
            const uint8_t protocolLength = arp[5];
            const uint16_t operation = static_cast<uint16_t>((arp[6] << 8u) | arp[7]);
            if (hardwareType != ARPHRD_ETHER || protocolType != ETH_P_IP ||
                hardwareLength != 6 || protocolLength != 4 || operation != ARPOP_REQUEST) {
                continue;
            }

            const uint8_t* senderIpv4 = arp + 14;
            const uint8_t* targetIpv4 = arp + 24;
            const std::string senderText = udp_network_detail::FormatIpv4Bytes(senderIpv4);
            const std::string targetText = udp_network_detail::FormatIpv4Bytes(targetIpv4);
            if (senderText.empty() || targetText.empty() || senderText == "0.0.0.0" || targetText == "0.0.0.0") {
                continue;
            }

            result->senderIpv4 = senderText;
            result->targetIpv4 = targetText;
            result->inferredPrefixLength = udp_network_detail::InferPrefixLength(senderIpv4, targetIpv4);
            result->evidence = "arp";
            return true;
        }

        if (etherType == ETH_P_IP) {
            if (bytesRead < 14 + static_cast<ssize_t>(sizeof(iphdr)) + 8) {
                continue;
            }
            const uint8_t* ipHeader = buffer.data() + 14;
            const int version = (ipHeader[0] >> 4u) & 0x0Fu;
            const int headerLength = (ipHeader[0] & 0x0Fu) * 4;
            if (version != 4 || headerLength < 20 || bytesRead < 14 + headerLength + 8) {
                continue;
            }
            if (ipHeader[9] != IPPROTO_UDP) {
                continue;
            }
            const uint8_t* udpHeader = ipHeader + headerLength;
            const uint16_t destinationPort = static_cast<uint16_t>((udpHeader[2] << 8u) | udpHeader[3]);
            if (expectedUdpPort > 0 && destinationPort != static_cast<uint16_t>(expectedUdpPort)) {
                continue;
            }

            const uint8_t* senderIpv4 = ipHeader + 12;
            const uint8_t* targetIpv4 = ipHeader + 16;
            const std::string senderText = udp_network_detail::FormatIpv4Bytes(senderIpv4);
            const std::string targetText = udp_network_detail::FormatIpv4Bytes(targetIpv4);
            if (senderText.empty() || targetText.empty() || senderText == "0.0.0.0" || targetText == "0.0.0.0") {
                continue;
            }

            result->senderIpv4 = senderText;
            result->targetIpv4 = targetText;
            result->inferredPrefixLength = udp_network_detail::InferPrefixLength(senderIpv4, targetIpv4);
            result->evidence = "udp";
            return true;
        }
    }

    internal::SetError(error, "Timed out waiting for UDP or ARP traffic on interface " + interfaceName);
    return false;
#endif
}

}  // namespace hm_ld1
