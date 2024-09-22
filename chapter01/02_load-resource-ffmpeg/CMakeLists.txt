cmake_minimum_required(VERSION 3.27)

project(chapter0102LoadResource LANGUAGES C VERSION 0.0.1)

message("chapter01-02 Load Resource Using FFMPEG")

find_package(FFMPEG REQUIRED)

add_executable(chapter0102LoadResource)


target_sources(chapter0102LoadResource PRIVATE
        main.c
)

target_link_libraries(chapter0102LoadResource LINK_PRIVATE
        ${FFMPEG_LIBRARIES}
)

target_include_directories(chapter0102LoadResource PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(chapter0102LoadResource PRIVATE
        ${FFMPEG_LIBRARY_DIRS}
)