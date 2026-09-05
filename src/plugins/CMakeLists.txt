# Codec plugins (Phase 1 group: PLG-1..PLG-9).
#
# Each plugin is a dlopen-loadable MODULE that exports bl_get_codec_plugin().
# The ffmpeg-backed plugins share a common static library that acts as a
# thin wrapper over libavcodec/libswscale/libswresample.

if(NOT BL_BUILD_PLUGINS)
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(BL_FFMPEG_PLUGINS REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

set(BL_PLUGIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/plugins)
set(BL_PLUGIN_VIDEO_DIR ${BL_PLUGIN_OUTPUT_DIR}/video)
set(BL_PLUGIN_AUDIO_DIR ${BL_PLUGIN_OUTPUT_DIR}/audio)

add_library(bl_ffmpeg_plugin_common STATIC
    ffmpeg/ffmpeg_plugin_common.c
)
target_include_directories(bl_ffmpeg_plugin_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(bl_ffmpeg_plugin_common PUBLIC PkgConfig::BL_FFMPEG_PLUGINS)
target_compile_options(bl_ffmpeg_plugin_common PRIVATE -Wall -Wextra)

# PLG-1 reference pair first; PLG-2..PLG-9 are added by the later commits
# in this group.
set(BL_PLUGIN_SOURCES
    video.h264:h264
    audio.aac:aac
)

function(bl_add_codec_plugin rel_path output_name)
    string(REPLACE "." "/" rel_dirs ${rel_path})
    add_library(${output_name} MODULE ${rel_dirs}_plugin.c)
    target_include_directories(${output_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg
    )
    target_link_libraries(${output_name} PRIVATE bl_ffmpeg_plugin_common)
    target_compile_options(${output_name} PRIVATE -Wall -Wextra)
    get_filename_component(type_dir ${rel_dirs} DIRECTORY)
    set_target_properties(${output_name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${type_dir}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${type_dir}
        OUTPUT_NAME ${output_name}
        PREFIX "bl"
    )
endfunction()

foreach(spec IN LISTS BL_PLUGIN_SOURCES)
    string(REPLACE ":" ";" spec_parts ${spec})
    list(GET spec_parts 0 rel_path)
    list(GET spec_parts 1 output_name)
    bl_add_codec_plugin(${rel_path} ${output_name})
endforeach()

# Stage every plugin MODULE into the canonical layout:
#   <build>/plugins/video/<output_name>.so
#   <build>/plugins/audio/<output_name>.so
add_custom_target(bl_codec_plugins ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BL_PLUGIN_VIDEO_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BL_PLUGIN_AUDIO_DIR}
)

foreach(spec IN LISTS BL_PLUGIN_SOURCES)
    string(REPLACE ":" ";" spec_parts ${spec})
    list(GET spec_parts 0 rel_path)
    list(GET spec_parts 1 output_name)
    string(REPLACE "." "/" rel_dirs ${rel_path})
    get_filename_component(type_dir ${rel_dirs} DIRECTORY)
    add_custom_command(TARGET ${output_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BL_PLUGIN_OUTPUT_DIR}/${type_dir}
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:${output_name}>
                ${BL_PLUGIN_OUTPUT_DIR}/${type_dir}/
        COMMENT "Staging ${output_name} into plugins/${type_dir}"
        VERBATIM
    )
    add_dependencies(bl_codec_plugins ${output_name})
endforeach()