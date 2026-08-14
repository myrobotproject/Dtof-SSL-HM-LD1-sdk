#include "transport/packet_source_factory.hpp"

#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "internal/error_utils.hpp"
#include "internal/timestamp_normalizer.hpp"
#include "protocol/udp_protocol.hpp"
#include "transport/udp_network.hpp"
#include "transport/udp_socket.hpp"

namespace hm_ld1 {
namespace {

constexpr uint64_t kMaxTrustedEpochSkewUs = 5ull * 1000000ull;

bool PrepareUdpInterface(
    const CameraConfig& config,
    InterfaceIpv4Info* interfaceInfo,
    std::string* interfaceSummary,
    std::string* error) {
    if (!GetInterfaceIpv4Info(config.udp.interfaceName, interfaceInfo, error)) {
        return false;
    }

    *interfaceSummary = "ipv4=" + FormatInterfaceIpv4(*interfaceInfo);
    if (interfaceInfo->hasIpv4) {
        return true;
    }
    if (!config.udp.autoConfig) {
        internal::SetError(
            error,
            "Interface " + config.udp.interfaceName +
                " has no IPv4 address. Configure it manually or retry with --udp-auto-config.");
        return false;
    }

    UdpAutoConfigProbeResult probeResult;
    if (!ProbeSensorTargetIpv4(
            config.udp.interfaceName,
            config.udp.port,
            config.udp.autoConfigTimeoutMs,
            &probeResult,
            error)) {
        return false;
    }
    if (!ConfigureInterfaceIpv4(
            config.udp.interfaceName,
            probeResult.targetIpv4,
            probeResult.inferredPrefixLength,
            error)) {
        return false;
    }
    if (!GetInterfaceIpv4Info(config.udp.interfaceName, interfaceInfo, error)) {
        return false;
    }

    *interfaceSummary = "ipv4=" + FormatInterfaceIpv4(*interfaceInfo) +
        ", auto-configured from " + probeResult.evidence +
        " target " + probeResult.targetIpv4 +
        ", camera=" + probeResult.senderIpv4;
    return true;
}

class UdpPacketSource final : public PacketSource {
public:
    bool Open(const CameraConfig& config, std::string* error) override {
        internal::ClearError(error);
        relativeTimestampNormalizer_.Reset();
        lastTimestampUs_ = 0;
        timestampMode_ = UdpTimestampMode::Unknown;
        if (!config.udp.interfaceName.empty() &&
            !PrepareUdpInterface(config, &interfaceInfo_, &interfaceSummary_, error)) {
            return false;
        }
        return socket_.Open(config.udp.bindAddress, config.udp.port, config.udp.interfaceName, error);
    }

    bool Poll(internal::SourceEvent* event, std::string* error) override {
        event->type = internal::SourceEventType::None;
        internal::ClearError(error);

        const int bytesRead = socket_.Receive(readBuffer_.data(), readBuffer_.size(), error);
        if (bytesRead < 0) {
            return false;
        }
        if (bytesRead == 0) {
            return true;
        }

        std::string parseError;
        if (!ParseUdpDataFrame(readBuffer_.data(), static_cast<size_t>(bytesRead), &event->measurement, &parseError)) {
            ++parseFailureCount_;
            lastError_ = std::move(parseError);
            event->type = internal::SourceEventType::None;
            return true;
        }

        NormalizeTimestamp(&event->measurement);

        ++okPacketCount_;
        lastError_.clear();
        event->type = internal::SourceEventType::Measurement;
        event->integrityName = "udp";
        return true;
    }

    void Close() override {
        socket_.Close();
        relativeTimestampNormalizer_.Reset();
        lastTimestampUs_ = 0;
        timestampMode_ = UdpTimestampMode::Unknown;
    }

    CameraStats Stats() const override {
        CameraStats stats;
        stats.okPackets = okPacketCount_;
        stats.parseFailures = parseFailureCount_;
        stats.lastError = lastError_;
        return stats;
    }

    std::string Describe(const CameraConfig& config) const override {
        std::ostringstream stream;
        stream << "udp " << config.udp.bindAddress << ":" << config.udp.port;
        if (!config.udp.interfaceName.empty()) {
            stream << " via " << config.udp.interfaceName;
            if (!interfaceSummary_.empty()) {
                stream << " [" << interfaceSummary_ << "]";
            }
        }
        return stream.str();
    }

private:
    void NormalizeTimestamp(internal::Measurement* measurement) {
        if (measurement == nullptr || !measurement->clock.device.valid) {
            return;
        }
        const uint32_t seconds = measurement->clock.device.raw0;
        const uint32_t nanoseconds = measurement->clock.device.raw1;
        const uint32_t safeNanoseconds = nanoseconds < 1000000000u ? nanoseconds : 0u;
        uint64_t valueUs = 0;
        const auto epochTimestamp = internal::EpochSecondsNanosecondsToUs(seconds, nanoseconds);
        const bool nearHostEpoch = epochTimestamp.has_value() &&
            internal::IsNearHostEpoch(epochTimestamp.value(), internal::SystemTimeNowUs(), kMaxTrustedEpochSkewUs);
        if (timestampMode_ == UdpTimestampMode::Unknown) {
            timestampMode_ = nearHostEpoch ? UdpTimestampMode::Epoch : UdpTimestampMode::Relative;
        }
        // The mode is selected once per Open() from the first packet. Once an
        // epoch stream is selected, trust subsequent valid fields even when a
        // packet briefly falls outside the startup skew window; switching to
        // a different domain mid-stream would create a discontinuity.
        if (timestampMode_ == UdpTimestampMode::Epoch && epochTimestamp.has_value()) {
            valueUs = epochTimestamp.value();
        } else if (timestampMode_ == UdpTimestampMode::Relative && nanoseconds < 1000000000u) {
            valueUs = relativeTimestampNormalizer_.Normalize(
                seconds,
                safeNanoseconds,
                internal::SystemTimeNowUs());
        } else {
            valueUs = lastTimestampUs_ == 0 ? internal::SystemTimeNowUs() : lastTimestampUs_ + 1ull;
        }
        if (lastTimestampUs_ != 0 && valueUs <= lastTimestampUs_) {
            valueUs = lastTimestampUs_ + 1ull;
        }
        lastTimestampUs_ = valueUs;
        measurement->clock.device.value = valueUs;
        measurement->clock.device.unit = TimestampUnit::Microseconds;
    }

    UdpSocket socket_;
    std::array<uint8_t, 8192> readBuffer_ {};
    size_t okPacketCount_ = 0;
    size_t parseFailureCount_ = 0;
    std::string lastError_;
    InterfaceIpv4Info interfaceInfo_;
    std::string interfaceSummary_;
    internal::RelativeSecondNormalizer relativeTimestampNormalizer_;
    uint64_t lastTimestampUs_ = 0;
    enum class UdpTimestampMode {
        Unknown,
        Epoch,
        Relative,
    };
    UdpTimestampMode timestampMode_ = UdpTimestampMode::Unknown;
};

}  // namespace

std::unique_ptr<PacketSource> CreateUdpPacketSource() {
    return std::make_unique<UdpPacketSource>();
}

}  // namespace hm_ld1
