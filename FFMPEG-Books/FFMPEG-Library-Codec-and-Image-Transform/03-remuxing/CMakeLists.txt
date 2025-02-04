cmake_minimum_required(VERSION 3.27)

project(Remuxing LANGUAGES C VERSION 0.0.1)

add_executable(Remuxing)

message("03 remuxing")

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(Remuxing PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(Remuxing LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

target_sources(Remuxing PRIVATE
        main.c
)