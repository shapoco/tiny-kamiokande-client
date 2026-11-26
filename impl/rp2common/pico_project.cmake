# ボードを選んで Pico SDK を取り込む。プロジェクトが project() の前に include する
# (texbreakout の impl/rp2common/pico_project.cmake に倣う)。
#
#   set(PICO_BOARD pico2_w_restouch_lcd_2_8)
#   include(<this file>)
#   project(name C CXX ASM)
#   pico_sdk_init()
#   add_subdirectory(<rp2common> rp2common)
#
# SDK は $PICO_SDK_PATH、無ければ ~/pico/pico-sdk (2.2.0 で確認)。picotool (.uf2 を作る) は
# $picotool_DIR、無ければ VS Code 拡張が ~/.pico-sdk/picotool/ に入れた最新のもの
# (それも無ければ SDK がダウンロードしてビルドする)。

if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        set(PICO_SDK_PATH $ENV{HOME}/pico/pico-sdk)
    endif()
endif()
set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Pico SDK")

# SDK のボードヘッダに加えて boards/ のものを探す
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    message(FATAL_ERROR "set PICO_BOARD before including pico_project.cmake")
endif()
# Cortex-M33 (ShapoGFX の FPU 版)
if(NOT PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico platform")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT picotool_DIR AND NOT DEFINED ENV{picotool_DIR})
    file(GLOB TKRP2_PICOTOOLS LIST_DIRECTORIES true
        $ENV{HOME}/.pico-sdk/picotool/*/picotool)
    if(TKRP2_PICOTOOLS)
        list(SORT TKRP2_PICOTOOLS)
        list(GET TKRP2_PICOTOOLS -1 TKRP2_PICOTOOL)
        set(picotool_DIR ${TKRP2_PICOTOOL} CACHE PATH "picotool package")
    endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
