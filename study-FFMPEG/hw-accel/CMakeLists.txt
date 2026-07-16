cmake_minimum_required(VERSION 3.27)

# VideoToolbox 기반 하드웨어 가속 레슨은 macOS 전용이다.
if (APPLE)
    add_subdirectory(01-list-hw-devices)
    add_subdirectory(02-hw-decode)
    add_subdirectory(03-hw-encode)
else ()
    message(STATUS "study-FFMPEG/hw-accel lessons are macOS(VideoToolbox) only - skipped")
endif ()
