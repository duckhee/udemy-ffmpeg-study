cmake_minimum_required(VERSION 3.27)

project(chapter0214IntroductionAudioData LANGUAGES CXX VERSION 0.0.1)

message("chapter02-14 introduction audio data")

add_executable(chapter0214IntroductionAudioData)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0214IntroductionAudioData PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0214IntroductionAudioData LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0214IntroductionAudioData PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0214IntroductionAudioData LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb - ${Stb_FOUND}")
endif ()
target_sources(chapter0214IntroductionAudioData PRIVATE
        main.c
)