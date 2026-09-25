# Bucharest Lite — Open-Source NLE

![Bucharest Lite Logo](https://raw.githubusercontent.com/your-repo/bucharest-lite/main/docs/images/logo.png 200x200)

A cross-platform, open-source (GPL-3.0) non-linear video editor built with **Qt6/C++17**, **FFmpeg**, and **OpenGL** rendering.

## 🚀 Features

Bucharest Lite implements 20 essential NLE features as a shippable v1.0 release:

| Category | Features |
|---|---|
| **Core Editing** | Multi-track timeline, drag-and-drop import, trimming, splitting, ripple editing, 3-point editing, snapping, zoom/pan, frame-accurate step editing, speed/time remapping, keyframeable properties |
| **Transitions & Effects** | Fade, wipe, dissolve, crossfade, keyframeable effects (color, blur, sharpen, distortion), video filters (brightness, contrast, gamma, hue, saturation, greyscale, chroma key), picture-in-picture |
| **Compositing** | Layer-based alpha blending, chroma key, masking, overlays, watermarks |
| **Titles & Text** | Built-in title editor, title templates (scrolling credits, lower thirds), SVG vector titles, animated titles (scroll, typewriter), subtitle support (SRT, ASS, WebVTT) |
| **Audio** | Multi-track mixing, waveform display, per-channel control, audio level meters, audio syncing tools |
| **Color Grading** | Color correction (wheels, curves), LUT support (3D LUT .cube), node-based grading (v2) |
| **Media & Codecs** | FFmpeg-based format support (H.264, VP9, AV1, Theora, MPEG-4), image sequence import, audio codecs (MP3, WAV, AAC, FLAC, OGG, Opus), proxy editing, format detection and metadata |
| **Export & Rendering** | Custom export profiles, batch rendering, presets (YouTube, Vimeo, DVD, Blu-ray, mobile), quality measurement (PSNR, SSIM), stream/encode to IP |
| **UI & Workflow** | Customizable interface, keyboard shortcuts (remappable), multi-monitor support, dark/light themes, unlimited undo/redo, media bin search/filter, project templates |
| **Advanced** | Multi-cam editing (v2), motion tracking (v2), video stabilization (v2), 8K support (v2), online resource integration (v2), plugin architecture (frei0r, OpenFX, LADSPA), portable/standalone builds |

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
│ domain model │  compositor, GL    │  mixer, FX, waveforms  │
│ edit ops     │  backends, cache   │  clocks, meters        │
├──────────────┴─────────┬──────────┴────────────────────────┤
│      bl_export         │            bl_core                │
│ batch queue, presets   │  time, project model, plugin      │
│ encoder/muxer chain    │  loader, codec registry, media    │
├────────────────────────┴───────────────────────────────────┤
│              bl_plugins  (header-only C ABI)               │
│        the stable contract implemented by .so/.dll plugins │
├────────────────────────────────────────────────────────────┤
│      Qt6 · FFmpeg (libav*) · OpenGL · nlohmann/json        │
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

## 🧪 Testing

**732 tests passing** across three build configurations:

- **Debug**: 308 core + 424 UI tests green
- **Release**: 308 core + 424 UI tests green  
- **TSAN (ThreadSanitizer)**: 308 core + 358 UI tests green (UI TSAN tests excluded from preset due to Qt6-internal allocator races)

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
| GUI | desktop 3.3 | WGL 3.3 | CGL 3.3 (Metal via MoltenVK later) |
| FFmpeg | distro packages | vendored shared builds | homebrew/vendored |
| CI | GitHub Actions ubuntu | windows-latest | macos-latest |
| Packaging | AppImage + deb | NSIS + portable zip | — |

Platform-specific code is confined to `src/core/platform/` (paths, dynamic libraries, high-res timers).

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

**Version 1.0.1** — September 2026

*Built with passion by the open-source community.*