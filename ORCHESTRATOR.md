# Bucharest Lite — Orchestrator Instructions
## Multi-Agent Setup for Building the NLE with OpenClaw + Opencode

---

## 1. Project Overview

**Project:** Bucharest Lite — Open-source non-linear video editor (NLE)
**License:** GPL-3.0
**Tech Stack:** Qt6 (C++) + FFmpeg + Vulkan/OpenGL rendering
**Target Platforms:** Linux, Windows, macOS
**Codebase Path:** `~/.openclaw/workspace/BucharestLite/`
**Use Opencode inside tmux window**
**Reference Docs:**
- `FEATURES.md` — Complete feature list + essential features + codec plugin list

---

## 2. Agent Roles

| Agent | Role | Responsibility |
|---|---|---|
| **Planner** | Architecture & Spec | Analyze requirements, design system architecture, create detailed specs, define module interfaces |
| **Builder-Backend** | Core Engine | Implement timeline engine, codec registry, media pipeline, rendering backend, audio engine |
| **Builder-Frontend** | UI/UX | Implement Qt6 GUI, timeline widget, preview window, settings panel, title editor |
| **Builder-Plugins** | Codec Plugins | Implement each codec plugin (H.264, VP9, AV1, Theora, MPEG-4, AAC, FLAC, Vorbis, Opus) |
| **Builder-Export** | Export & Rendering | Implement export pipeline, batch rendering, format presets, quality presets |
| **Tester** | QA & Integration | Write unit tests, integration tests, performance benchmarks, cross-platform validation |
| **Docs** | Documentation | Write API docs, user guide, developer guide, changelog |

---

## 3. Project Structure

```
BucharestLite/
├── FEATURES.md                    # Feature specification (already written)
├── ORCHESTRATOR.md                # This file
├── CMakeLists.txt                 # Root CMake configuration
├── include/
│   ├── bl_core/                   # Core engine headers
│   ├── bl_timeline/               # Timeline engine headers
│   ├── bl_render/                 # Rendering backend headers
│   ├── bl_audio/                  # Audio engine headers
│   ├── bl_ui/                     # UI framework headers
│   ├── bl_export/                 # Export pipeline headers
│   └── bl_plugins/                # Plugin API headers
├── src/
│   ├── core/                      # Core engine implementation
│   ├── timeline/                  # Timeline engine
│   ├── render/                    # Rendering backend
│   ├── audio/                     # Audio engine
│   ├── ui/                        # Qt6 GUI
│   ├── export/                    # Export pipeline
│   ├── plugins/                   # Codec plugins
│   │   ├── video/
│   │   │   ├── h264_plugin.cpp
│   │   │   ├── vp9_plugin.cpp
│   │   │   ├── av1_plugin.cpp
│   │   │   ├── theora_plugin.cpp
│   │   │   └── mpeg4_plugin.cpp
│   │   └── audio/
│   │       ├── aac_plugin.cpp
│   │       ├── flac_plugin.cpp
│   │       ├── vorbis_plugin.cpp
│   │       └── opus_plugin.cpp
│   └── main.cpp                   # Application entry point
├── tests/
│   ├── unit/                      # Unit tests (Google Test)
│   ├── integration/               # Integration tests
│   └── performance/               # Performance benchmarks
├── docs/
│   ├── api/                       # API documentation
│   ├── user-guide/                # User documentation
│   └── developer-guide/           # Developer documentation
├── resources/
│   ├── icons/                     # Application icons
│   ├── themes/                    # UI themes
│   └── translations/              # i18n files
└── scripts/                       # Build/CI scripts
```

---

## 4. Workflow — Plan → Build → Review → Test Loop

### Phase 0: Planning (Planner Agent)
1. Analyze `FEATURES.md` for all 20 essential features
2. Create `docs/architecture/ARCHITECTURE.md` with:
   - System architecture diagram
   - Module dependency graph
   - Data flow diagrams
   - Plugin API specification
   - Codec plugin interface spec
