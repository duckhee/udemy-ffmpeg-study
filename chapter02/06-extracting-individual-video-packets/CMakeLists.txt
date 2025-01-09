cmake_minimum_required(VERSION 3.27)

project(chapter0206ExtractingIndividualVideoPacket LANGUAGES C VERSION 0.0.1)

message("chapter02-06 extracting individual video packets")

add_executable(chapter0206ExtractingIndividualVideoPacket)

find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0206ExtractingIndividualVideoPacket PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0206ExtractingIndividualVideoPacket LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "This program need to FFMPEG - ${FFMPEG_FOUND}")
endif ()

if (${Stb_FOUND})
    target_include_directories(chapter0206ExtractingIndividualVideoPacket PRIVATE
            ${Stb_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0206ExtractingIndividualVideoPacket LINK_PRIVATE
            ${Stb_LIBRARIES}
    )
else (${Stb_FOUND})
    message(STATUS "This program need to Stb lib - ${Stb_FOUND}")
endif ()

target_sources(chapter0206ExtractingIndividualVideoPacket PRIVATE
        main.c
)