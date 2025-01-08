cmake_minimum_required(VERSION 3.27)

project(chapter0203AllocatedMemoryFFMPEG_VideoData LANGUAGES C VERSION 0.0.1)

message("chapter02-03 allocated memory FFMPEG video data")

add_executable(chapter0203AllocatedMemoryFFMPEG_VideoData)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0203AllocatedMemoryFFMPEG_VideoData PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0203AllocatedMemoryFFMPEG_VideoData LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0203AllocatedMemoryFFMPEG_VideoData PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0203AllocatedMemoryFFMPEG_VideoData LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to std ${std_FOUND}")
endif ()

target_sources(chapter0203AllocatedMemoryFFMPEG_VideoData PRIVATE
        main.c
)
