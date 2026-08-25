add_executable(bl_core_tests
    test_time.cpp
    test_result.cpp
    test_logger.cpp
)

target_link_libraries(bl_core_tests PRIVATE bl_core GTest::gtest_main)

if(MSVC)
    target_compile_options(bl_core_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(bl_core_tests PRIVATE -Wall -Wextra)
endif()

include(GoogleTest)
gtest_discover_tests(bl_core_tests DISCOVERY_TIMEOUT 60)
