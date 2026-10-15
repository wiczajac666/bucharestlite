# Redistribution of Windows builds.
#
# This file is `include()`d from the top-level CMakeLists.txt on WIN32 only.

# Embed the Windows version resource (icon + version metadata) into the app.
#
# version.rc.in is configured rather than compiled as-is so the numeric
# FILEVERSION/PRODUCTVERSION can never drift from `project(... VERSION ...)`.
# build_installer.ps1 also reads windows-version.txt (written below) to name
# its artifacts, so the exe metadata, the installer filename, the uninstaller's
# DisplayVersion and the portable zip all carry the same number by construction.
set(BL_VERSION_BUILD 0
    CACHE STRING "Fourth (build) component of the Windows FILEVERSION")

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY)

if(TARGET bl_lite)
    target_sources(bl_lite PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

# Single-sourced version for build_installer.ps1, which must run after
# `cmake --preset` and so can read this instead of re-parsing CMakeLists.txt.
# Format is deliberately trivial (one bare version string) for the same reason.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/windows-version.txt "${PROJECT_VERSION}\n")

install(FILES
        ${CMAKE_CURRENT_LIST_DIR}/../../LICENSE
        DESTINATION doc/bucharest-lite)
install(FILES
        ${CMAKE_CURRENT_LIST_DIR}/../../THIRD_PARTY_NOTICES.md
        DESTINATION doc/bucharest-lite)