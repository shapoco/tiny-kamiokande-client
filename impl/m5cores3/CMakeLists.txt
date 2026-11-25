# M5Stack CoreS3 版。共通部分は ../espcommon/components/ と ../m5common/components/
#
#   ./build.sh    # ビルド
#   ./run.sh      # ビルドと書き込み
#   ./monitor.sh  # シリアルログ
# 環境変数: IDF_ROOT (ESP-IDF 5.5 のディレクトリ)、SHAPOGFX_PATH (ShapoGFX のクローン)

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{SHAPOGFX_PATH})
    message(FATAL_ERROR "SHAPOGFX_PATH is not set")
endif()
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../espcommon/components
    ${CMAKE_CURRENT_LIST_DIR}/../m5common/components
    $ENV{SHAPOGFX_PATH})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinykamiokande)
