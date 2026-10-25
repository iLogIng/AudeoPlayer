cmake_minimum_required(VERSION 3.15)
project(AudeoPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find FFmpeg
find_package(PkgConfig REQUIRED)

pkg_check_modules(AVFORMAT  REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC   REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL    REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE   REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
# 滤镜（倍速、简单滤镜链）
# pkg_check_modules(AVFILTER REQUIRED IMPORTED_TARGET libavfilter)

# find SDL3
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
# 如果是静态链接 SDL3，可能需要同时查找 SDL3_main 并链接 SDL3::SDL3_main
# 静态链接 需要附加
# find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3 SDL3_main)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(AudeoPlayer
    main.cpp
)

target_include_directories(AudeoPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接库
target_link_libraries(AudeoPlayer PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::SWRESAMPLE
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    SDL3::SDL3
    # PkgConfig::AVFILTER   # 启用滤镜
    # SDL3::SDL3_main       # 静态链接 SDL3 且需要入口替换时取消注释
)
