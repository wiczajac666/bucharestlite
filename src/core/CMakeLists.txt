add_library(bl_core STATIC
    time.cpp
    result.cpp
    logger.cpp
    plugin_validation.cpp
    plugin_loader.cpp
    codec_registry.cpp
    platform/dynamic_library.cpp
    platform/paths.cpp
)

add_library(bl::core ALIAS bl_core)

target_include_directories(bl_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(bl_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bl_core PUBLIC cxx_std_17)

if(NOT MSVC)
    target_link_libraries(bl_core PUBLIC ${CMAKE_DL_LIBS})
endif()

set_target_properties(bl_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

bl_enable_warnings(bl_core)
