cmake_minimum_required(VERSION 3.27)

project(chapter0107VideoStream LANGUAGES C VERSION 0.0.1)

message("chapter01-07 video stream")

add_executable(chapter0107VideoStream)

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0107VideoStream PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0107VideoStream LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

target_sources(chapter0107VideoStream PRIVATE
        main.c
)
