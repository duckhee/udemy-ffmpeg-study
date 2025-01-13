cmake_minimum_required(VERSION 3.27)

project(chapter0209DecodingVideoFrame LANGUAGES C VERSION 0.0.1)

message("chapter02-09 decoding a video frame")

add_executable(chapter0209DecodingVideoFrame)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0209DecodingVideoFrame PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0209DecodingVideoFrame LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif (${FFMPEG_FOUND})

if (${Stb_FOUND})
    target_include_directories(chapter0209DecodingVideoFrame PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0209DecodingVideoFrame LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb ${Stb_FOUND}")
endif (${Stb_FOUND})

target_sources(chapter0209DecodingVideoFrame PRIVATE
        main.c
)