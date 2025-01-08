cmake_minimum_required(VERSION 3.27)

project(chapter0205FindVideoStream LANGUAGES C VERSION 0.0.1)

message("chapter02-05 FFMPEG find Video Stream ")

add_executable(chapter0205FindVideoStream)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0205FindVideoStream PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0205FindVideoStream LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0205FindVideoStream PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0205FindVideoStream LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to std ${std_FOUND}")
endif ()

target_sources(chapter0205FindVideoStream PRIVATE
        main.c
)
