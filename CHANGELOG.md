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
- **UI-8 Autosave + crash recovery**: dirty state now derives from the undo
  index (all edit kinds mark the project dirty), and an `AutosaveManager`
  snapshots on a 60s timer and on window focus-loss into a rolling
  `AutosaveRing` (10 slots, keyed by project name); opening/creating a project
  offers to restore a newer snapshot, adopting it against the original project
  dir so a later save rewrites the real file.
- **UI-8 Keyboard shortcuts**: centralized `ActionRegistry` makes every action
  remappable (new defaults include Ctrl+Shift+E batch export, Ctrl+1..5 panel
  toggles, Space playback); overrides persist to QSettings; Help → Keyboard
  Shortcuts… opens the inline-capture editor.
- **UI-9 Video effects**: the Inspector's Add… menu lists every built-in effect
  from the `EffectRegistry` and seeds identity-by-default params; each effect's
  parameters now have a schema (`ParamSpec`: key/label/range/default) rendered
  as sliders that commit one undoable command per drag gesture. Color
  Correction (brightness/contrast/gamma) ships with specs, Box Blur exposes a
  radius slider, and spec-less effects keep the raw JSON editor. The registry
  registers builtins on first use so the running app serves a populated
  catalog.

### Fixed
- Project title ` *` and the close prompt now appear for clip/mixer edits too,
  not only rename and media-bin changes; undo-to-pristine clears dirty.
- Undo/redo shortcuts are registered through the same remappable registry.
- Recovery decision no longer misses a snapshot when the manual save and the
  autosave share a modification-time tick (NTFS updates timestamps lazily);
  ties fall back to a content comparison.
- Built-in effects are registered on first use of the `EffectRegistry`, so the
  effect catalog and per-clip effect processing work in the running app (they
  had only been populated inside unit tests).
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