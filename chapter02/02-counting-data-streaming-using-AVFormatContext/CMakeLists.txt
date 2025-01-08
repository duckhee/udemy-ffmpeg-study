cmake_minimum_required(VERSION 3.27)

project(chapter0202CountingDataStreamUsingAVFormatContext LANGUAGES C VERSION 0.0.1)

message("chapter02-02 counting data stream using AVFormatContext")

add_executable(chapter0202CountingDataStreamUsingAVFormatContext)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0202CountingDataStreamUsingAVFormatContext PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0202CountingDataStreamUsingAVFormatContext LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0202CountingDataStreamUsingAVFormatContext PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0202CountingDataStreamUsingAVFormatContext LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to std ${std_FOUND}")
endif ()

target_sources(chapter0202CountingDataStreamUsingAVFormatContext PRIVATE
        main.c
)