cmake_minimum_required(VERSION 3.27)

project(studyFFMPEG05DecodeAudio LANGUAGES C VERSION 0.0.1)

message("study-FFMPEG-05 decode audio")

add_executable(${PROJECT_NAME})

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(${PROJECT_NAME} PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(${PROJECT_NAME} LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif (${FFMPEG_FOUND})

target_sources(${PROJECT_NAME} PRIVATE
        main.c
)
