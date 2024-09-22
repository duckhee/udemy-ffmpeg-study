cmake_minimum_required(VERSION 3.27)

project(chapter0103DumpFormatFFMPEG LANGUAGES C VERSION 0.0.1)

message("chapter01-03 dump format ffmpeg")


find_package(FFMPEG REQUIRED)

add_executable(chapter0103DumpFormatFFMPEG)


target_sources(chapter0103DumpFormatFFMPEG PRIVATE
        main.c
)

target_link_libraries(chapter0103DumpFormatFFMPEG LINK_PRIVATE
        ${FFMPEG_LIBRARIES}
)

target_include_directories(chapter0103DumpFormatFFMPEG PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(chapter0103DumpFormatFFMPEG PRIVATE
        ${FFMPEG_LIBRARY_DIRS}
)