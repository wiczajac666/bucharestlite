add_library(bl_audio STATIC
    track_strip.cpp
    mixer.cpp
    audio_engine.cpp
)

add_library(bl::audio ALIAS bl_audio)

target_include_directories(bl_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(bl_audio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bl_audio PUBLIC cxx_std_17)

target_link_libraries(bl_audio PUBLIC bl_core bl_timeline)

bl_enable_warnings(bl_audio)