3. Define all module interfaces (headers + method signatures)
4. Break down each feature into implementation tasks
5. **Approval gate:** L Hustla reviews and approves architecture before any code is written

### Phase 1: Backend Core (Builder-Backend)
**Task Group 1: Core Engine**
- Implement `bl_core` module:
  - Plugin loader (dynamic library loading)
  - Codec registry (register/unregister codecs)
  - Media pipeline (decode → process → encode)
  - Error handling and logging
- Implement `bl_timeline` module:
  - Timeline data model (tracks, clips, regions)
  - 3-point editing logic
  - Ripple editing / snapping
  - Keyframe system
- Implement `bl_render` module:
  - Frame rendering pipeline
  - GPU acceleration (Vulkan/OpenGL)
  - Proxy rendering support
  - Real-time preview engine

**Task Group 2: Audio Engine**
- Implement `bl_audio` module:
  - Multi-track audio mixer
  - Audio effects pipeline (EQ, compression, reverb)
  - Waveform generation
  - Audio level meters
  - Audio/video sync engine

**Task Group 3: Codec Plugins**
- Implement each codec plugin following the plugin API:
  - `h264_plugin` — H.264 (AVC) encode/decode
  - `vp9_plugin` — VP9 encode/decode
  - `av1_plugin` — AV1 encode/decode
  - `theora_plugin` — Theora encode/decode
  - `mpeg4_plugin` — MPEG-4 Part 2 encode/decode
  - `aac_plugin` — AAC audio encode/decode
  - `flac_plugin` — FLAC audio decode
  - `vorbis_plugin` — Vorbis audio decode
  - `opus_plugin` — Opus audio decode

Each plugin must implement:
```cpp
struct CodecPlugin {
    const char* name;
    const char* description;
    CodecType type; // VIDEO or AUDIO
    bool (*init)(void** context);
    int (*decode)(void* context, const uint8_t* input, size_t input_size, uint8_t** output, size_t* output_size);
    int (*encode)(void* context, const uint8_t* input, size_t input_size, uint8_t** output, size_t* output_size);
    void (*cleanup)(void* context);
};
CodecPlugin* get_codec_plugin(); // Exported symbol
```

### Phase 2: Frontend UI (Builder-Frontend)
**Task Group 4: Qt6 GUI**
- Implement `bl_ui` module:
  - Main window with customizable layout
  - Timeline widget (multi-track, zoom/pan, snapping)
  - Preview window (video playback, scopes)
  - Media bin (drag-and-drop, search, filter)
  - Title editor (text, fonts, colors, animation)
  - Settings panel (codec config, preferences)
  - Color correction panel (wheels, curves, LUT)
  - Audio mixer panel (waveform, meters, levels)
  - Export dialog (format selection, presets, batch)

### Phase 3: Export Pipeline (Builder-Export)
**Task Group 5: Export & Rendering**
- Implement `bl_export` module:
  - Export engine (uses codec plugins)
  - Format presets (YouTube, Vimeo, DVD, Blu-ray, mobile)
  - Quality presets (low, medium, high, custom)
  - Batch rendering queue
  - Export progress tracking
  - Stream/encode to IP support

### Phase 4: Testing (Tester)
**Task Group 6: QA & Integration**
- Write unit tests for all modules (Google Test framework)
- Write integration tests (codec loading, timeline operations, export)
- Performance benchmarks (render speed, memory usage)
- Cross-platform validation (Linux, Windows, macOS)
- Automated CI tests

### Phase 5: Documentation (Docs)
**Task Group 7: Documentation**
- API documentation (Doxygen)
- User guide (Markdown + PDF export)
- Developer guide (architecture, plugin development, contribution)
- CHANGELOG.md
- README.md

---

## 5. Opencode Integration

### How to Use OpenClaw to Orchestrate

1. **Start with the Planner agent**
   ```
   /agents → Select "Plan"
   /sessions → Open or create BucharestLite session
   ```

