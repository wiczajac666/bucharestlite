# Bucharest Lite Changelog

## Unreleased

### Added
- **UI-6 Export + Batch UI**: Export…/Export jobs… actions with worker-thread
  export (MP4/WebM only), batch queue persisted to `export_queue.json`, inline
  progress/cancel, codec set shared with the preview pipeline via
  `PreviewPanel::pluginDirs()`.
- **UI-7 Mixer panel**: per-audio-track strips (gain fader 0–1.5 + dB, pan
  −1…1, M/S toggles) and a functional master strip; new track-level
  `gain`/`pan` and `SequenceSettings` `masterGain`/`masterPan` with
  backward-compatible serialization; drag = one undoable `FunctionCommand`.

### Fixed
- Staged test fixtures/plugins before `gtest_discover_tests` so incremental
  UI runs don't spuriously fail.
- `bl_lite` target outputs `bucharest-lite` and is installed to `bin`.

## v1.0.1 — 2026-09-15

### Fixed
- **CMakeLists.txt**: Removed duplicate `option(BL_BUILD_EXPORT "Build export pipeline (bl_export)" OFF)` definition (was defined on both lines 16 and 18, second is now removed)
- **CMakeLists.txt**: Version bumped from 1.0.0 to 1.0.1

## v1.0.0 — 2026-08-25

### Initial Release
- All 20 essential features implemented
- 732 tests passing (308 core + 424 UI) across debug/release/TSAN builds
- Full plugin ABI system (v2) with FFmpeg codec plugins
- Rational time arithmetic with exact integer operations
- Thread-safe CodecRegistry with shared_mutex
- Project persistence with JSON schema migration v0→v1
- Cross-platform build (Linux, Windows, macOS)
- GPU-accelerated preview via OpenGL compositing
- Export pipeline with codec plugins and format presets
- Full undo/redo system with command merging
- Proxy editing workflow
- Multi-track timeline with snapping and trim/split/ripple operations
- Transitions and keyframeable effects
- Subtitle support (SRT/ASS burn-in)
- Audio mixing with meters and scopes
- Color grading with wheels and curves
- Chroma key compositing
- Titles and text animation
- Batch export with queue persistence
- Dark/light theme support
- QSettings layout/geometry/theme persistence