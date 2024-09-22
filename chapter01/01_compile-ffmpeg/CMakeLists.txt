cmake_minimum_required(VERSION 3.27)

message("chapter01-01 compile ffmpeg")

project(chapter0101CompileFFMPEG LANGUAGES C VERSION 0.0.1)

add_executable(chapter0101CompileFFMPEG)

target_sources(chapter0101CompileFFMPEG PRIVATE
        main.c
)