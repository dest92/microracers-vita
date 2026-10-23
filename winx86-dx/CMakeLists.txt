cmake_minimum_required(VERSION 3.16)
project(winx86_dx LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(winx86_dx STATIC
    src/dinput/dinput.cpp
    src/ddraw/software.cpp
    src/d3d/device.cpp
    src/shims/base.cpp
    src/shims/imports.cpp
    src/audio/mixer.cpp
    src/audio/dsound_bus.cpp
    src/io/read_stream.cpp
    src/io/vfs.cpp
  src/winmm/mci.cpp
  src/winmm/mmio.cpp
)
target_compile_features(winx86_dx PUBLIC cxx_std_17)
target_include_directories(winx86_dx PUBLIC include)
target_link_libraries(winx86_dx PUBLIC Threads::Threads)
