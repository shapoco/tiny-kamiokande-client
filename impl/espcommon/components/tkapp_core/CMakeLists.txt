# アプリ本体 (impl/appcore) の ESP-IDF コンポーネント
set(TKAPP_APPCORE ${CMAKE_CURRENT_LIST_DIR}/../../../appcore)

# ShapoGFX のコンポーネント名はクローンしたディレクトリ名
get_filename_component(SHAPOGFX_COMPONENT "$ENV{SHAPOGFX_PATH}" ABSOLUTE)
get_filename_component(SHAPOGFX_COMPONENT "${SHAPOGFX_COMPONENT}" NAME)

idf_component_register(
    SRCS
        "${TKAPP_APPCORE}/src/app.cpp"
        "${TKAPP_APPCORE}/src/anim.cpp"
        "${TKAPP_APPCORE}/src/scene.cpp"
        "${TKAPP_APPCORE}/src/ui.cpp"
        "${TKAPP_APPCORE}/src/detector.cpp"
        "${TKAPP_APPCORE}/src/cbor.cpp"
        "${TKAPP_APPCORE}/src/eventlog.cpp"
        "${TKAPP_APPCORE}/src/sync.cpp"
        "${TKAPP_APPCORE}/src/history.cpp"
    INCLUDE_DIRS "${TKAPP_APPCORE}/include"
    REQUIRES ${SHAPOGFX_COMPONENT}
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
# 版 (リポジトリ直下の version.txt)。画面の 3D の表示の右下に出す
file(STRINGS ${TKAPP_APPCORE}/../../version.txt TKAPP_VERSION LIMIT_COUNT 1)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TKAPP_VERSION="${TKAPP_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TKAPP_APPCORE}/../../version.txt)
