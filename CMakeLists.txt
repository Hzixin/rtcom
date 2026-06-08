cmake_minimum_required(VERSION 3.14)
project(rtcom VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(PkgConfig REQUIRED)

pkg_check_modules(OSIP2 REQUIRED libosip2)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(GLOG REQUIRED libglog)

# Thread support
find_package(Threads REQUIRED)

# Common headers
set(COMMON_SRC
    src/common/config.cpp
    src/common/sdp.cpp
)

# SIP module
set(SIP_SRC
    src/sip/sip_manager.cpp
    src/sip/sip_call.cpp
)

# Media module
set(MEDIA_SRC
    src/media/media_capture.cpp
    src/media/media_encoder.cpp
    src/media/media_decoder.cpp
)

# RTP module
set(RTP_SRC
    src/rtp/rtp_packet.cpp
    src/rtp/rtp_session.cpp
    src/rtp/rtcp_handler.cpp
    src/rtp/jitter_buffer.cpp
)

# Session module
set(SESSION_SRC
    src/session/session_manager.cpp
)

# Net module
set(NET_SRC
    src/net/udp_socket.cpp
    src/net/io_multiplexer.cpp
    src/net/thread_pool.cpp
)

# Server executable
add_executable(rtcom_server
    src/main.cpp
    ${COMMON_SRC}
    ${SIP_SRC}
    ${MEDIA_SRC}
    ${RTP_SRC}
    ${SESSION_SRC}
    ${NET_SRC}
)

target_include_directories(rtcom_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OSIP2_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${GLOG_INCLUDE_DIRS}
)

target_link_libraries(rtcom_server PRIVATE
    ${OSIP2_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${GLOG_LIBRARIES}
    Threads::Threads
)

# Compiler flags
target_compile_options(rtcom_server PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Test executable
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(rtcom_test
        test/test_main.cpp
        ${COMMON_SRC}
        ${SIP_SRC}
        ${MEDIA_SRC}
        ${RTP_SRC}
        ${NET_SRC}
    )
    target_include_directories(rtcom_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OSIP2_INCLUDE_DIRS}
        ${AVCODEC_INCLUDE_DIRS}
        ${AVFORMAT_INCLUDE_DIRS}
        ${AVUTIL_INCLUDE_DIRS}
        ${SWSCALE_INCLUDE_DIRS}
        ${GLOG_INCLUDE_DIRS}
    )
    target_link_libraries(rtcom_test PRIVATE
        ${OSIP2_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVFORMAT_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${GLOG_LIBRARIES}
        Threads::Threads
    )
    target_compile_options(rtcom_test PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter
    )
    # SIPp integration test
    add_executable(sipp_test
        test/sipp_test.cpp
        ${COMMON_SRC}
        ${SIP_SRC}
        ${RTP_SRC}
        ${NET_SRC}
    )
    target_include_directories(sipp_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OSIP2_INCLUDE_DIRS}
        ${GLOG_INCLUDE_DIRS}
    )
    target_link_libraries(sipp_test PRIVATE
        ${OSIP2_LIBRARIES}
        ${GLOG_LIBRARIES}
        Threads::Threads
    )
    target_compile_options(sipp_test PRIVATE -Wall -Wno-unused-parameter)

    # SIP server test (listens for SIPp UAC calls)
    add_executable(sip_server_test
        test/sip_server_test.cpp
        ${COMMON_SRC}
        ${SIP_SRC}
        ${RTP_SRC}
        ${NET_SRC}
    )
    target_include_directories(sip_server_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OSIP2_INCLUDE_DIRS}
        ${GLOG_INCLUDE_DIRS}
    )
    target_link_libraries(sip_server_test PRIVATE
        ${OSIP2_LIBRARIES}
        ${GLOG_LIBRARIES}
        Threads::Threads
    )
    target_compile_options(sip_server_test PRIVATE -Wall -Wno-unused-parameter)
endif()
