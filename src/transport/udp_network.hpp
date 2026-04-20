#pragma once

#include <string>

namespace hm_ld1 {

struct InterfaceIpv4Info {
    bool exists = false;
    bool hasIpv4 = false;
    std::string address;
    int prefixLength = 0;
};

struct UdpAutoConfigProbeResult {
    std::string senderIpv4;
    std::string targetIpv4;
    int inferredPrefixLength = 24;
    std::string evidence;
};

bool GetInterfaceIpv4Info(const std::string& interfaceName, InterfaceIpv4Info* info, std::string* error);
bool ProbeSensorTargetIpv4(const std::string& interfaceName, int expectedUdpPort, int timeoutMs, UdpAutoConfigProbeResult* result, std::string* error);
bool ConfigureInterfaceIpv4(const std::string& interfaceName, const std::string& address, int prefixLength, std::string* error);
std::string FormatInterfaceIpv4(const InterfaceIpv4Info& info);

}  // namespace hm_ld1

