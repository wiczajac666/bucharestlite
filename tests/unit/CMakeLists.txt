set(BL_FIXTURES
    good_plugin:fixturegood
    bad_abi_plugin:fixturebadabi
    noentry_plugin:fixturenoentry
)

foreach(fixture IN LISTS BL_FIXTURES)
    string(REPLACE ":" ";" fixture_pair ${fixture})
    list(GET fixture_pair 0 fixture_name)
    list(GET fixture_pair 1 fixture_output)
    add_library(${fixture_name} MODULE ../fixtures/${fixture_name}.c)
    target_include_directories(${fixture_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    )
    set_target_properties(${fixture_name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:bl_core_tests>/plugins
        RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:bl_core_tests>/plugins
        OUTPUT_NAME ${fixture_output}
        PREFIX "lib"
    )
    if(WIN32)
        set_property(TARGET ${fixture_name} PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endforeach()

add_executable(bl_core_tests
    test_time.cpp
    test_result.cpp
    test_logger.cpp
    test_undo_stack.cpp
    test_codec_registry.cpp
    test_plugin_loader.cpp
    test_builtin_plugins.cpp
    test_concurrency.cpp
    test_decoder_bridge.cpp
    test_effects.cpp
    test_frame_cache.cpp
    test_compositor.cpp
    test_audio_mixer.cpp
    test_audio_meter_analysis.cpp
    test_media_jobs.cpp
    test_persistence.cpp
    test_timeline.cpp
    test_codec_plugins.cpp
    test_preview_engine.cpp
    test_media_thumbnail.cpp
)

target_link_libraries(bl_core_tests PRIVATE bl_core bl_timeline bl_render bl_audio GTest::gtest_main)

target_include_directories(bl_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_compile_definitions(bl_core_tests PRIVATE
    BL_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:bl_core_tests>"
    BL_TEST_MEDIA_DIR="$<TARGET_FILE_DIR:bl_core_tests>/media"
)

if(WIN32)
    target_compile_definitions(bl_core_tests PRIVATE
        BL_DYNLIB_SUFFIX=".dll")
endif()

if(MSVC)
    target_compile_options(bl_core_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(bl_core_tests PRIVATE -Wall -Wextra)
endif()

include(GoogleTest)
gtest_discover_tests(bl_core_tests DISCOVERY_TIMEOUT 60)

# The POST_BUILD fixture copy below reads the staged media directory, so it must
# not run before bl_media_fixtures has produced it.
add_dependencies(bl_core_tests bl_media_fixtures)

add_custom_command(TARGET bl_core_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/../media
            $<TARGET_FILE_DIR:bl_core_tests>/media
    COMMENT "Copying media fixtures next to test binary"
    VERBATIM
)

if(BL_BUILD_PLUGINS)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/plugins-copied.stamp
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/plugins
                $<TARGET_FILE_DIR:bl_core_tests>/plugins
        COMMAND ${CMAKE_COMMAND} -E touch
                ${CMAKE_CURRENT_BINARY_DIR}/plugins-copied.stamp
        DEPENDS h264 vp9 av1 theora mpeg4 aac flac vorbis opus
        COMMENT "Copying codec plugins next to test binary"
        VERBATIM
    )
    add_custom_target(bl_core_tests_plugins ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/plugins-copied.stamp
    )
endif()

if(COMMAND bl_stage_ffmpeg_runtime)
    bl_stage_ffmpeg_runtime(bl_core_tests)
endif()
