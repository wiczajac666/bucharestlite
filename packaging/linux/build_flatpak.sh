#!/usr/bin/env bash
# Build a bucharest-lite Flatpak bundle without requiring flatpak-builder.
#
# This replicates the steps flatpak-builder would run for bucharest-lite-app.json
# using the low-level `flatpak build-*` subcommands (flatpak-builder is a plain
# wrapper around these). It is the driver used to produce and verify
# packaging/flatpak/bucharest-lite-app.flatpak.
#
# Requires: flatpak with flathub remote available (runtime + SDK are fetched
# with --user, no root needed). Network access for flathub and ffmpeg.org.
set -euo pipefail

APP_ID="com.github.harlemi.bucharestlite"
RUNTIME="org.kde.Platform"
SDK="org.kde.Sdk"
BRANCH="${FLATPAK_BRANCH:-6.8}"
ARCH="$(flatpak --default-arch)"
FFMPEG_VERSION="8.0.1"
FFMPEG_SHA256="05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$ROOT/packaging/flatpak"
BUNDLE="$OUT_DIR/bucharest-lite-app.flatpak"
WORK="$(mktemp -d "${TMPDIR:-/tmp}"/bl-flatpak.XXXXXX)"
BUILD_DIR="$WORK/build"
REPO_DIR="$WORK/repo"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

log() { echo "==> $*"; }

flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
log "Fetching $RUNTIME//$BRANCH + $SDK//$BRANCH (large first run)"
flatpak install --user -y flathub "$RUNTIME//$BRANCH" "$SDK//$BRANCH"

flatpak build-init "$BUILD_DIR" "$APP_ID" "$SDK" "$RUNTIME" "$BRANCH"

mkdir -p "$OUT_DIR"
log "Staging FFmpeg $FFMPEG_VERSION source"
mkdir -p "$ROOT/.flatpak-build"
cd "$ROOT/.flatpak-build"
curl -sSL -o "ffmpeg-$FFMPEG_VERSION.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
echo "$FFMPEG_SHA256  ffmpeg-$FFMPEG_VERSION.tar.xz" | sha256sum -c -
tar xf "ffmpeg-$FFMPEG_VERSION.tar.xz"

log "Configure + build + install FFmpeg into the sandbox payload"
flatpak build "$BUILD_DIR" sh -c 'cd "'"$ROOT"'"/.flatpak-build/ffmpeg-'"$FFMPEG_VERSION"' && \
    ./configure --prefix=/app --disable-doc --disable-programs --disable-debug \
        --enable-shared --disable-static --enable-gpl --enable-libaom \
        --enable-libopus --enable-libtheora --enable-version3 --enable-libvorbis && \
    make -j"$(nproc)" && make install'

log "Configure, build and install bucharest-lite against the bundle FFmpeg"
flatpak build "$BUILD_DIR" sh -c 'export PKG_CONFIG_PATH=/app/lib/pkgconfig; cd "'"$ROOT"'" && \
    cmake -S . -B .flatpak-build/blapp -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/app -DCMAKE_PREFIX_PATH=/usr \
        -DBL_BUILD_TESTS=OFF -DBL_BUILD_UI=ON -DBL_BUILD_PLUGINS=ON -DBL_BUILD_EXPORT=ON && \
    cmake --build .flatpak-build/blapp -j"$(nproc)" && \
    cmake --install .flatpak-build/blapp'

log "Rename desktop + icons to the app-id so flatpak exports them"
flatpak build "$BUILD_DIR" sh -c 'cd /app/share/applications && \
    cp bucharest-lite.desktop com.github.harlemi.bucharestlite.desktop && \
    sed -i "s/^Icon=bucharest-lite/Icon=com.github.harlemi.bucharestlite/" com.github.harlemi.bucharestlite.desktop && \
    cd /app/share/icons/hicolor && \
    for s in 16 32 48 64 128 256 512; do \
      cp "${s}x${s}/apps/bucharest-lite.png" "${s}x${s}/apps/com.github.harlemi.bucharestlite.png"; \
    done && \
    cp scalable/apps/bucharest-lite.svg scalable/apps/com.github.harlemi.bucharestlite.svg'

log "Finalize, export, install and smoke-test"
flatpak build-finish "$BUILD_DIR" --command=bucharest-lite \
    --share=ipc --socket=x11 --socket=wayland --device=dri \
    --talk-name=org.freedesktop.Notifications
flatpak build-export "$REPO_DIR" "$BUILD_DIR" main
flatpak build-bundle "$REPO_DIR" "$BUNDLE" "$APP_ID" main

flatpak install --user -y "$BUNDLE" >/dev/null
timeout 6 flatpak run --user --env=QT_QPA_PLATFORM=offscreen "$APP_ID"
rc=$?
[ "$rc" -eq 124 ] && log "Smoke OK: app ran until timeout (healthy event loop)" \
                 || { log "Smoke FAILED with exit $rc"; exit 1; }

log "Bundle: $BUNDLE"
du -h "$BUNDLE"