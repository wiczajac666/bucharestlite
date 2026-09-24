find_package(FFmpeg REQUIRED)

add_library(bl_core STATIC
    time.cpp
    result.cpp
    logger.cpp
    undo_stack.cpp
    job_manager.cpp
    media_source.cpp
    demuxer.cpp
    project_repository.cpp
    autosave_ring.cpp
    plugin_validation.cpp
    plugin_loader.cpp
    codec_registry.cpp
    decoder_bridge.cpp
    builtin_plugins.cpp
    ../plugins/passthrough/passthrough_plugin.c
    platform/dynamic_library.cpp
    platform/paths.cpp
)

add_library(bl::core ALIAS bl_core)

target_include_directories(bl_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(bl_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bl_core PUBLIC cxx_std_17)

target_link_libraries(bl_core PUBLIC nlohmann_json::nlohmann_json)

if(NOT MSVC)
    target_link_libraries(bl_core PUBLIC ${CMAKE_DL_LIBS})
endif()

target_link_libraries(bl_core PUBLIC FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil)