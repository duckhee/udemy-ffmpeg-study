cmake_minimum_required(VERSION 3.27)

project(chapter0201ProjectSetting LANGUAGES C VERSION 0.0.1)

message("chapter02-01 project setting")

add_executable(chapter0201ProjectSetting)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)


if (${FFMPEG_FOUND})
    target_include_directories(chapter0107VideoStream PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0107VideoStream LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})

    target_include_directories(chapter0201ProjectSetting PRIVATE ${Stb_INCLUDE_DIR})

else (${Stb_FOUND})
    message(STATUS "This program need to Stb ${Stb_FOUND}")
endif ()

target_sources(chapter0201ProjectSetting PRIVATE
        main.c
)


