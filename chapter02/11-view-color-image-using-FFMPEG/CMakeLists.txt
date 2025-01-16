cmake_minimum_required(VERSION 3.27)

project(chapter0211ViewColorImageUsingFFMPEG LANGUAGES C VERSION 0.0.1)

message("chapter02-11 view color image using FFMPEG decoder")

add_executable(chapter0211ViewColorImageUsingFFMPEG)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0211ViewColorImageUsingFFMPEG PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0211ViewColorImageUsingFFMPEG LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0211ViewColorImageUsingFFMPEG PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0211ViewColorImageUsingFFMPEG LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb - ${Stb_FOUND}")
endif ()

target_sources(chapter0211ViewColorImageUsingFFMPEG PRIVATE
        main.c
)