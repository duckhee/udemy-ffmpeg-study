cmake_minimum_required(VERSION 3.27)

project(Decoding LANGUAGES C VERSION 0.0.1)

message("04-decoding")

add_executable(Decoding)

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(Decoding PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(Decoding LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()


target_sources(Decoding PRIVATE
        main.c
)