# FindFFmpeg.cmake — locate FFmpeg (libavformat/avcodec/avutil/swscale/swresample)
#
# Provides imported targets:
#   FFmpeg::avformat  FFmpeg::avcodec  FFmpeg::avutil
#   FFmpeg::swscale   FFmpeg::swresample
# plus the FFMPEG_ROOT directory hint (env var or -DFFMPEG_ROOT).
#
# Resolution order:
#   1. pkg-config (Linux/macOS/CI, mirrors historical behaviour)
#   2. Manual <root>/include + <root>/lib layout, root from -DFFMPEG_ROOT
#      or $env:FFMPEG_ROOT (used for Windows shared builds)
#
# On success this module sets FFmpeg_FOUND=TRUE and FFMPEG_BIN_DIR to the
# runtime DLL/shared-library directory (used for installer staging).

set(FFmpeg_FOUND FALSE)

if(NOT DEFINED FFMPEG_ROOT AND DEFINED ENV{FFMPEG_ROOT})
    set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT DEFINED FFMPEG_SKIP_PKGCONFIG)
    pkg_check_modules(BL_FFMPEG QUIET IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
        libswresample
    )
endif()

if(BL_FFMPEG_FOUND AND TARGET PkgConfig::BL_FFMPEG)
    foreach(_mod avformat avcodec avutil swscale swresample)
        if(NOT TARGET FFmpeg::${_mod})
            add_library(FFmpeg::${_mod} INTERFACE IMPORTED)
        endif()
        set_property(TARGET FFmpeg::${_mod} PROPERTY
            INTERFACE_LINK_LIBRARIES PkgConfig::BL_FFMPEG)
    endforeach()
    set(FFmpeg_FOUND TRUE)
    return()
endif()

if(NOT FFMPEG_ROOT)
    message(FATAL_ERROR
        "FindFFmpeg: FFmpeg not found via pkg-config and -DFFMPEG_ROOT is "
        "unset. Point FFMPEG_ROOT at an FFmpeg shared build (include/ + lib/).")
endif()

set(_ffmpeg_inc "${FFMPEG_ROOT}/include")
set(_ffmpeg_lib "${FFMPEG_ROOT}/lib")
set(_ffmpeg_bin "${FFMPEG_ROOT}/bin")

if(NOT EXISTS "${_ffmpeg_inc}/libavutil/avutil.h")
    message(FATAL_ERROR "FFmpeg headers not found at ${_ffmpeg_inc}")
endif()

foreach(_mod avformat avcodec avutil swscale swresample)
    if(NOT TARGET FFmpeg::${_mod})
        add_library(FFmpeg::${_mod} UNKNOWN IMPORTED)
    endif()
    find_library(FFmpeg_${_mod}_LIBRARY
        NAMES ${_mod}
        HINTS "${_ffmpeg_lib}"
        NO_DEFAULT_PATH
    )
    if(NOT FFmpeg_${_mod}_LIBRARY)
        message(FATAL_ERROR "FindFFmpeg: ${_mod} import library not found in ${_ffmpeg_lib}")
    endif()
    set_target_properties(FFmpeg::${_mod} PROPERTIES
        IMPORTED_LOCATION "${FFmpeg_${_mod}_LIBRARY}")
    set_property(TARGET FFmpeg::${_mod} APPEND PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_inc}")
endforeach()

set(FFMPEG_BIN_DIR "${_ffmpeg_bin}" CACHE PATH
    "Directory holding the FFmpeg runtime shared libraries")
set(FFmpeg_FOUND TRUE)