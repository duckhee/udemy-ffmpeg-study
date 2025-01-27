cmake_minimum_required(VERSION 3.27)

project(Demuxing LANGUAGES C VERSION 0.0.1)

message("02 demuxing")

add_executable(Demuxing)

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(Demuxing PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(Demuxing LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

target_sources(Demuxing PRIVATE
        main.c
)
