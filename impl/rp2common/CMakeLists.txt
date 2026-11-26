# Pico SDK (RP2350 + CYW43439) 版の共通部分: ShapoGFX と appcore の取り込み、クロック、パネル、
# フレームループ、WiFi と HTTPS、データ取得、シリアル、設定の保存。各プロジェクトは pico_sdk_init() の後に
#
#   add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../rp2common rp2common)
#   add_executable(tiny_kamiokande main.cpp)
#   target_link_libraries(tiny_kamiokande PRIVATE tkrp2_common)
#   tkrp2_setup(tiny_kamiokande)
#
# オプション (cmake の -D):
#   TKAPP_DEMO=ON        通信せず埋め込みのサンプルを順に表示する
#   RP2_DUAL_CORE=OFF    core0 だけで描く (比較用)
#   RP2_LCD_FLIP=ON      画面を 180 度回す
#   RP2_STDIO_RTT=ON     ログを SWD のデバッガに出す (SEGGER RTT。シリアルのコマンドは使えない)
#   RP2_TOUCH_LOG=ON     抵抗膜タッチ (XPT2046) の生値と画面の座標をログに出す (写像の確認用)
#   RP2_GFX3D_IN_RAM=OFF ShapoGFX の 3D 描画のコードを Flash に置いたままにする (RAM を約 60 KB 空ける)
#   RP2_SYS_KHZ、RP2_VREG、RP2_FLASH_CLKDIV、RP2_FLASH_RXDELAY、RP2_PERI_DIV、RP2_LCD_BAUD
#                        クロックの設定 (既定は clocks.hpp と runner.cpp)

option(TKAPP_DEMO "Show the embedded samples instead of fetching" OFF)
option(RP2_DUAL_CORE "Draw every strip on both cores" ON)
option(RP2_LCD_FLIP "Turn the screen by 180 degrees" OFF)
option(RP2_STDIO_RTT "Log through the SWD debugger (SEGGER RTT)" OFF)
option(RP2_TOUCH_LOG "Log the raw and mapped coordinates of the resistive touch (XPT2046)" OFF)
option(RP2_GFX3D_IN_RAM "Run the 3D renderer's code of ShapoGFX from RAM" ON)
set(RP2_CLOCK_OPTIONS RP2_SYS_KHZ RP2_VREG RP2_FLASH_CLKDIV RP2_FLASH_RXDELAY RP2_PERI_DIV RP2_LCD_BAUD)
foreach(opt IN LISTS RP2_CLOCK_OPTIONS)
    set(${opt} "" CACHE STRING "Clock setting (see rp2common/clocks.hpp)")
endforeach()

