cmake_minimum_required(VERSION 3.27)

project(chapter0106FunctionMacroAvDictionaryStruct LANGUAGES C VERSION 0.0.1)

message("chapter01-06 function macro AVDictionary data struct")

add_executable(chapter0106FunctionMacroAvDictionaryStruct)

find_package(FFMPEG REQUIRED)

if (${FFMPEG_FOUND})
    target_include_directories(chapter0106FunctionMacroAvDictionaryStruct PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0106FunctionMacroAvDictionaryStruct LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
else (${FFMPEG_FOUND})
    message(STATUS "check ffmpeg library ${FFMPEG_FOUND}")
endif ()

target_sources(chapter0106FunctionMacroAvDictionaryStruct PRIVATE
        main.c
)