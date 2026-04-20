#include "transport/serial_port.hpp"

#include <cctype>
#include <string>

#include "internal/error_utils.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace hm_ld1 {
namespace {

constexpr int kReadTimeoutMs = 20;

#ifdef _WIN32
std::string NormalizeWindowsPortName(const std::string& portName) {
    std::string lower = portName;
    for (char& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lower.rfind("\\\\.\\", 0) == 0) {
        return portName;
    }
    if (lower.rfind("com", 0) == 0) {
        return "\\\\.\\" + portName;
    }
    return portName;
}
#else
speed_t ToPosixBaud(int baud, std::string* error) {
    switch (baud) {
        case 115200:
            return B115200;
        case 460800:
#ifdef B460800
            return B460800;
#else
            internal::SetError(error, "B460800 is not available on this platform");
            return 0;
#endif
        case 921600:
#ifdef B921600
            return B921600;
#else
            internal::SetError(error, "B921600 is not available on this platform");
            return 0;
#endif
        default:
            internal::SetError(error, "Unsupported POSIX baud rate: " + std::to_string(baud));
            return 0;
    }
}
#endif

}  // namespace

SerialPort::~SerialPort() {
    Close();
}

bool SerialPort::Open(const std::string& portName, int baud, std::string* error) {
    internal::ClearError(error);
    if (portName.empty()) {
        internal::SetError(error, "Serial port name must not be empty");
        return false;
    }
#ifdef _WIN32
    const std::string normalizedName = NormalizeWindowsPortName(portName);
    handle_ = CreateFileA(
        normalizedName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        internal::SetError(error, "Failed to open serial port: " + portName);
        return false;
    }

    DCB dcb {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(static_cast<HANDLE>(handle_), &dcb)) {
        internal::SetError(error, "GetCommState failed");
        Close();
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!SetCommState(static_cast<HANDLE>(handle_), &dcb)) {
        internal::SetError(error, "SetCommState failed");
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts {};
    timeouts.ReadIntervalTimeout = kReadTimeoutMs;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = kReadTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    if (!SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts)) {
        internal::SetError(error, "SetCommTimeouts failed");
        Close();
        return false;
    }

    SetupComm(static_cast<HANDLE>(handle_), 64 * 1024, 64 * 1024);
    PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
#else
    fd_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        internal::SetError(error, "Failed to open serial port: " + portName + ", errno=" + std::to_string(errno));
        return false;
    }

    termios tty {};
    if (tcgetattr(fd_, &tty) != 0) {
        internal::SetError(error, "tcgetattr failed, errno=" + std::to_string(errno));
        Close();
        return false;
    }

    cfmakeraw(&tty);
    tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tty.c_cflag |= CS8;
#ifdef CRTSCTS
    tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    const speed_t speed = ToPosixBaud(baud, error);
    if (speed == 0) {
        Close();
        return false;
    }

    if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) {
        internal::SetError(error, "Failed to set baud rate");
        Close();
        return false;
    }

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        internal::SetError(error, "tcsetattr failed, errno=" + std::to_string(errno));
        Close();
        return false;
    }

    tcflush(fd_, TCIFLUSH);
    return true;
#endif
}

int SerialPort::ReadSome(uint8_t* buffer, size_t capacity, std::string* error) {
    internal::ClearError(error);
#ifdef _WIN32
    DWORD bytesRead = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(capacity), &bytesRead, nullptr)) {
        internal::SetError(error, "ReadFile failed");
        return -1;
    }
    return static_cast<int>(bytesRead);
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
    const ssize_t bytesRead = read(fd_, buffer, capacity);
    if (bytesRead < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        internal::SetError(error, "read failed, errno=" + std::to_string(errno));
        return -1;
    }
    return static_cast<int>(bytesRead);
#endif
}

void SerialPort::Close() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

}  // namespace hm_ld1


