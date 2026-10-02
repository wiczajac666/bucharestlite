# Bucharest Lite — Open-Source NLE

![Bucharest Lite Logo](https://raw.githubusercontent.com/your-repo/bucharest-lite/main/docs/images/logo.png 200x200)

A cross-platform, open-source (GPL-3.0) non-linear video editor built with **Qt6/C++17** and **FFmpeg** (CPU compositor; GPU-accelerated rendering is planned, not yet shipped).

## 🚀 Features

Bucharest Lite implements the features below. Items marked **planned** are
documented gaps, not shipped functionality (see `docs/user-guide.md` →
"Known limitations").

| Category | Features (v1.1.1) |
|---|---|
| **Core Editing** | Multi-track timeline ✅, drag-and-drop import (media bin → timeline) ✅, trimming/splitting/ripple editing ✅, 3-point editing ✅, snapping ✅, zoom ✅, frame-accurate step editing ✅, speed/time remapping + reverse ✅, keyframeable properties ✅ |
| **Transitions & Effects** | Keyframeable video effects — color correction, box blur, chroma key, sharpen, hue/saturation, levels/crop, greyscale, transform ✅; effect params as sliders ✅; transition *model* (fade/wipe/dissolve edge-metadata) ✅, transition rendering in preview/export *planned* |
| **Compositing** | Layer-based alpha blending (premultiplied) ✅, chroma key ✅, picture-in-picture via opacity/position keyframes ✅; masking/roto *planned* |
| **Titles & Text** | Per-clip soft subtitles (Inspector text → MOV_TEXT stream in MP4) ✅; SRT/ASS/WebVTT import *planned*; title editor, scrolling credits, lower thirds, SVG/animated titles *planned* |
| **Audio** | Multi-track mixer with gain/pan/M/S ✅, live L/R peak-dB meters ✅, audio export mixdown (AAC/FLAC/Opus/Vorbis) ✅; waveform display, audio syncing tools *planned* |
| **Color Grading** | Brightness/contrast/gamma color correction ✅; color wheels/curves, 3D LUT (`.cube`) import, scopes *planned* |
| **Media & Codecs** | 9 FFmpeg-backed codec plugins (H.264, VP9, AV1, Theora, MPEG-4, AAC, FLAC, Vorbis, Opus) ✅, format detection + metadata probe ✅, media-bin thumbnails + metadata ✅, batch export ✅, Matroska import ✅; Matroska export, image-sequence import, proxy editing *planned* |
| **Export & Rendering** | Custom export settings, MP4 + WebM, batch queue with persistence ✅; render presets (YouTube/Vimeo/DVD/Blu-ray/mobile), PSNR/SSIM, stream-to-IP, Matroska, per-clip/per-range export *planned* |
| **UI & Workflow** | Dockable layouts ✅, remappable keyboard shortcuts ✅, dark/light themes ✅, unlimited undo/redo ✅, media-bin search/filter ✅, project auto-save + crash recovery ✅; multi-monitor, project templates, bin folders/collections *planned* |
| **Advanced** | Portable/standalone builds (Windows portable zip, Flatpak) ✅; multi-cam, motion tracking, stabilization, 8K, online resources, OpenFX/frei0r/LADSPA plugin architecture — v2+ |

## 📦 Quick Start

### Building from Source

```bash
# Clone the repository
git clone https://github.com/your-repo/bucharest-lite.git
cd bucharest-lite

# Create build directory and configure
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBL_BUILD_TESTS=ON -DBL_BUILD_PLUGINS=ON -DBL_BUILD_UI=ON

# Build
make -j$(nproc)

# Run
./bucharest-lite
```

### Build Options

| Option | Default | Description |
|---|---|---|
| `BL_BUILD_TESTS` | ON | Build unit tests (Google Test) |
| `BL_ENABLE_TSAN` | OFF | Enable ThreadSanitizer |
| `BL_BUILD_PLUGINS` | ON | Build dlopen-loadable codec plugins |
| `BL_BUILD_EXPORT` | OFF | Build export pipeline (bl_export) |
| `BL_BUILD_UI` | ON | Build Qt6 GUI (bl_ui) |

## 🏗️ Architecture

Bucharest Lite uses a **layered architecture** with a stable plugin ABI as the contract between the application and loadable codec modules:

```
┌────────────────────────────────────────────────────────────┐
│                       bl_ui (Qt6)                          │
│   Main window, timeline, preview, bins, inspector, panels  │
├──────────────┬────────────────────┬────────────────────────┤
│ bl_timeline  │    bl_render       │      bl_audio          │
│ domain model │  compositor (CPU)  │  mixer, FX, meters     │
│ edit ops     │  cache, preview    │  clocks, analysis      │
├──────────────┴─────────┬──────────┴────────────────────────┤
│      bl_export         │            bl_core                │
│ batch queue, presets   │  time, project model, plugin      │
│ encoder/muxer chain    │  loader, codec registry, media    │
├────────────────────────┴───────────────────────────────────┤
│              bl_plugins  (header-only C ABI)               │
│        the stable contract implemented by .so/.dll plugins │
├────────────────────────────────────────────────────────────┤
│      Qt6 · FFmpeg (libav*) · nlohmann/json                 │
└────────────────────────────────────────────────────────────┘
```

Key principle: **preview and export share one composition path** — `Compositor::renderFrame()` is used by both the interactive preview engine and the export frame server.

## 📦 Codec Plugins

Bucharest Lite ships with **9 FFmpeg-backed codec plugins** dynamically loaded at runtime:

| Plugin | Video Codec | Audio Codec | Notes |
|---|---|---|---|
| PLG-1 | h264 (libx264) | — | CRF+preset params |
| PLG-2 | vp9 (libvpx-vp9) | — | Row-MT threading |
| PLG-3 | av1 (libsvtav1/libaom) | — | SVT preset ladder |
| PLG-4 | theora (libtheora) | — | Legacy web |
| PLG-5 | mpeg4 | — | Simple profile |
| PLG-6 | — | aac (native) | FDK AAC incompatible with GPL |
| PLG-7 | — | flac | Lossless |
| PLG-8 | — | vorbis | |
| PLG-9 | — | opus | |

Plus built-in **passthrough** pseudo-plugins for stream-copy.

Each plugin is ABI v2, dynamically loaded via `dlopen`, and follows the `BlCodecPlugin*` entry point contract.

## 📄 Documentation

- [FEATURES.md](FEATURES.md) — Complete feature list & specification
- [ORCHESTRATOR.md](ORCHESTRATOR.md) — Multi-agent orchestration instructions
- [ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md) — System architecture & module specs
- [PROJECT_STATUS.md](PROJECT_STATUS.md) — Current project status & test counts
- [Developer Guide](docs/developer-guide.md) — Codec plugin development guide
- [User Guide](docs/user-guide.md) — Export workflow & troubleshooting
- [CHANGELOG.md](CHANGELOG.md) — Release history

## ✨ What's New in 1.1.1

- **Correct alpha compositing** — per-pixel premultiplied alpha blending so
  faded clips, opacity keyframes, and stacked layers combine over track content
  beneath them.
- **Five new video effects** — Chroma Key, Sharpen, Hue/Saturation,
  Levels/Curves, and Crop (built-in catalog grows from 4 to 9), each with
  slider-driven parameters.
- **Speed editing & reverse playback** — per-clip fractional speed plus a
  *Reverse* toggle; the compositor and the audio mixdown share one source-time
  mapping so reversed clips play sample-truthfully, and changes are undoable.
- **Soft subtitles** — subtitle text per clip edited in the Inspector and
  exported as a real MOV_TEXT subtitle stream in MP4 (never burned in).

See [CHANGELOG.md](CHANGELOG.md) for the full history.

## 🧪 Testing

**553 tests passing across debug and release builds (371 core + 182 UI):**

- **Debug**: 553 tests green
- **Release**: 553 tests green
- **TSAN (ThreadSanitizer)**: core tests green; UI TSAN tests excluded from the
  preset due to Qt6-internal allocator races

Run tests:
```bash
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
cmake --preset linux-release && cmake --build --preset linux-release && ctest --preset linux-release
cmake --preset linux-tsan && cmake --build --preset linux-tsan && ctest --preset linux-tsan
```

## 🎯 Platform Support

| Concern | Linux | Windows | macOS |
|---|---|---|---|
| Plugin suffix | `.so` | `.dll` | `.dylib` |
| Plugin dir | `/usr/share/...` + `~/.local/share/...` | `%APPDATA%\BucharestLite\plugins` | `~/Library/Application Support/...` |
| Rendering | CPU compositor (OpenGL planned) | CPU compositor (OpenGL planned) | — (packaging out of scope) |
| FFmpeg | distro packages | vendored shared builds | — |
| CI | GitHub Actions ubuntu ✓ | GitHub Actions windows-latest ✓ | — |
| Packaging | deb + Flatpak | NSIS + portable zip | — |

Platform-specific code is confined to `src/core/platform/` (paths, dynamic libraries, high-res timers).

The Linux build needs distro **FFmpeg 8** or newer — the codec plugins use FFmpeg 8 APIs
(`SwsContext` became opaque, `avcodec_get_supported_config`, `AV_CODEC_CONFIG_*`). Ubuntu 24.04
ships FFmpeg 6.1 and will not compile; Ubuntu 26.04 ships 8.0.1 and is what CI pins. Windows CI
builds with MSVC (`cl`), never MinGW, matching the toolchain the releases are built with.

## 📜 Licensing

Bucharest Lite is licensed under **GPL-3.0-or-later**.

- FFmpeg must be built `--enable-gPL` (x264/x265/SVT-AV1 require it)
- Distribution bundles carry license notices
- Third-party notices maintained in `THIRD_PARTY_NOTICES.md`
- Qt6 open-source licensing (LGPLv3) satisfied via dynamic linking

## 🛠️ Development

Adding new codecs follows the plugin-based architecture — no core modifications needed:

1. Create shared library in `src/plugins/video/` or `src/plugins/audio/`
2. Export `bl_get_codec_plugin()` with ABI v2
3. Plugin is automatically discovered at runtime by scanning system and user plugin directories
4. Provide `BlParamDesc` array for UI auto-generation of parameter editors

See [Developer Guide](docs/developer-guide.md) for full plugin development tutorial.

## 🖥️ Screenshots

*(Add screenshots of the UI here)*

![Timeline Panel](docs/images/timeline-panel.png)
![Export Dialog](docs/images/export-dialog.png)
![Inspector Panel](docs/images/inspector-panel.png)

## 🙏 Acknowledgments

- Inspired by Kdenlive, Shotcut, and OpenShot
- FFmpeg for media IO and encoding/decoding
- Qt6 for the cross-platform GUI framework
- nlohmann/json for JSON serialization
- Google Test for the test framework

---

**Version 1.1.1** — September 2026

*Built with passion by the open-source community.*