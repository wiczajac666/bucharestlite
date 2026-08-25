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

function(bl_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_subdirectory(src/core)

if(BL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
