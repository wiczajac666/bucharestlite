include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(FFmpeg REQUIRED)

set(BL_MEDIA_STAGE ${CMAKE_CURRENT_BINARY_DIR}/media)

add_custom_command(
    OUTPUT ${BL_MEDIA_STAGE}/test_video.mp4
           ${BL_MEDIA_STAGE}/test_audio.wav
           ${BL_MEDIA_STAGE}/test_av.mp4
           ${BL_MEDIA_STAGE}/test_video.h264
           ${BL_MEDIA_STAGE}/test_audio_vorbis.ogg
           ${BL_MEDIA_STAGE}/test_audio_opus.opus
           ${BL_MEDIA_STAGE}/test_video_theora.ogv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BL_MEDIA_STAGE}
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=1:size=320x240:rate=24 -pix_fmt yuv420p ${BL_MEDIA_STAGE}/test_video.mp4
    COMMAND ffmpeg -y -loglevel error -f lavfi -i sine=frequency=440:duration=1 -ar 48000 -ac 2 ${BL_MEDIA_STAGE}/test_audio.wav
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=2:size=320x240:rate=24 -f lavfi -i sine=frequency=440:duration=2 -pix_fmt yuv420p -shortest ${BL_MEDIA_STAGE}/test_av.mp4
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=2:size=320x240:rate=24 -c:v libx264 -preset veryfast -bf 0 -pix_fmt yuv420p -f h264 ${BL_MEDIA_STAGE}/test_video.h264
    COMMAND ffmpeg -y -loglevel error -f lavfi -i sine=frequency=440:duration=1 -ar 48000 -ac 2 -c:a libvorbis ${BL_MEDIA_STAGE}/test_audio_vorbis.ogg
    COMMAND ffmpeg -y -loglevel error -f lavfi -i sine=frequency=440:duration=1 -ar 48000 -ac 2 -c:a libopus ${BL_MEDIA_STAGE}/test_audio_opus.opus
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=1:size=160x120:rate=24 -c:v libtheora ${BL_MEDIA_STAGE}/test_video_theora.ogv
    COMMENT "Generating synthetic media fixtures with ffmpeg CLI"
    VERBATIM
)

add_custom_target(bl_media_fixtures ALL
    DEPENDS ${BL_MEDIA_STAGE}/test_video.mp4
            ${BL_MEDIA_STAGE}/test_audio.wav
            ${BL_MEDIA_STAGE}/test_av.mp4
            ${BL_MEDIA_STAGE}/test_video.h264
            ${BL_MEDIA_STAGE}/test_audio_vorbis.ogg
            ${BL_MEDIA_STAGE}/test_audio_opus.opus
            ${BL_MEDIA_STAGE}/test_video_theora.ogv
)

if(WIN32 AND DEFINED FFMPEG_BIN_DIR)
    function(bl_stage_ffmpeg_runtime test_target)
        file(GLOB _ffmpeg_runtime_glob
            ${FFMPEG_BIN_DIR}/avcodec-*.dll
            ${FFMPEG_BIN_DIR}/avformat-*.dll
            ${FFMPEG_BIN_DIR}/avutil-*.dll
            ${FFMPEG_BIN_DIR}/swscale-*.dll
            ${FFMPEG_BIN_DIR}/swresample-*.dll
            ${FFMPEG_BIN_DIR}/libgcc_s_seh-1.dll
            ${FFMPEG_BIN_DIR}/libstdc++-6.dll
            ${FFMPEG_BIN_DIR}/libwinpthread-1.dll
        )
        if(NOT _ffmpeg_runtime_glob)
            message(FATAL_ERROR "No FFmpeg runtime DLLs matched in ${FFMPEG_BIN_DIR}")
        endif()
        set(_stage_cmds)
        foreach(_dll IN LISTS _ffmpeg_runtime_glob)
            list(APPEND _stage_cmds
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll}" $<TARGET_FILE_DIR:${test_target}>)
        endforeach()
        add_custom_command(TARGET ${test_target} POST_BUILD ${_stage_cmds}
            COMMENT "Staging FFmpeg runtime next to ${test_target}"
            VERBATIM)
    endfunction()
endif()

add_subdirectory(unit)

if(TARGET bl_ui)
    add_subdirectory(ui)
endif()