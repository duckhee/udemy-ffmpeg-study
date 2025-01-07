cmake_minimum_required(VERSION 3.27)

project(chapter0203AdvancedFFMPEGDataStructure LANGUAGES C VERSION 0.0.1)

message("chapter02-03 advanced FFMPEG Data Structure")

add_executable(chapter0203AdvancedFFMPEGDataStructure)

find_package(FFMPEG REQUIRED)
find_package(stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0203AdvancedFFMPEGDataStructure PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0203AdvancedFFMPEGDataStructure LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

if (${stb_FOUND})
    target_include_directories(chapter0203AdvancedFFMPEGDataStructure PRIVATE
            ${stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0203AdvancedFFMPEGDataStructure LINK_PRIVATE
            ${stb_LIBRARIES}
    )
else (${stb_FOUND})
    message(STATUS "This program need to std ${std_FOUND}")
endif ()

target_sources(chapter0203AdvancedFFMPEGDataStructure PRIVATE
        main.c
)