cmake_minimum_required(VERSION 3.27)

project(chapter0105TimeBaseAVTime LANGUAGES C VERSION 0.0.1)

message("chapter01-05 TimeBase AV Time")

add_executable(chapter0105TimeBaseAVTime)

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0105TimeBaseAVTime PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0105TimeBaseAVTime LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This Program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

target_sources(chapter0105TimeBaseAVTime PRIVATE
        main.c
)