include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BL_FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
)

set(BL_MEDIA_STAGE ${CMAKE_CURRENT_BINARY_DIR}/media)

add_custom_command(
    OUTPUT ${BL_MEDIA_STAGE}/test_video.mp4
           ${BL_MEDIA_STAGE}/test_audio.wav
           ${BL_MEDIA_STAGE}/test_av.mp4
           ${BL_MEDIA_STAGE}/test_video.h264
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BL_MEDIA_STAGE}
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=1:size=320x240:rate=24 -pix_fmt yuv420p ${BL_MEDIA_STAGE}/test_video.mp4
    COMMAND ffmpeg -y -loglevel error -f lavfi -i sine=frequency=440:duration=1 -ar 48000 -ac 2 ${BL_MEDIA_STAGE}/test_audio.wav
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=2:size=320x240:rate=24 -f lavfi -i sine=frequency=440:duration=2 -pix_fmt yuv420p -shortest ${BL_MEDIA_STAGE}/test_av.mp4
    COMMAND ffmpeg -y -loglevel error -f lavfi -i testsrc=duration=2:size=320x240:rate=24 -c:v libx264 -preset veryfast -bf 0 -pix_fmt yuv420p -f h264 ${BL_MEDIA_STAGE}/test_video.h264
    COMMENT "Generating synthetic media fixtures with ffmpeg CLI"
    VERBATIM
)

add_custom_target(bl_media_fixtures ALL
    DEPENDS ${BL_MEDIA_STAGE}/test_video.mp4
            ${BL_MEDIA_STAGE}/test_audio.wav
            ${BL_MEDIA_STAGE}/test_av.mp4
            ${BL_MEDIA_STAGE}/test_video.h264
)

add_subdirectory(unit)