2. **Review the architecture spec**
   - Ask Planner to analyze `FEATURES.md`
   - Request detailed architecture breakdown
   - Approve before code generation

3. **Switch to Build agents**
   ```
   /agents → Select "Build"
   ```
   - Assign tasks to specific builder agents
   - Each builder works on their task group independently
   - Use `/tasks` to track progress

4. **Coordinate between builders**
   - Builders must follow the shared interfaces defined by Planner
   - Use `/sessions` to communicate between builder sessions
   - Resolve interface conflicts before merging

5. **Review and iterate**
   - Review code output from each builder
   - If issues found → switch back to Plan → revise spec → switch back to Build
   - Repeat until all features are implemented

### Agent Assignment Pattern

For each task group, spawn a sub-agent:
```json
{
  "taskName": "bucharest-lite-builder-backend",
  "task": "Implement core engine, timeline, render, and audio modules",
  "context": "fork",
  "runtime": "subagent"
}
```

### Approval Gates

- **Gate 1:** Architecture spec approved by L Hustla → proceed to Backend
- **Gate 2:** Backend module interfaces defined → proceed to Frontend
- **Gate 3:** All code passes unit tests → proceed to Export
- **Gate 4:** Export pipeline functional → proceed to QA
- **Gate 5:** All tests pass → proceed to Docs
- **Gate 6:** Docs complete → release candidate

---

## 6. Build Process

### Prerequisites
- CMake 3.20+
- Qt6 (QtBase, QtMultimedia, QtSvg)
- FFmpeg (development libraries)
- Vulkan SDK or OpenGL 4.5+
- Google Test (for testing)
- C++17 compiler (GCC 11+, Clang 14+, MSVC 2019+)

### Build Commands
```bash
cd BucharestLite
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./bucharest-lite
```

### Plugin Loading
Plugins are loaded dynamically from `~/.local/share/bucharest-lite/plugins/` at runtime. Each plugin directory contains:
```
plugins/
├── video/
│   ├── libh264_plugin.so    # Linux
│   ├── h264_plugin.dll      # Windows
│   └── libh264_plugin.dylib # macOS
├── audio/
│   ├── libaac_plugin.so
│   └── ...
└── plugin_manifest.json     # Plugin metadata
```

---

## 7. Progress Tracking

Track progress in `PROJECT_STATUS.md`:
```markdown
# Bucharest Lite — Project Status

## Phase 0: Planning
- [x] Architecture spec complete
- [ ] Approved by L Hustla

## Phase 1: Backend
- [x] Core engine implemented
- [x] Timeline engine implemented
- [ ] Render backend implemented
- [ ] Audio engine implemented
- [ ] Codec plugins implemented

## Phase 2: Frontend
- [ ] Qt6 GUI implemented

## Phase 3: Export
- [ ] Export pipeline implemented

## Phase 4: Testing
- [ ] Unit tests written
- [ ] Integration tests written
- [ ] All tests passing

## Phase 5: Documentation
- [ ] API docs complete
- [ ] User guide complete
- [ ] Developer guide complete
```

---

## 8. Quick Start — First Session

1. Open OpenClaw
2. Run: `opencode` to start the coding session
3. Select the BucharestLite project
4. Start with Planner agent — request architecture analysis
5. Review the output
6. Approve or request changes
7. Switch to Build agents and assign tasks
8. Iterate until complete

---

## 9. Notes

- **Never skip the Plan phase.** Always get approval before code generation.
- **Keep interfaces stable.** Once defined, module interfaces should not change without consensus.
- **Codec plugins are independent.** Each codec can be implemented and tested separately.
- **Proxy editing is critical.** Implement early — it affects UI and rendering.
- **Cross-platform matters.** Test on all three platforms early and often.
- **GPL-3.0 compliance.** All code must be open source. No proprietary dependencies.

---

*This file is the source of truth for multi-agent orchestration. Update it as the project evolves.*
