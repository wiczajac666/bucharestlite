add_library(bl_timeline STATIC
    clip.cpp
    keyframes.cpp
    track.cpp
    sequence.cpp
    timeline.cpp
)

target_include_directories(bl_timeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(bl_timeline PUBLIC bl_core nlohmann_json::nlohmann_json)

bl_enable_warnings(bl_timeline)
