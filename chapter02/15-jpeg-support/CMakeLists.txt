cmake_minimum_required(VERSION 3.27)

project(chapter0215JPEGImage LANGUAGES CXX VERSION 0.0.1)

message("chapter02-14 JPEG Support ")

add_executable(chapter0215JPEGImage)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0215JPEGImage PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0215JPEGImage LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0215JPEGImage PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0215JPEGImage LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb - ${Stb_FOUND}")
endif ()
target_sources(chapter0215JPEGImage PRIVATE
        main.c
)