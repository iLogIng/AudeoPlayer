cmake_minimum_required(VERSION 3.15)
project(AudeoPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# pthread (signalfd / eventfd 信号监听)
find_package(Threads REQUIRED)
# 如果是静态链接 SDL3，可能需要同时查找 SDL3_main 并链接 SDL3::SDL3_main
# 同时需要附加
# find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3 SDL3_main)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(avplayer_core STATIC
    src/core/AppEvent.cpp
    src/core/PacketQueue.cpp
    src/core/SignalListener.cpp
    src/media/Demuxer.cpp
    src/media/AudioDecoder.cpp
    src/media/Resampler.cpp
    src/media/VideoDecoder.cpp
    src/media/VideoConverter.cpp
    src/media/VideoFrameQueue.cpp
    src/output/PcmRingBuffer.cpp
    src/output/AudioOutput.cpp
    src/output/VideoOutput.cpp
    src/output/Window.cpp
    src/player/AudioPipeline.cpp
    src/player/MediaPlayer.cpp
    src/player/VideoPipeline.cpp
    src/player/VideoPresenter.cpp
    src/sync/AudioClock.cpp
)

target_include_directories(avplayer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接库
target_link_libraries(avplayer_core PUBLIC
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    PkgConfig::SWSCALE
    SDL3::SDL3
    Threads::Threads
    # PkgConfig::AVFILTER   # 启用滤镜
    # SDL3::SDL3_main       # 静态链接 SDL3 且需要入口替换时取消注释
)

target_compile_options(avplayer_core PRIVATE -Wall -Wextra)

add_executable(AudeoPlayer
    main.cpp
)

target_link_libraries(AudeoPlayer PRIVATE
    avplayer_core
)
