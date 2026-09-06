find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(bl_ui STATIC
    app/theme_manager.cpp
    app/project_controller.cpp
    app/main_window.cpp
    panels/timeline_panel.cpp
    panels/timeline_items.cpp
    panels/timeline_edit_controller.cpp
    panels/preview_panel.cpp
    transport/transport_controller.cpp
    panels/media_bin_panel.cpp
    panels/inspector_panel.cpp
    panels/mixer_panel.cpp
)
add_library(bl::ui ALIAS bl_ui)

target_include_directories(bl_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_include_directories(bl_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(bl_ui PROPERTIES AUTOMOC ON)

target_compile_features(bl_ui PUBLIC cxx_std_17)

target_link_libraries(bl_ui PUBLIC Qt6::Widgets bl::core bl_timeline bl::render)

bl_enable_warnings(bl_ui)

add_executable(bl_lite app/main.cpp)
target_link_libraries(bl_lite PRIVATE bl::ui Qt6::Widgets)
target_include_directories(bl_lite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(bl_lite PROPERTIES AUTOMOC ON)
bl_enable_warnings(bl_lite)