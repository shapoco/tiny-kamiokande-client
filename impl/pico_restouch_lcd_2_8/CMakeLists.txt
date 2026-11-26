# Waveshare Pico-ResTouch-LCD-2.8 + Raspberry Pi Pico 2 W 版。共通部分は rp2common/
#
#   ./build.sh                  # build/tiny_kamiokande.uf2 (-DTKAPP_DEMO=ON でデモモード)
#   ./flash.sh                  # picotool で書き込む (BOOTSEL か、動作中なら picotool がリブートさせる)
#   ./flash_swd.sh              # SWD (Debug Probe) で書き込む

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w_restouch_lcd_2_8)
include(${CMAKE_CURRENT_LIST_DIR}/../rp2common/pico_project.cmake)

project(tiny_kamiokande C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../rp2common rp2common)

add_executable(tiny_kamiokande main.cpp)
target_link_libraries(tiny_kamiokande PRIVATE tkrp2_common)
tkrp2_setup(tiny_kamiokande)
