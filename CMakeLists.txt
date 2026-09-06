cmake_minimum_required(VERSION 3.20)

project(BucharestLite
    VERSION 0.1.0
    DESCRIPTION "Bucharest Lite - open-source non-linear video editor"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BL_BUILD_TESTS "Build unit tests" ON)
option(BL_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(BL_BUILD_PLUGINS "Build dlopen-loadable codec plugins" ON)
option(BL_BUILD_UI "Build Qt6 GUI (bl_ui)" ON)

if(BL_ENABLE_TSAN)
    if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        message(FATAL_ERROR "BL_ENABLE_TSAN requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)

FetchContent_MakeAvailable(nlohmann_json)

function(bl_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/timeline)
add_subdirectory(src/render)
add_subdirectory(src/audio)

if(BL_BUILD_PLUGINS)
    add_subdirectory(src/plugins)
endif()

if(BL_BUILD_UI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        add_subdirectory(src/ui)
    endif()
endif()

if(BL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
