#include "transport/uvc_device.hpp"

#include <algorithm>
#include <string>

#include "internal/error_utils.hpp"

#ifdef __linux__
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace hm_ld1 {
namespace {

#ifdef __linux__
bool RetryIoctl(int fd, unsigned long request, void* arg) {
    for (;;) {
        if (::ioctl(fd, request, arg) == 0) {
            return true;
        }
        if (errno != EINTR) {
            return false;
        }
    }
}

std::string ErrnoMessage(const std::string& prefix) {
    return prefix + ": " + std::strerror(errno);
}

uint32_t EffectiveCapabilities(const v4l2_capability& capabilities) {
    if ((capabilities.capabilities & V4L2_CAP_DEVICE_CAPS) != 0) {
        return capabilities.device_caps;
    }
    return capabilities.capabilities;
}
#endif

}  // namespace

UvcDevice::~UvcDevice() {
    Close();
}

bool UvcDevice::Open(const std::string& devicePath, uint32_t width, uint32_t height, std::string* error) {
    internal::ClearError(error);
    if (devicePath.empty()) {
        internal::SetError(error, "UVC device path must not be empty");
        return false;
    }
    if (width == 0 || height == 0) {
        internal::SetError(error, "UVC stream width and height must be non-zero");
        return false;
    }
#ifndef __linux__
    (void)devicePath;
    (void)width;
    (void)height;
    internal::SetError(error, "UVC transport is only supported on Linux");
    return false;
#else
    Close();

    fd_ = ::open(devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        internal::SetError(error, ErrnoMessage("Failed to open UVC device " + devicePath));
        return false;
    }

    v4l2_capability capabilities {};
    if (!RetryIoctl(fd_, VIDIOC_QUERYCAP, &capabilities)) {
        internal::SetError(error, ErrnoMessage("Failed to query UVC capabilities"));
        Close();
        return false;
    }

    const uint32_t effectiveCapabilities = EffectiveCapabilities(capabilities);
    if ((effectiveCapabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        internal::SetError(error, "UVC device does not expose video capture");
        Close();
        return false;
    }
    if ((effectiveCapabilities & V4L2_CAP_STREAMING) == 0) {
        internal::SetError(error, "UVC device does not support streaming I/O");
        Close();
        return false;
    }

    v4l2_format format {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    if (!RetryIoctl(fd_, VIDIOC_S_FMT, &format)) {
        internal::SetError(error, ErrnoMessage("Failed to configure the UVC stream format"));
        Close();
        return false;
    }
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        internal::SetError(error, "UVC device did not accept YUYV format");
        Close();
        return false;
    }
    if (format.fmt.pix.width != width || format.fmt.pix.height != height) {
        internal::SetError(error, "UVC device negotiated a different stream size");
        Close();
        return false;
    }

    v4l2_streamparm streamParameters {};
    streamParameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamParameters.parm.capture.timeperframe.numerator = 1;
    streamParameters.parm.capture.timeperframe.denominator = 30;
    RetryIoctl(fd_, VIDIOC_S_PARM, &streamParameters);

    v4l2_requestbuffers requestBuffers {};
    requestBuffers.count = 4;
    requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestBuffers.memory = V4L2_MEMORY_MMAP;
    if (!RetryIoctl(fd_, VIDIOC_REQBUFS, &requestBuffers)) {
        internal::SetError(error, ErrnoMessage("Failed to request UVC streaming buffers"));
        Close();
        return false;
    }
    if (requestBuffers.count == 0) {
        internal::SetError(error, "UVC device did not provide any streaming buffers");
        Close();
        return false;
    }

    buffers_.assign(requestBuffers.count, Buffer());
    for (uint32_t index = 0; index < requestBuffers.count; ++index) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (!RetryIoctl(fd_, VIDIOC_QUERYBUF, &buffer)) {
            internal::SetError(error, ErrnoMessage("Failed to query a UVC streaming buffer"));
            Close();
            return false;
        }

        void* mapped = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset);
        if (mapped == MAP_FAILED) {
            internal::SetError(error, ErrnoMessage("Failed to map a UVC streaming buffer"));
            Close();
            return false;
        }
        buffers_[index].start = mapped;
        buffers_[index].length = buffer.length;
    }

    for (uint32_t index = 0; index < static_cast<uint32_t>(buffers_.size()); ++index) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (!RetryIoctl(fd_, VIDIOC_QBUF, &buffer)) {
            internal::SetError(error, ErrnoMessage("Failed to enqueue a UVC streaming buffer"));
            Close();
            return false;
        }
    }

    int bufferType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!RetryIoctl(fd_, VIDIOC_STREAMON, &bufferType)) {
        internal::SetError(error, ErrnoMessage("Failed to start the UVC stream"));
        Close();
        return false;
    }

    activeWidth_ = format.fmt.pix.width;
    activeHeight_ = format.fmt.pix.height;
    devicePath_ = devicePath;
    return true;
#endif
}

bool UvcDevice::ReadFrame(std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        internal::SetError(error, "UVC payload output pointer is null");
        return false;
    }
    payload->clear();
    internal::ClearError(error);

#ifndef __linux__
    internal::SetError(error, "UVC transport is only supported on Linux");
    return false;
#else
    if (fd_ < 0) {
        internal::SetError(error, "UVC device is not open");
        return false;
    }

    pollfd pollDescriptor {};
    pollDescriptor.fd = fd_;
    pollDescriptor.events = POLLIN;
    int pollResult = 0;
    do {
        pollResult = ::poll(&pollDescriptor, 1, 50);
    } while (pollResult < 0 && errno == EINTR);

    if (pollResult < 0) {
        internal::SetError(error, ErrnoMessage("Failed while waiting for the next UVC frame"));
        return false;
    }
    if (pollResult == 0) {
        return true;
    }

    v4l2_buffer buffer {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (::ioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) {
            return true;
        }
        internal::SetError(error, ErrnoMessage("Failed to dequeue the next UVC frame"));
        return false;
    }
    if (buffer.index >= buffers_.size()) {
        internal::SetError(error, "UVC driver returned an invalid buffer index");
        return false;
    }

    const Buffer& mappedBuffer = buffers_[buffer.index];
    const size_t bytesUsed = std::min<size_t>(buffer.bytesused, mappedBuffer.length);
    const uint8_t* begin = static_cast<const uint8_t*>(mappedBuffer.start);
    payload->assign(begin, begin + bytesUsed);

    if (!RetryIoctl(fd_, VIDIOC_QBUF, &buffer)) {
        internal::SetError(error, ErrnoMessage("Failed to requeue a UVC frame buffer"));
        return false;
    }
    return true;
#endif
}

void UvcDevice::Close() {
#ifdef __linux__
    if (fd_ >= 0 && !buffers_.empty()) {
        int bufferType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(fd_, VIDIOC_STREAMOFF, &bufferType);
    }
    for (const Buffer& buffer : buffers_) {
        if (buffer.start != nullptr && buffer.length > 0) {
            ::munmap(buffer.start, buffer.length);
        }
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif

    buffers_.clear();
    fd_ = -1;
    activeWidth_ = 0;
    activeHeight_ = 0;
    devicePath_.clear();
}

uint32_t UvcDevice::activeWidth() const {
    return activeWidth_;
}

uint32_t UvcDevice::activeHeight() const {
    return activeHeight_;
}

const std::string& UvcDevice::devicePath() const {
    return devicePath_;
}

}  // namespace hm_ld1


