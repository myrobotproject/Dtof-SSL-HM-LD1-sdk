#include <hm_ld1_sdk/hm_ld1_sdk.hpp>

#include <iostream>

int main() {
    hm_ld1::Camera camera;
    if (camera.Describe() != "not-open") {
        std::cerr << "unexpected initial camera state\n";
        return 1;
    }

    const hm_ld1::CameraStats stats = camera.Stats();
    if (stats.okPackets != 0 || stats.parseFailures != 0) {
        std::cerr << "unexpected initial camera statistics\n";
        return 2;
    }

    return 0;
}
