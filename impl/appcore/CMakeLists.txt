# アプリ本体 (機種非依存)。ホスト版と Pico SDK などのビルドが使う
# (ESP-IDF は impl/espcommon/components/tkapp_core)。
#
#   add_subdirectory(impl/appcore appcore)
#   target_link_libraries(app PRIVATE tkapp::core)

set(TKAPP_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/anim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cbor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eventlog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/history.cpp
)
set(TKAPP_CORE_SOURCES ${TKAPP_CORE_SOURCES} PARENT_SCOPE)


add_library(tkapp_core STATIC ${TKAPP_CORE_SOURCES})
add_library(tkapp::core ALIAS tkapp_core)
target_include_directories(tkapp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tkapp_core PUBLIC shapoco::gfx)
target_compile_features(tkapp_core PUBLIC cxx_std_17)
target_compile_options(tkapp_core PRIVATE -Wall -Wextra)
# 版 (リポジトリ直下の version.txt)。画面の 3D の表示の右下に出す
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../version.txt TKAPP_VERSION LIMIT_COUNT 1)
target_compile_definitions(tkapp_core PRIVATE TKAPP_VERSION="${TKAPP_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../version.txt)
