# Third-Party Notices

Bucharest Lite is licensed under GPL-3.0-or-later (see `LICENSE`). The
following third-party components are used and distributed alongside the
application or referenced at build time.

## Runtime components bundled with distributables

| Component | Purpose | License | Notes |
|---|---|---|---|
| Qt 6 (QtCore, QtGui, QtWidgets, QtMultimedia) | GUI framework | LGPLv3 | Dynamic linking; separate Qt decorations licensed LGPLv3 |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample) | Media demux/decode/encode | LGPLv2.1+^(b)^ | Bundles the codecs below |
| x264 | H.264/AVC encode | GPL-2.0+ | Requires the GPL FFmpeg build |
| libvpx | VP8/VP9 encode/decode | BSD-3-Clause | |
| SVT-AV1 | AV1 encode | MIT + BSD-2-Clause / BSD-3-Clause | |
| libtheora | Theora encode | BSD-3-Clause | |
| libvorbis | Vorbis encode | BSD-3-Clause | |
| libopus | Opus encode | BSD-3-Clause | |
| FLAC | FLAC encode | BSD-3-Clause | |

^(b)^ FFmpeg is built with `--enable-gpl` and includes the GPL codecs listed
above, so the FFmpeg binaries shipped here fall under GPL-2.0-or-later.

## Build-time / test dependencies (not distributed)

| Component | License |
|---|---|
| nlohmann/json (vendored at `third_party/`) | MIT |
| GoogleTest / GoogleMock | BSD-3-Clause |
| CMake, Ninja, NSIS (NSIS_mUI via Makensis) | BSD-3-Clause / zlib |

## Derived files

- `resources/icons/hicolor/*` and `packaging/windows/bucharest-lite.ico`:
  project artwork, GPL-3.0-or-later.

Full license texts for GPL-3.0 and the other licenses referenced above are
included in the distributable under `doc/bucharest-lite/` (GPL-3.0) and are
available from their respective projects.