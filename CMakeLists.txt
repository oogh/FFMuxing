cmake_minimum_required(VERSION 3.14)

project(GHExport)

set(CMAKE_CXX_STANDARD 14)

include_directories(/usr/local/Cellar/ffmpeg/4.2.2/include)
link_directories(/usr/local/Cellar/ffmpeg/4.2.2/lib)

aux_source_directory(. SRC_DIR)

add_executable(exporter ${SRC_DIR})

target_link_libraries(exporter
        avformat
        avcodec
        avutil
        avfilter
        swresample
        swscale)