get_filename_component(TKRP2_IMPL "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED ENV{SHAPOGFX_PATH})
    message(FATAL_ERROR "SHAPOGFX_PATH is not set (the ShapoGFX clone)")
endif()
set(SHAPOGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# ESP32-S3 と同じ: テクスチャの遠近補正はスパンの両端で正確 (レベル 1)、頂点処理は固定小数
set(SHAPOGFX3D_CORRECT_PERSPECTIVE 1 CACHE STRING "" FORCE)
set(SHAPOGFX3D_FIXED_POINT 1 CACHE STRING "" FORCE)
add_subdirectory($ENV{SHAPOGFX_PATH} shapo-gfx)
# ShapoGFX の RP2 向けの最適化 (SIO 補間器) を有効にするため
target_link_libraries(shapogfx PRIVATE hardware_interp_headers)
# スパンの描画を RAM で動かす (ShapoGFX の example/rp2350-touch-lcd-2/demo3d と同じ)
target_compile_definitions(shapogfx PRIVATE
    "SHAPOGFX3D_HOT_ATTR=__attribute__((section(\".time_critical.gfx3d\")))"
    SHAPOGFX3D_HOT_INSTANTIATE=1)

# アプリ本体
add_subdirectory(${TKRP2_IMPL}/appcore appcore)

set(TKRP2_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/clocks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/console.cpp
    ${CMAKE_CURRENT_LIST_DIR}/diag.cpp
    ${CMAKE_CURRENT_LIST_DIR}/net.cpp
    ${CMAKE_CURRENT_LIST_DIR}/panel_spi.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pipeline.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runner.cpp
    ${CMAKE_CURRENT_LIST_DIR}/settings_store.cpp
    ${CMAKE_CURRENT_LIST_DIR}/touch_xpt2046.cpp
)
# tkrp2_setup() はプロジェクトのディレクトリから呼ばれるので、親にも見せる
set(TKRP2_SOURCES ${TKRP2_SOURCES} PARENT_SCOPE)

# SDK のライブラリと同じ INTERFACE ライブラリ: ソースはプログラムが 1 回だけコンパイルする
add_library(tkrp2_common INTERFACE)
target_sources(tkrp2_common INTERFACE ${TKRP2_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/samples.S)
target_include_directories(tkrp2_common INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(tkrp2_common INTERFACE
    tkapp::core
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mbedtls
    pico_mbedtls
    hardware_clocks
    hardware_dma
    hardware_flash
    hardware_interp
    hardware_pwm
    hardware_spi
    hardware_vreg
    hardware_watchdog
)
target_compile_definitions(tkrp2_common INTERFACE
    TKAPP_DEMO=$<BOOL:${TKAPP_DEMO}>
    TKRP2_DUAL_CORE=$<BOOL:${RP2_DUAL_CORE}>
    RP2COMMON_LCD_FLIP=$<BOOL:${RP2_LCD_FLIP}>
    RP2COMMON_TOUCH_LOG=$<BOOL:${RP2_TOUCH_LOG}>
    # core1 は runner.cpp の専用スタックで動かす
    PICO_CORE1_STACK_SIZE=0
    # TLS の握手が core1 の帯を遅らせても再起動しないよう、ウォッチドッグは長めに
    RP2COMMON_WATCHDOG_MS=8000
    # CYW43 の SPI (PIO) は clk_sys を分周する。既定の 2 は 150 MHz 用で、300 MHz では
    # 仕様 (50 MHz) を超えるので 4 にする (37.5 MHz。150 MHz の既定と同じ速さ)
    CYW43_PIO_CLOCK_DIV_INT=4
    # DTR を上げない端末 (pyserial の既定など) にもログを出す
    PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
    # lwipopts.h と mbedtls_config.h はこのディレクトリのもの
    MBEDTLS_CONFIG_FILE="mbedtls_config.h"
)
foreach(opt IN LISTS RP2_CLOCK_OPTIONS)
    if(NOT "${${opt}}" STREQUAL "")
        string(REPLACE "RP2_" "RP2COMMON_" def ${opt})
        target_compile_definitions(tkrp2_common INTERFACE ${def}=${${opt}})
    endif()
endforeach()

# プログラムごとの設定: 警告、デモのデータ、stdio、3D のコードの置き場所、uf2
function(tkrp2_setup target)
    set(TKRP2_SAMPLES ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../espcommon/samples)
    get_target_property(APP_SOURCES ${target} SOURCES)
    set_property(SOURCE ${APP_SOURCES} ${TKRP2_SOURCES}
        TARGET_DIRECTORY ${target}
        APPEND PROPERTY COMPILE_OPTIONS -Wall -Wextra)
    set_property(SOURCE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/samples.S
        TARGET_DIRECTORY ${target}
        APPEND PROPERTY COMPILE_DEFINITIONS
            TK_SAMPLE_S1="${TKRP2_SAMPLES}/s1.cbor"
            TK_SAMPLE_S2="${TKRP2_SAMPLES}/s2.cbor"
            TK_SAMPLE_S3="${TKRP2_SAMPLES}/s3.cbor")
    set_property(SOURCE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/samples.S
        TARGET_DIRECTORY ${target}
        APPEND PROPERTY OBJECT_DEPENDS
            ${TKRP2_SAMPLES}/s1.cbor ${TKRP2_SAMPLES}/s2.cbor ${TKRP2_SAMPLES}/s3.cbor)
    if(RP2_STDIO_RTT)
        pico_enable_stdio_usb(${target} 0)
        pico_enable_stdio_uart(${target} 0)
        pico_enable_stdio_rtt(${target} 1)
        target_compile_definitions(${target} PRIVATE BUFFER_SIZE_UP=8192)
    else()
        pico_enable_stdio_usb(${target} 1)
        pico_enable_stdio_uart(${target} 0)
    endif()
    if(RP2_GFX3D_IN_RAM)
        # ShapoGFX の 3D 描画 (src/gfx3d/ の gfx3d.cpp と scene.cpp) を RAM に置く。Flash の .text の規則から外すと、
        # .data の *(.text*) が拾う (ShapoGFX の example/rp2350-touch-lcd-2/demo3d に倣う。XIP キャッシュを通信のコードと
        # 取り合わない)。shapes.cpp (形状の生成。gfx2d の同名の図形描画も一緒に入る、約 18 KB) は描画の内側ではないので
        # Flash のまま (TLS の握手のヒープの余裕のため)
        set(SDK_LD ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld)
        file(READ ${SDK_LD} LD)
        set(FLASH_RULE "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)")
        string(FIND "${LD}" "${FLASH_RULE}" FOUND)
        if(FOUND EQUAL -1)
            message(FATAL_ERROR "RP2_GFX3D_IN_RAM: the SDK's linker script changed")
        endif()
        string(REPLACE "${FLASH_RULE}"
            "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a: *libshapogfx.a:gfx3d.cpp.o *libshapogfx.a:scene.cpp.o) .text*)"
            LD "${LD}")
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/memmap_gfx3d_in_ram.ld "${LD}")
        pico_set_linker_script(${target} ${CMAKE_CURRENT_BINARY_DIR}/memmap_gfx3d_in_ram.ld)
    endif()
    pico_add_extra_outputs(${target})
endfunction()
