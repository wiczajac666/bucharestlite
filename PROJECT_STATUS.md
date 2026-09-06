# Bucharest Lite — Project Status

## Phase 0: Planning
- [x] Architecture spec complete (`docs/architecture/ARCHITECTURE.md` v1.0.0)
- [x] Approved by L Hustla (2026-08-25) — **Gate 1 passed**
- [x] Open questions answered (ARCHITECTURE.md §18, decisions D1–D3)
- [x] Deviations §19 accepted

## Phase 1: Backend
- [x] CORE-1: project skeleton, logger, Result/Err, time library + unit tests (27/27 green)
- [x] CORE-2: plugin ABI header (bl_plugins v1), platform layer, PluginLoader, CodecRegistry + fixtures (44/44 green)
- [x] CORE-3: built-in passthrough plugin (video+audio, ABI v2, registerBuiltins) + docs (51/51 green)
- [x] Thread-safety retrofit: CodecRegistry shared_mutex, race-free plugin statics, TSAN preset + stress tests (55/55 green incl. linux-tsan)
- [x] CORE-4: thread-safe UndoStack + command infrastructure (ICommand/CommandBase/FunctionCommand; merge-on-push, macros, memory limit) — 71/71 green incl. linux-tsan
- [x] CORE-5: JobManager (priority queue, cooperative cancel, events) + MediaSource probe + Demuxer (FFmpeg 8) — 90/90 green incl. linux-tsan
- [x] CORE-6: ProjectRepository (JSON schema v1, atomic saves, relative media paths, migrator framework) + AutosaveRing (10-slot, recovery detection) — 106/106 green incl. linux-tsan; bl_core complete
- [x] TL-1: Timeline domain model (Sequence, Track, Clip, Marker, SpeedRemap, EffectInstance, TimelineSnapshot, Timeline) + JSON round-trip — 140/140 green incl. linux-tsan; bl_timeline module complete
- [x] TL-2: Timeline editing operations (trimLeft/Right, rippleDelete, rippleTrimLeft/Right) + speed-aware source math + splitClip fix — 163/163 green incl. linux-tsan
- [x] DecoderBridge (plugin decode adaptation) — 8/8 green incl. linux-tsan
- [x] bl_render (CPU compositor: FrameCache, Compositor, EffectRegistry, built-in effects) — 19/19 green incl. linux-tsan
- [x] bl_audio (AudioEngine, TrackStrip mixer, Mixer, IDeviceOutput/NullDeviceOutput) — 12/12 green incl. linux-tsan
- Test count: 308 total (debug + linux-tsan)
- [x] TL-4: 3-point editing (Track insertClip split-and-push / overwriteClip remnant-aware / appendClip + makeThreePointClip source-range→timeline mapping) — 190/190 green incl. linux-tsan
- [x] TL-5: speed/time retiming (setClipSpeed source-preserving retime + ripple, retimedTimelineDuration rational math, effectiveDuration semantics clarified) — 203/203 green incl. linux-tsan
- [x] TL-6: keyframeable properties (KeyframeTrack/Set, Hold/Linear/Bezier-smoothstep evaluate, 7 channels, clip-relative times, fragment rebase across split/insert/overwrite) — 225/225 green incl. linux-tsan
- [x] TL-7: clip transitions (edge-metadata model: TransitionSpec kinds, duration, alignment, params; addTransition validates adjacency+bounds; pruneInvalidTransitions sweep on all mutators; split/insert/overwrite/ripple/speed all prune gracefully) — 252/252 green incl. linux-tsan
- [x] TL-8: subtitle track data model (SubtitleStyle struct; subtitleText + subtitleStyle on Clip; setSubtitleText/getSubtitleText/setSubtitleStyle/getSubtitleStyle on Track; Sequence/Timeline wrappers) — 269/269 green incl. linux-tsan
- [x] bl_timeline (timeline domain model, clips, tracks, sequences, keyframes, transitions, subtitles)
- [x] Render backend implemented (bl_render — CPU compositor)
- [x] Audio engine implemented (bl_audio)
- [x] Codec plugins implemented (h264, vp9, av1, theora, mpeg4, aac, flac, vorbis, opus) — 9 dlopen FFmpeg-backed MODULEs, best-effort decoder init for extradata-driven codecs (theora/vorbis/opus), granule-aligned audio encode feed (EAGAIN-tolerant, tail-on-flush), lossless FLAC round-trip; av1 via libdav1d; staged into <build>/plugins/{video,audio}; tests `CodecPlugins/*` + `CodecPluginTest.*` — 321/321 green incl. linux-tsan; dev guide: `docs/developer-guide.md`
- Test count: 321 total (debug + linux-tsan) — after codec plugins
- Test count: 333 total (debug + linux-tsan) — after UI-1 (12 new UI tests)

