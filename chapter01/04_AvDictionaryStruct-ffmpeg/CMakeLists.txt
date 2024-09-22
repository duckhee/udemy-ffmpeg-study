cmake_minimum_required(VERSION 3.27)

project(chapter0104AvDictionaryStructFFMPEG LANGUAGES C VERSION 0.0.1)

message("chapter01-03 av dictionary struct ffmpeg")

find_package(FFMPEG REQUIRED)

add_executable(chapter0104AvDictionaryStructFFMPEG)

target_sources(chapter0104AvDictionaryStructFFMPEG PRIVATE
        main.c
)

target_link_libraries(chapter0104AvDictionaryStructFFMPEG LINK_PRIVATE
        ${FFMPEG_LIBRARIES}
)

target_include_directories(chapter0104AvDictionaryStructFFMPEG PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(chapter0104AvDictionaryStructFFMPEG PRIVATE
        ${FFMPEG_LIBRARY_DIRS}
)