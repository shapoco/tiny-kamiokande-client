idf_component_register(SRCS "app_main.cpp" REQUIRES m5platform)
# idf.py -DTKAPP_DEMO=1 build で通信せず埋め込みのサンプルを表示する
if(DEFINED TKAPP_DEMO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TKAPP_DEMO=${TKAPP_DEMO})
endif()
