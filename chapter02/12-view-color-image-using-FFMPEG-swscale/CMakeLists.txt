cmake_minimum_required(VERSION 3.27)

project(chapter0212ViewingColorImageUsingSwScaleSetting LANGUAGES C VERSION 0.0.1)

message("chapter02-12 view color image using FFMPEG decoder - setting software scale")

add_executable(chapter0212ViewingColorImageUsingSwScaleSetting)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0212ViewingColorImageUsingSwScaleSetting PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0212ViewingColorImageUsingSwScaleSetting LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0212ViewingColorImageUsingSwScaleSetting PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0212ViewingColorImageUsingSwScaleSetting LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb - ${Stb_FOUND}")
endif ()

target_sources(chapter0212ViewingColorImageUsingSwScaleSetting PRIVATE
        main.c
)