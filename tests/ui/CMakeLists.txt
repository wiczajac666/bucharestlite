find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(bl_ui_tests
    test_project_controller.cpp
    test_main_window.cpp
    test_timeline_edit_controller.cpp
    test_clip_edit_controller.cpp
    test_timeline_panel.cpp
    test_transport_controller.cpp
    test_preview_panel.cpp
    test_media_bin_panel.cpp
    test_inspector_panel.cpp
)

target_link_libraries(bl_ui_tests PRIVATE bl::ui bl::core bl_timeline bl::render GTest::gtest GTest::gtest_main Qt6::Widgets Qt6::Test)

target_compile_features(bl_ui_tests PRIVATE cxx_std_17)

target_include_directories(bl_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui)

target_compile_definitions(bl_ui_tests PRIVATE
    BL_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:bl_ui_tests>"
    BL_TEST_MEDIA_DIR="$<TARGET_FILE_DIR:bl_ui_tests>/media"
)

bl_enable_warnings(bl_ui_tests)

include(GoogleTest)
gtest_discover_tests(bl_ui_tests DISCOVERY_TIMEOUT 120)

add_custom_command(TARGET bl_ui_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/../media
            $<TARGET_FILE_DIR:bl_ui_tests>/media
    COMMENT "Copying media fixtures next to test binary"
    VERBATIM
)

if(BL_BUILD_PLUGINS)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/plugins-copied.stamp
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/plugins
                $<TARGET_FILE_DIR:bl_ui_tests>/plugins
        COMMAND ${CMAKE_COMMAND} -E touch
                ${CMAKE_CURRENT_BINARY_DIR}/plugins-copied.stamp
        DEPENDS h264 vp9 av1 theora mpeg4 aac flac vorbis opus
        COMMENT "Copying codec plugins next to UI test binary"
        VERBATIM
    )
    add_custom_target(bl_ui_tests_plugins ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/plugins-copied.stamp
    )
endif()