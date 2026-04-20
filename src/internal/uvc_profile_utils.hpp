#pragma once

#include <cstdint>

#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

namespace hm_ld1::internal {

inline UvcStreamProfile ResolveUvcProfile(UvcStreamProfile profile) {
    return profile == UvcStreamProfile::Auto ? UvcStreamProfile::Mixed120x90 : profile;
}

inline bool UvcProfileNeedsBootstrap(UvcStreamProfile profile) {
    const UvcStreamProfile resolved = ResolveUvcProfile(profile);
    return resolved == UvcStreamProfile::Depth40x30 || resolved == UvcStreamProfile::PointCloud160x120;
}

inline bool GetUvcStreamDimensions(UvcStreamProfile profile, uint32_t* width, uint32_t* height) {
    if (width == nullptr || height == nullptr) {
        return false;
    }

    const UvcStreamProfile resolved = ResolveUvcProfile(profile);
    switch (resolved) {
        case UvcStreamProfile::Depth40x30:
            *width = 40;
            *height = 30;
            return true;
        case UvcStreamProfile::Mixed120x90:
            *width = 120;
            *height = 90;
            return true;
        case UvcStreamProfile::PointCloud160x120:
            *width = 160;
            *height = 120;
            return true;
        case UvcStreamProfile::Raw480x360:
            *width = 480;
            *height = 360;
            return true;
        case UvcStreamProfile::Auto:
        default:
            return false;
    }
}

inline const char* UvcProfileName(UvcStreamProfile profile) {
    switch (ResolveUvcProfile(profile)) {
        case UvcStreamProfile::Depth40x30:
            return "40x30";
        case UvcStreamProfile::Mixed120x90:
            return "120x90";
        case UvcStreamProfile::PointCloud160x120:
            return "160x120";
        case UvcStreamProfile::Raw480x360:
            return "480x360";
        case UvcStreamProfile::Auto:
        default:
            return "auto";
    }
}

}  // namespace hm_ld1::internal
