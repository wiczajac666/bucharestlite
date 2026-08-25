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
- [ ] DecoderBridge (plugin decode adaptation) — deferred to PLG-1..9 phase
- [ ] Timeline engine implemented (bl_timeline): TL-1..8  ← **next**
- [ ] Timeline engine implemented (bl_timeline)
- [ ] Render backend implemented (bl_render)
- [ ] Audio engine implemented (bl_audio)
- [ ] Codec plugins implemented (h264, vp9, av1, theora, mpeg4, aac, flac, vorbis, opus)

## Phase 2: Frontend
- [ ] Qt6 GUI implemented (bl_ui)

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
