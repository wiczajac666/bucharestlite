# Bucharest Lite Changelog

## v1.1.1 — 2026-09-27

### Added
- **Alpha compositing**: the preview/export compositor blends frames with
  per-pixel premultiplied alpha (source RGB premultiplied by alpha before
  compositing) instead of an additive accumulator, so faded clips, opacity
  keyframes and stacked layers combine correctly over the tracks below.
- **Five new video effects**: Chroma Key, Sharpen, Hue/Saturation,
  Levels/Curves and Crop grow the built-in effect catalog from 4 to 9; each
  ships a parameter schema rendered as sliders with one undoable command per
  drag gesture.
- **Speed editing & reverse playback**: every clip carries a fractional
  speed (numerator/denominator) and a *Reverse* toggle; the preview compositor
  and the audio mixdown share one `Clip::sourceTimeAt` source-time mapping, so
  sped-up or reversed clips play sample/field-truthfully on the timeline, and
  speed changes are one undoable Inspector command.
- **Soft subtitles**: a per-clip subtitle text editor in the Inspector, exported
  as a genuine subtitle stream (MOV_TEXT) in MP4 output — the text is never
  burned into the picture; subtitle streams are probed and listed by
  `MediaSource`, and each text's duration honors the clip's timeline span.
- **Audio export**: the Export dialog gains a default-on *Include audio* option
  that muxes a stereo mixdown of every audible clip/track into the output
  (AAC/FLAC in MP4, Opus/Vorbis in WebM). A Qt-free offline
  `TimelineMixdown` renderer (`bl_export`) mirrors the mixer's channel, gain and
  pan laws plus volume keyframes and mute/solo, decoding each referenced clip
  through `PcmAudioSource` (Demuxer + DecoderBridge + libswresample at the
  sequence sample rate) and feeding the audio encoder fixed-size planar PCM
  blocks; missing or undecodable clips contribute silence. Encoder granules
  drive monotonic audio PTS in the muxers.
- **UI-11 Live mixer level meters**: each audio strip and the master bus now
  show classic L/R peak-dB bars (green/amber/red zones + decaying peak hold)
  driven by the playhead. Per-source audio is decoded once in the background
  (`bl_audio::AudioMeterEngine`, backed by the JobManager) into 60 Hz
  peak/RMS envelopes, and the meter reads are mix-accurate at any playhead
  position — clip and track gain/pan, mute/solo and master gain/pan all shape
  the reading, mirroring the renderer's `TrackStrip` constant-power mapping.
  Meters follow playback ticks and ruler scrubs alike
  (`PreviewPanel::playheadChanged`); analysis is reconciled with the media bin
  on every project change.
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
- **UI-10 Media bin → timeline drag-and-drop**: media rows drag out of the bin
  carrying their id (`application/x-bucharest-media`); dragging onto the
  timeline probes the source once for duration and stream kind, snaps the
  start, shows a placement ghost (green for valid, red on overlap), routes
  video/audio to matching lanes (A/V sources to either), and commits one
  undoable "Add clip" command per drop via the new
  `TimelineEditController::addClip` mutator.
- **Flatpak packaging**: `bucharest-lite-app.json` is a buildable flatpak-builder
  manifest (org.kde.Platform/Sdk 6.8) that bundles FFmpeg 8.0.1 (with
  libaom/libopus/libtheora/libvorbis) since the freedesktop SDK's FFmpeg 7
  predates the codec plugins' `avcodec_get_supported_config`/`SwsContext` API;
  `packaging/linux/build_flatpak.sh` drives the same build with only the
  `flatpak` CLI (no flatpak-builder dependency). Verified end-to-end: bundle
  built, installed from the `.flatpak`, and launched offscreen in the sandbox.
  h264 falls back to FFmpeg's native encoder (no libx264) and VP9 encode is
  unavailable (no libvpx) in this build.

### Fixed
- Reversed clips decode their source media in ascending order during audio
  mixdown (the mirrored window was previously read backwards, breaking
  decoders whose seek is not sample-exact).
- Reversed clip source-time mapping used the wrong delta type and distance
  accounting in the timeline core.
- The export pipeline no longer requires an audio track for MP4 subtitle
  streams (subtitle stream creation is decoupled from audio) and drops a
  duplicate `fps` declaration in the export runner.
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