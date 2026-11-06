add_library(bl_audio STATIC
    track_strip.cpp
    mixer.cpp
    audio_engine.cpp
    device_output.cpp
    device_output_alsa.cpp
    device_output_winmm.cpp
    audio_meter_analysis.cpp
    audio_meter_engine.cpp
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

if(UNIX AND NOT APPLE)
    # device_output_alsa.cpp loads libasound with dlopen at runtime.
    target_link_libraries(bl_audio PUBLIC ${CMAKE_DL_LIBS})
endif()

bl_enable_warnings(bl_audio)
