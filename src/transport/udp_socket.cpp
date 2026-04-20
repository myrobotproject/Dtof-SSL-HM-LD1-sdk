#include "transport/udp_socket.hpp"

#include <string>

#include "internal/error_utils.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace hm_ld1 {
namespace {

constexpr int kReadTimeoutMs = 20;

}  // namespace

UdpSocket::~UdpSocket() {
    Close();
}

bool UdpSocket::Open(const std::string& bindAddress, int port, const std::string& interfaceName, std::string* error) {
    internal::ClearError(error);
    if (port <= 0 || port > 65535) {
        internal::SetError(error, "UDP port must be in the range [1, 65535]");
        return false;
    }
#ifdef _WIN32
    (void)bindAddress;
    (void)port;
    (void)interfaceName;
    internal::SetError(error, "UDP transport is not implemented on Windows");
    return false;
#else
    (void)interfaceName;
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        internal::SetError(error, "socket failed, errno=" + std::to_string(errno));
        return false;
    }

    int reuse = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
        internal::SetError(error, "Invalid UDP bind address: " + bindAddress);
        Close();
        return false;
    }

    if (bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        internal::SetError(error, "bind failed, errno=" + std::to_string(errno));
        Close();
        return false;
    }

    return true;
#endif
}

int UdpSocket::Receive(uint8_t* buffer, size_t capacity, std::string* error) {
    internal::ClearError(error);
#ifdef _WIN32
    (void)buffer;
    (void)capacity;
    internal::SetError(error, "UDP transport is not implemented on Windows");
    return -1;
#else
    pollfd fdSet {};
    fdSet.fd = fd_;
    fdSet.events = POLLIN;
    const int pollResult = poll(&fdSet, 1, kReadTimeoutMs);
    if (pollResult < 0) {
        internal::SetError(error, "poll failed, errno=" + std::to_string(errno));
        return -1;
    }
    if (pollResult == 0) {
        return 0;
    }

    const ssize_t bytesRead = recv(fd_, buffer, capacity, 0);
    if (bytesRead < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        internal::SetError(error, "recv failed, errno=" + std::to_string(errno));
        return -1;
    }
    return static_cast<int>(bytesRead);
#endif
}

void UdpSocket::Close() {
#ifdef _WIN32
    socket_ = reinterpret_cast<void*>(-1);
#else
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

}  // namespace hm_ld1


