cmake_minimum_required(VERSION 3.16)

project(hm_ld1_sdk_package_consumer LANGUAGES CXX)

find_package(hm_ld1_sdk CONFIG REQUIRED)

add_executable(hm_ld1_sdk_package_consumer main.cpp)
target_compile_features(hm_ld1_sdk_package_consumer PRIVATE cxx_std_17)
target_link_libraries(hm_ld1_sdk_package_consumer PRIVATE hm_ld1::sdk)