## Phase 2: Frontend
- [x] UI-1: Qt6 GUI skeleton — `bl_ui` module + `bl_lite` app entry; `ProjectController` (new/open/save-as via ProjectRepository, timeline JSON in `project.extensions["timeline"]`, undoable rename & media-bin mutations); dockable `MainWindow` (Media Bin, Preview, Inspector, Mixer, Timeline docks; action bar; dark/light theme; QSettings layout+geometry+theme persistence; dirty title `*`); placeholder panels for UI-2..UI-7; offscreen QPA coverage `MainWindow.*` + `ProjectController.*` — 333/333 green incl. linux-tsan (12 new UI tests)
- [x] UI-2: Timeline panel — `TimelineEditController` (Qt-free, undoable move/trim/split/remove + group-move with track-kind agnostic drops, snap, ripple vs non-ripple semantics, every edit a single `FunctionCommand`); QGraphicsView timeline (`timeline_items`: ruler w/ tick marks + timecode, playhead, snap indicator, markers, lane, clip items w/ 6px trim zones + transition hatch + subtitle badge; `timeline_panel`: `TimelineView` with click/Ctrl-toggle!marquee/A-select, drag move, trim preview+commit, Shift+trim ripple, Ctrl+wheel zoom clamp 0.25–64 px/frame, ruler scrub, Delete=non-ripple / Backspace=ripple / S=split-at-playhead; `TrackHeader` fixed-width lane titles synced to vertical scrollbar; snap checkbox, zoom controls); MainWindow wiring (Timeline no longer a placeholder — MainWindow placeholder count 5→4); offscreen QPA coverage `TimelineEditController.*` (13) + `TimelinePanel.*` (12) — 358/358 green incl. linux-tsan (25 new UI tests; tsan preset runs with `ignore_interceptors_accesses=1` to filter Qt6/offscreen internal allocator false positives)
- Test count: 358 total (debug + linux-tsan) — after UI-2 (25 new UI tests)
- [x] UI-3: Preview panel — `transport_controller` (Qt-free play/pause/stop, tick-by-wall-clock, ±1-frame step, rate; `TransportControllerTest.*` — 13 tests); CPU preview pipeline `preview_engine` (`FrameCache` keyed by mediaItem/frame/size, `MediaDecodeSource` lazily opening `Demuxer`+EXTRA-data codec awareness + `CodecRegistry` fallback lookup by `caps.ff_decoder`, divergence-aware decode loops with mid-loop `DecoderBridge::flush`; `PreviewEngineTest.*` — 7 tests incl. real theora fixture through full pipeline, repeat-request cache hits, reopen-on-demand); `preview_panel` (`PreviewSurface` paints BGRA→`QImage::Format_RGB32` letterboxed on black; transport bar ▶/⏸/■/⏮/⏭ + timecode label, 33ms `PreciseTimer` tick, engine rebuilt on project/sequence-settings change; `playheadChanged` synced BOTH ways with TimelinePanel — equality no-op prevents recursion; `timelineChanged` → refresh); MainWindow wiring + Preview dock is real (placeholder count 6→5); fixed demuxer PTS conversion (AVRational `time_base` → ticks-per-second `Time` rate was inverted `num/den`, folding every frame to time 0 → identical frames; now `Rational::make(time_base.den, time_base.num)`) and NaN-safe fps probe fallback on zero/unknown rates; offscreen QPA coverage `PreviewPanelTest.*` (5) incl. scrub-loop finite-coupling and play-until-duration — 382/382 green (debug + release), 358/358 incl. linux-tsan (UI tests excluded from tsan preset)
- [x] UI-4: Media Bin panel — `MediaBinPanel` replaces the placeholder (placeholder count 5→4): `QListWidget` of media rows keyed by media-bin id, Add... (`QFileDialog` multi-select) + Remove button/activated-key, case-insensitive live name filter (`mediaBinFilter`); every mutation goes through the existing undoable `ProjectController::addToMediaBin`/`removeFromMediaBin` and the panel reloads on `projectChanged`, so add/remove/undo/redo/open/new all reflect; Qt-widget-free `media_bin_detail::matchesFilter` helper for plain unit tests; MainWindow wiring `new MediaBinPanel(controller_, this)`; offscreen QPA coverage `MediaBinPanel.*` (7) — 389/389 green (debug + release); drag-to-timeline deferred (no TimelineView drop handling yet)

## Phase 3: Export
- [ ] Export pipeline implemented (bl_export)

## Phase 4: Testing
- [ ] Unit tests written
- [ ] Integration tests written
- [ ] All tests passing

## Phase 5: Documentation
- [ ] API docs complete
- [ ] User guide complete
- [ ] Developer guide complete
