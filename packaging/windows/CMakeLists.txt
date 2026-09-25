# Redistribution of Windows builds.
#
# This file is `include()`d from the top-level CMakeLists.txt on WIN32 only.

# Embed the Windows version resource (icon + version metadata) into the app.
if(TARGET bl_lite)
    target_sources(bl_lite PRIVATE ${CMAKE_CURRENT_LIST_DIR}/version.rc)
endif()

install(FILES
        ${CMAKE_CURRENT_LIST_DIR}/../../LICENSE
        DESTINATION doc/bucharest-lite)
install(FILES
        ${CMAKE_CURRENT_LIST_DIR}/../../THIRD_PARTY_NOTICES.md
        DESTINATION doc/bucharest-lite)