# M5Stack 版のボード部分 (M5Unified での起動、タッチ、パネル出力)。フレームループは espcommon/components/tk_runner
if(IDF_TARGET STREQUAL "esp32p4")
    set(PANEL_SRC "panel_ppa.cpp")
    set(PANEL_REQUIRES esp_driver_ppa esp_mm)
else()
    set(PANEL_SRC "panel_spi.cpp")
    set(PANEL_REQUIRES "")
endif()

idf_component_register(
    SRCS "m5platform.cpp" "${PANEL_SRC}"
    INCLUDE_DIRS "."
    REQUIRES tk_runner tkapp_core m5unified m5gfx ${PANEL_REQUIRES}
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
