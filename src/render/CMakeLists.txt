add_library(bl_render STATIC
    effect.cpp
    frame_cache.cpp
    compositor.cpp
    media_decode_source.cpp
    preview_engine.cpp
)

add_library(bl::render ALIAS bl_render)

target_include_directories(bl_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(bl_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bl_render PUBLIC cxx_std_17)

target_link_libraries(bl_render PUBLIC bl_core bl_timeline nlohmann_json::nlohmann_json)

bl_enable_warnings(bl_render)
