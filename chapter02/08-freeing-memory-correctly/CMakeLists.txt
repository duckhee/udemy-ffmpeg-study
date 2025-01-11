cmake_minimum_required(VERSION 3.27)

project(chapter0208FreeingMemoryCorrectly LANGUAGES C VERSION 0.0.1)

message("chapter02-08 Freeing Memory Correctly")

add_executable(chapter0208FreeingMemoryCorrectly)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0208FreeingMemoryCorrectly PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0208FreeingMemoryCorrectly LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0208FreeingMemoryCorrectly PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0208FreeingMemoryCorrectly LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb lib - ${Stb_FOUND}")
endif ()

target_sources(chapter0208FreeingMemoryCorrectly PRIVATE
        main.c
)