cmake_minimum_required(VERSION 3.27)

project(chapter0207FindAudioStreamAndExtractingAudioPackets LANGUAGES C VERSION 0.0.1)

message("chapter02-07 find audio stream and extracting audio packets")

add_executable(chapter0207FindAudioStreamAndExtractingAudioPackets)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0207FindAudioStreamAndExtractingAudioPackets PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0207FindAudioStreamAndExtractingAudioPackets LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0207FindAudioStreamAndExtractingAudioPackets PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0207FindAudioStreamAndExtractingAudioPackets LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb lib - ${Stb_FOUND}")
endif ()

target_sources(chapter0207FindAudioStreamAndExtractingAudioPackets PRIVATE
        main.c
)