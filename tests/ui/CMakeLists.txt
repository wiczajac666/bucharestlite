find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(bl_ui_tests
    test_project_controller.cpp
    test_main_window.cpp
    test_timeline_edit_controller.cpp
    test_timeline_panel.cpp
)

target_link_libraries(bl_ui_tests PRIVATE bl::ui bl::core bl_timeline GTest::gtest GTest::gtest_main Qt6::Widgets Qt6::Test)

target_compile_features(bl_ui_tests PRIVATE cxx_std_17)

target_include_directories(bl_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui)

bl_enable_warnings(bl_ui_tests)

include(GoogleTest)
gtest_discover_tests(bl_ui_tests DISCOVERY_TIMEOUT 120)