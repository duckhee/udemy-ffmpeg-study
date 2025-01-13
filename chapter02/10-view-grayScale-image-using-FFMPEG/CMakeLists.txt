cmake_minimum_required(VERSION 3.27)

project(chapter0210DecodingFrameAndViewingGrayScaleImage LANGUAGES C VERSION 0.0.1)

message("chapter02-10 decoding a video frame and viewing grey image")

add_executable(chapter0210DecodingFrameAndViewingGrayScaleImage)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0210DecodingFrameAndViewingGrayScaleImage PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0210DecodingFrameAndViewingGrayScaleImage LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif (${FFMPEG_FOUND})

if (${Stb_FOUND})
    target_include_directories(chapter0210DecodingFrameAndViewingGrayScaleImage PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0210DecodingFrameAndViewingGrayScaleImage LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb ${Stb_FOUND}")
endif (${Stb_FOUND})

target_sources(chapter0210DecodingFrameAndViewingGrayScaleImage PRIVATE
        main.c
)