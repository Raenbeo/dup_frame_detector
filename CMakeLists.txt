cmake_minimum_required(VERSION 3.25)
project(dup_frame_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)

# Qt-free libraries (unit-testable)
add_library(decoder STATIC src/decoder/decoder.cpp)
target_include_directories(decoder PUBLIC src)
target_link_libraries(decoder PUBLIC ${OpenCV_LIBS} PRIVATE PkgConfig::FFMPEG)
target_include_directories(decoder PUBLIC ${OpenCV_INCLUDE_DIRS})

add_library(matcher STATIC src/matcher/matcher.cpp)
target_include_directories(matcher PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(matcher PUBLIC ${OpenCV_LIBS} decoder)

# UI + App
add_library(ui STATIC
    src/ui/main_window.cpp
    src/ui/main_window.hpp
    src/ui/video_view.cpp
    src/ui/video_view.hpp
    src/ui/timeline.cpp
    src/ui/timeline.hpp)
target_include_directories(ui PUBLIC src)
target_link_libraries(ui PUBLIC Qt6::Widgets decoder matcher)

add_executable(dup_frame_detector src/app/main.cpp)
target_link_libraries(dup_frame_detector PRIVATE ui)

# Tests
enable_testing()
add_executable(decoder_test tests/decoder_test.cpp)
target_link_libraries(decoder_test PRIVATE decoder)

add_executable(matcher_test tests/matcher_test.cpp)
target_link_libraries(matcher_test PRIVATE matcher)
add_test(NAME matcher COMMAND matcher_test)

add_executable(loop_test tests/loop_test.cpp)
target_link_libraries(loop_test PRIVATE matcher)

add_executable(ui_smoke_test tests/ui_smoke_test.cpp)
target_link_libraries(ui_smoke_test PRIVATE ui Qt6::Test)

add_executable(export_test tests/export_test.cpp)
target_link_libraries(export_test PRIVATE decoder)
