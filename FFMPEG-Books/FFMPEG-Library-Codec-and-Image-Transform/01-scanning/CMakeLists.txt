cmake_minimum_required(VERSION 3.27)

project(Scanning LANGUAGES C VERSION 0.0.1)

message("01 scanning")

add_executable(Scanning)

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(Scanning PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(Scanning LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

target_sources(Scanning PRIVATE
        main.c
)