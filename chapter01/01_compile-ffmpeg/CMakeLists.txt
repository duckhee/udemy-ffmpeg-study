cmake_minimum_required(VERSION 3.27)

message("chapter01-01 compile ffmpeg")

find_package(FFMPEG REQUIRED)

project(chapter0101CompileFFMPEG LANGUAGES C VERSION 0.0.1)

add_executable(chapter0101CompileFFMPEG)

target_sources(chapter0101CompileFFMPEG PRIVATE
        main.c
)

target_include_directories(chapter0101CompileFFMPEG PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(chapter0101CompileFFMPEG PRIVATE ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(chapter0101CompileFFMPEG PRIVATE ${FFMPEG_LIBRARIES})
