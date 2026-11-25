# ホスト版 (確認用)。CBOR の応答ファイルを順に流し込み、指定した時刻のフレームを PNG に書く。
#
#   export SHAPOGFX_PATH=...
#   cmake -S impl/host -B impl/host/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build impl/host/build
#   impl/host/build/tkapp_host --out frames --snap 1,3,5 a.cbor b.cbor

cmake_minimum_required(VERSION 3.13)
project(tkapp_host LANGUAGES CXX)

if(NOT DEFINED ENV{SHAPOGFX_PATH})
    message(FATAL_ERROR "SHAPOGFX_PATH is not set (the ShapoGFX clone)")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SHAPOGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory($ENV{SHAPOGFX_PATH} shapo-gfx)
add_subdirectory(../appcore appcore)

add_executable(tkapp_host main.cpp)
target_link_libraries(tkapp_host PRIVATE tkapp::core)
target_compile_options(tkapp_host PRIVATE -Wall -Wextra)
