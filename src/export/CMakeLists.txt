# Export pipeline module (Phase 5 group: EXPORT-1..EXPORT-5).
#
# Provides the BlExportEngine that coordinates video+audio encoding via
# FFmpeg-backed codec plugins and muxes the result into a container (MP4/MKV).

if(NOT BL_BUILD_EXPORT)
    return()
endif()

find_package(FFmpeg REQUIRED)

set(BL_EXPORT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/export)

# Export engine core implementation
add_library(bl_export STATIC
    export_engine.cpp
    muxer.cpp
    mkv_muxer.cpp
    batch_queue.cpp
    edl_exporter.cpp
)
set_property(TARGET bl_export PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(bl_export PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/bl_plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ui
)

target_link_libraries(bl_export PUBLIC bl_ffmpeg_plugin_common FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample nlohmann_json::nlohmann_json)
if(NOT MSVC)
    target_compile_options(bl_export PRIVATE -Wall -Wextra)
endif()

# Export engine depends on codec plugins being built
if(BL_BUILD_PLUGINS)
    add_dependencies(bl_export bl_codec_plugins)
endif()

# Stage the export shared library
set_target_properties(bl_export PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BL_EXPORT_OUTPUT_DIR}
    OUTPUT_NAME "bl_export"
    PREFIX "bl"
)