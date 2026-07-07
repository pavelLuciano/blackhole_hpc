cmake_minimum_required(VERSION 3.20)
project(blackhole_serial LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Modo IMAGEN (foto con disco de acreción)
add_executable(serial_image main.cpp)
target_include_directories(serial_image PRIVATE ${CMAKE_SOURCE_DIR}/../common)
target_compile_options(serial_image PRIVATE -O3)

# Modo VIDEO (esferas orbitando, lee escena de archivo)
add_executable(serial_video render_video.cpp)
target_include_directories(serial_video PRIVATE ${CMAKE_SOURCE_DIR}/../common)
target_compile_options(serial_video PRIVATE -O3)
