cmake_minimum_required(VERSION 3.16)

project(hm_ld1_sdk VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(HM_LD1_SDK_TOP_LEVEL_DEFAULT ON)
else()
    set(HM_LD1_SDK_TOP_LEVEL_DEFAULT OFF)
endif()

option(HM_LD1_SDK_ENABLE_WARNINGS "Enable common compiler warnings for hm_ld1_sdk" ${HM_LD1_SDK_TOP_LEVEL_DEFAULT})
option(HM_LD1_SDK_BUILD_TOOLS "Build hm_ld1_sdk command-line tools" ${HM_LD1_SDK_TOP_LEVEL_DEFAULT})

add_library(hm_ld1_sdk SHARED
    src/camera.cpp
    src/internal/frame_event.cpp
    src/protocol/frame_parser.cpp
    src/protocol/serial_protocol.cpp
    src/protocol/udp_protocol.cpp
    src/protocol/uvc_protocol.cpp
    src/transport/packet_source.cpp
    src/transport/packet_source_serial.cpp
    src/transport/packet_source_udp.cpp
    src/transport/packet_source_uvc.cpp
    src/transport/udp_network_configure.cpp
    src/transport/udp_network_format.cpp
    src/transport/udp_network_probe.cpp
    src/transport/serial_port.cpp
    src/transport/udp_socket.cpp
    src/transport/uvc_device.cpp
)

add_library(hm_ld1::sdk ALIAS hm_ld1_sdk)

target_compile_features(hm_ld1_sdk PUBLIC cxx_std_17)

set_target_properties(hm_ld1_sdk PROPERTIES
    CXX_EXTENSIONS OFF
)

set_target_properties(hm_ld1_sdk PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME hm_ld1_sdk
    EXPORT_NAME sdk
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(hm_ld1_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_compile_definitions(hm_ld1_sdk PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(HM_LD1_SDK_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(hm_ld1_sdk PRIVATE /W4)
    else()
        target_compile_options(hm_ld1_sdk PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

function(hm_ld1_add_tool target source_file)
    add_executable(${target}
        ${source_file}
    )

    target_link_libraries(${target}
        PRIVATE
            hm_ld1_sdk
    )

    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(HM_LD1_SDK_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endfunction()

if(HM_LD1_SDK_BUILD_TOOLS)
    set(HM_LD1_SDK_TOOL_TARGETS
        hm_ld1_sdk_depth_dump
        hm_ld1_sdk_timestamp_probe
        hm_ld1_sdk_pointcloud_probe
        hm_ld1_sdk_pointcloud_dump
    )

    hm_ld1_add_tool(hm_ld1_sdk_depth_dump tools/hm_ld1_sdk_depth_dump.cpp)
    hm_ld1_add_tool(hm_ld1_sdk_timestamp_probe tools/hm_ld1_sdk_timestamp_probe.cpp)
    hm_ld1_add_tool(hm_ld1_sdk_pointcloud_probe tools/hm_ld1_sdk_pointcloud_probe.cpp)
    hm_ld1_add_tool(hm_ld1_sdk_pointcloud_dump tools/hm_ld1_sdk_pointcloud_dump.cpp)
endif()

install(TARGETS hm_ld1_sdk
    EXPORT hm_ld1_sdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(HM_LD1_SDK_BUILD_TOOLS)
    install(TARGETS ${HM_LD1_SDK_TOOL_TARGETS}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(FILES include/hm_ld1_sdk/hm_ld1_sdk.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hm_ld1_sdk
)

install(FILES
    LICENSE
    README.md
    README_CN.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/hm_ld1_sdk
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/hm_ld1_sdkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/hm_ld1_sdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/hm_ld1_sdkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hm_ld1_sdk
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hm_ld1_sdkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hm_ld1_sdkConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hm_ld1_sdk
)

install(EXPORT hm_ld1_sdkTargets
    FILE hm_ld1_sdkTargets.cmake
    NAMESPACE hm_ld1::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hm_ld1_sdk
)
