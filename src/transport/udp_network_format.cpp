#include "transport/udp_network.hpp"

namespace hm_ld1 {

std::string FormatInterfaceIpv4(const InterfaceIpv4Info& info) {
    if (!info.exists) {
        return "missing";
    }
    if (!info.hasIpv4) {
        return "no-ipv4";
    }
    return info.address + "/" + std::to_string(info.prefixLength);
}

}  // namespace hm_ld1
