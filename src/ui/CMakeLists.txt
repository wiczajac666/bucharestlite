find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(bl_ui STATIC
    app/theme_manager.cpp
    app/project_controller.cpp
    app/main_window.cpp
    app/autosave_manager.cpp
    panels/timeline_panel.cpp
    panels/timeline_items.cpp
    panels/timeline_edit_controller.cpp
    panels/clip_edit_controller.cpp
    panels/preview_panel.cpp
    transport/transport_controller.cpp
    panels/media_bin_panel.cpp
    panels/inspector_panel.cpp
    panels/mixer_controller.cpp
    panels/mixer_panel.cpp
    panels/master_fader_panel.cpp
    export/export_settings.cpp
    export/export_plan.cpp
    export/export_runner.cpp
    export/export_worker.cpp
    export/export_queue.cpp
    dialogs/export_dialog.cpp
    dialogs/export_progress_dialog.cpp
    dialogs/batch_export_dialog.cpp
)

if(TARGET bl_export)
    target_link_libraries(bl_ui PUBLIC bl_export)
endif()
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
set_target_properties(bl_lite PROPERTIES AUTOMOC ON OUTPUT_NAME bucharest-lite)
bl_enable_warnings(bl_lite)

install(TARGETS bl_lite RUNTIME DESTINATION bin)