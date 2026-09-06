# Bucharest Lite — Developer Guide: Codec Plugins

This guide covers the Phase 1 codec plugin layer (PLG-1..PLG-9): the loadable
plugin ABI, how the FFmpeg-backed plugins are structured, the audio encode
constraints that trip people up, and how the plugin test harness works.

## Overview

A codec plugin is a `dlopen`-loadable shared module that exports a single
entry point `bl_get_codec_plugin()` returning a `BlCodecPlugin*`
(`include/bl_plugins/codec_plugin.h`, ABI v2). Bucharest Lite ships nine
FFmpeg-backed codecs as modules:

| Plugin | Codec | encoder | decoder | module output |
|--------|-------|---------|---------|---------------|
| PLG-1 | h264 | libx264 | h264 | `plugins/video/blh264.so` |
| PLG-2 | vp9 | libvpx-vp9 | vp9 | `plugins/video/blvp9.so` |
| PLG-3 | av1 | libsvtav1 | libdav1d | `plugins/video/blav1.so` |
| PLG-4 | theora | libtheora | theora | `plugins/video/bltheora.so` |
| PLG-5 | mpeg4 | mpeg4 | mpeg4 | `plugins/video/blmpeg4.so` |
| PLG-6 | aac | aac | aac | `plugins/audio/blaac.so` |
| PLG-7 | flac | flac | flac | `plugins/audio/blflac.so` |
| PLG-8 | vorbis | libvorbis | vorbis | `plugins/audio/blvorbis.so` |
| PLG-9 | opus | libopus | opus | `plugins/audio/blopus.so` |

## ABI contract (what every plugin must honor)

- **Video interchange format**: BGRA32 packed (bottom-up not assumed; the
  wrapper converts). Audio interchange: float32 planar (each channel is a
  plane of `sample_count` floats).
- **Ownership**: decoded/encoded buffers are produced through
  `cfg->host->alloc(size, userdata)` and freed by the host via
  `cfg->host->free(ptr, userdata)`. The plugin never frees host memory itself.
- **flush()** carries no metadata: trailing decoder flush frames and encoder
  EOF frames are count-only (no geometry/keyframe guarantees).
- **decode()** is one-packet-per-call. A server is not allowed to return
  `BL_DECODE_NEED_MORE_INPUT` to a DecoderBridge caller; use
  `thread_count = 1` and hand packets one at a time.
- **Encoder availability** is reported at `init()`; if the encoder cannot be
  opened the plugin still returns `BL_OK` from `init()` and `encode()` fails
  with `BL_ERR_ENCODE_FAILED` so callers can fall back.

## FFmpeg wrapper (`src/plugins/ffmpeg/ffmpeg_plugin_common.c`)

All nine plugins are thin descriptors on top of one shared static library.
Each descriptor sets a `BlFfmpegProfile`:

```c
static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_H264,
    /*decode_name*/ "h264",
    /*encode_id*/ AV_CODEC_ID_H264,
    /*encode_name*/ "libx264",
    /*encode_fallback_name*/ NULL,
    /*enc_pix_fmt*/ AV_PIX_FMT_YUV420P,      /* video */
    /*enc_sample_fmt*/ AV_SAMPLE_FMT_S16,     /* audio */
    /*encode_options*/ kOptions,
    /*caps_flags*/ 0u,                        /* or BL_FLAG_LOSSLESS */
    /*file_extensions*/ kExtensions,
    /*is_video*/ 1,
};
```

The wrapper:

1. Opens the decoder (`decode_name`) and encoder (`encode_name`, falling
   back to `encode_fallback_name`) against the `decode_id`/`encode_id`.
2. Open is **best-effort**: codecs that need container extradata
   (theora/vorbis/opus with raw packets) fail `avcodec_open2` at `init()`;
   the wrapper tolerates that, leaving the decoder NULL until extradata is
   attached via `cfg->extradata`/`cfg->extradata_size`. `decode()` returns
   `BL_ERR_DECODE_FAILED` until then.
3. Decode: per-call frame → swscale to BGRA32 (video) or swresample to FLTP
   (audio) → `host->alloc`.
4. Encode: converts the interchange frame to the encoder's native format and
   forwards to `avcodec_send_frame`.

### The audio encode feed (read this)

`feed_one_audio_slice()` + `push_pending_samples()` implement a pending
sample batch. Incoming audio is appended to a planar FIFO and consumed in
**full `frame_size` granules** only. Why:

- `libvorbis` rejects any frame larger than its `frame_size` (64 at 48 kHz);
  the generic `avcodec_send_frame` check fails such frames outright.
- `libopus` **rejects mixed-size frames**: it internally rewrites the audio
  from `frame->data[0]` into a reusable sample buffer and produces a packet
  per `send_frame`; sending 960 then 64 then 960 triggers `EINVAL` on the
  second 960.
- Sending too much input in one burst makes `avcodec_send_frame` return
  `EAGAIN` when the encoder's internal buffering fills up.

Therefore `encode()` never hard-fails on `EAGAIN`: it stops, keeps the
remainder pending, and the next `encode()` (or `flush()`) continues. At
`flush()`, the sub-`frame_size` remainder is pushed as the **final, small
frame** before the EOF marker (`avcodec_send_frame(NULL)`).

In practice for the shipped plugins:

- aac/flac/vorbis: each 1024-sample chunk maps to exact granules, no
  remainder.
- opus (frame_size 960): a 1024-sample chunk sends one full granule and the
  64-sample remainder stays pending until the next call; the flush tail is
  the final small frame.

### Per-codec notes

- `av1`: the native `av1` decoder in some FFmpeg builds (e.g. Debian) is
  hardware-gated ("Your platform doesn't support hardware accelerated AV1
  decoding"); `av1_plugin.c` uses `libdav1d` for decode. `libsvtav1` also
  requires both dimensions >= 64, so round-trip tests use 128x64.
- `opus`: encoder sample format must be `AV_SAMPLE_FMT_S16` or
  `AV_SAMPLE_FMT_FLT` (FLTP is rejected); set a `frame_size` option (960 =
  20 ms @ 48 kHz) so granule accounting is deterministic.
- `flac`: declared `BL_FLAG_LOSSLESS`; a `frame_size` option pins framing so
  input decodes back to the exact s16 quantization of the source.
- `vp9`: `cpu-used` is clamped to the valid 0..9 range.

## Building and staging

CMake (`src/plugins/CMakeLists.txt`) builds every module with
`POSITION_INDEPENDENT_CODE` (the shared `bl_ffmpeg_plugin_common` static lib
needs PIC to link into MODULEs) and writes them to:

```
<build>/plugins/video/bl<name>.so
<build>/plugins/audio/bl<name>.so
```

Tests re-stage those into `<build>/tests/unit/plugins/…` via the
`bl_core_tests_plugins` ALL target. That copy is stamp-ordered against the
real MODULE targets (`h264 vp9 av1 … opus`) — depend on a custom aggregate
target instead and stale binaries will silently leak into the test dir.

## Testing

`tests/unit/test_codec_plugins.cpp` exercises every plugin through the real
dlopen ABI:

- `CodecPlugins/VideoRoundTripTest` — h264/vp9/av1/theora/mpeg4 raw encode ->
  decode geometry checks.
- `CodecPlugins/AudioRoundTripTest` — aac sample-count round trip.
- `CodecPlugins/AudioLosslessRoundTripTest` — flac decodes to the exact s16
  quantization of the input (eps = 1/32767 + 1e-5 per sample).
- `CodecPlugins/ExtradataCodecTest` — theora/vorbis/opus decoded from
  container fixtures (`tests/media/test_video_theora.ogv`,
  `test_audio_vorbis.ogg`, `test_audio_opus.opus`); extradata and params come
  from a libavformat probe (`probeContainer()`), packets via `Demuxer`.
- `CodecPluginTest.StandaloneEncodersProducePayload` — encode-only
  emissions for theora/vorbis/opus.
- `CodecPluginTest.*` — plugin registry loading, Annex-B fixture through
  DecoderBridge.

Run the sweep the same way the project gates CI:

```
cmake --preset linux-debug   && cmake --build --preset linux-debug   && ctest --preset linux-debug
cmake --preset linux-release && cmake --build --preset linux-release && ctest --preset linux-release
cmake --preset linux-tsan    && cmake --build --preset linux-tsan    && ctest --preset linux-tsan
```
## GUI (bl_ui)

The Qt6 frontend lives in `src/ui/` (built when `BL_BUILD_UI=ON` and Qt6 is
found). `bl_lite` is the application entry point; `bl_ui` is a static library
holding the application shell.

Layout:

- `app/main.cpp` — `QApplication` entry (`bl_lite`).
- `app/main_window.hpp/.cpp` — dockable `QMainWindow` shell with action bar,
  dark/light theme and `QSettings`-backed layout/geometry/theme persistence.
  Inject a `QSettings*` in the constructor for isolated test state.
- `app/project_controller.hpp/.cpp` — owns the open document: `bl::ProjectData`,
  the `bl::Timeline` and the `bl::UndoStack`. Persistence goes through
  `bl::core::ProjectRepository`; the timeline is serialized inside
  `project.extensions["timeline"]` so a `.blproj` round-trips both documents.
  Mutations (rename, media-bin add/remove) are pushed to the undo stack.
- `app/theme_manager.hpp/.cpp` — dark/light stylesheet builder.
- `panels/*` — dockable panels. Placeholders remain for UI-4..UI-7 (Media Bin,
  Inspector, Mixer); the Timeline (UI-2) and Preview (UI-3) are real
  implementations described below.

GUI tests run headless through the offscreen QPA platform:

```
cmake --preset linux-debug
cmake --build --preset linux-debug --target bl_ui_tests bl_lite
QT_QPA_PLATFORM=offscreen ./build/linux-debug/tests/ui/bl_ui_tests
```

`MainWindow.*` covers dock creation, dirty-tracking on the window title, undo/
redo action enablement, and layout/theme persistence across windows.
`ProjectController.*` covers document creation, undoable mutations, and
save/load round-trips through `ProjectRepository`.

## Timeline panel (UI-2)

The Timeline replaces the UI-2 placeholder. Three layers:

- `panels/timeline_edit_controller.hpp/.cpp` — **Qt-free** edit logic. Every
  mutation validates on the flat track index (video lanes first, then audio
  lanes) before pushing a **single** `FunctionCommand` onto the project undo
  stack; `undoChanged` from the stack triggers a full panel rebuild. Covers
  move/trim/ripple-trim/split/remove, group-move (keeps relative offsets),
  and snapping (6 px tolerance). Single-clip drags may land on *any* other
  track — kind-mismatch drops are validated by `canPlaceAt`, not rejected.
- `panels/timeline_items.hpp/.cpp` — `QGraphicsItem` scene primitives: `Ruler`
  (tick marks + non-drop timecode at 1s intervals), `Playhead`, `SnapIndicator`,
  `Marker`, `Lane`, and `Clip` (6 px left/right trim zones, transition hatch on
  the out edge when `transitionSpec` is set, `»S«` badge for subtitled clips,
  live `previewRect` overlay during move/trim).
- `panels/timeline_panel.hpp/.cpp` — `TimelinePanel` (snap checkbox, zoom
  buttons/label, playhead, selection `QSet<ClipId>`, time↔x helpers,
  `rebuildFromModel`) hosting `TimelineView` and `TrackHeader`. `TrackHeader`
  is a fixed-width lane-titles column whose vertical scrollbar is synced to
  the view's so titles stay glued to their lanes.

Scene geometry (see `timeline_geometry`): origin at ruler top-left,
`kRulerHeight = 26`, video lanes 44 px, audio lanes 36 px, view space px =
`frame * pixelsPerFrame` with the playhead at 0. Zoom clamps to 0.25–64
px/frame (default 6).

`TimelineView` owns the gesture state machine:

| Input | Action |
|-------|--------|
| click | select clip |
| Ctrl/Shift click | toggle selection |
| empty-lane drag | marquee (Ctrl/Shift merges) |
| clip drag | move; drop on any track (undoable) |
| edge drag | trim; Shift = ripple |
| ruler drag | scrub playhead |
| Delete | non-ripple delete |
| Backspace / Shift+Delete | ripple delete |
| S | split at playhead |
| Ctrl+A | select all clips |
| Ctrl+wheel | zoom (centered on cursor) |

Every gestures triggers `undoChanged` mid-gesture; the rebuilding panel calls
`TimelineView::cancelActiveGesture()` so a drag/trim releasing into a rebuilt
scene can't read a stale item.

Keyframe/undo gotchas for gesture tests:
- **Frame-quantization**: `Time` arithmetic (ripples in particular) produces
  rational fractions of a frame. Compare by `llround(seconds * fps)`-style
  frame helpers, never exact `Time` equality.
- **Flat indexing**: `flat = videoTrackIndex` then `tracks().size() +
  audioTrackIndex`; the shared test fixture (1 video + 1 audio) therefore has
  video lane 0 and audio lane 1. `Fixture::find(name)` scans *both* kinds —
  assert presence via absence on the source lane, not on emptiness of the
  search.

`TimelinePanel.*`/`TimelineEditController.*` cover: clip/marker rendering,
click/Ctrl-multi/marquee selection, move (incl. cross-kind to the audio lane)
with undo, trim/ripple-trim undoability, Delete-vs-Backspace semantics, split
at playhead, ruler scrub, zoom clamping + rebuild, and scene refresh after
undo/redo. `TimelineEditController` runs offscreen-free as plain unit tests.

**TSan note**: the offscreen QPA platform and QTest's event machinery spawn
pooled threads whose internal `QArrayData` allocator traffic races inside
`libQt6Core`/`libQt6Gui` (not in app code). The `linux-tsan` *test preset*
sets `TSAN_OPTIONS=ignore_interceptors_accesses=1`, which filters
interceptor-only allocation races while still reporting any race whose
accesses have real app/`bl_*` frames. Re-run the full GUI suite under tsan
after touching the gesture handlers.

## Preview panel (UI-3)

The Preview dock composits the timeline to a `QImage` on the main thread
(no GPU yet — GL rendering is deferred). Three layers:

- `transport/transport_controller.hpp/.cpp` — **Qt-free** transport state
  machine. Play/pause/stop, `tick(Duration)` advances the playhead by wall
  clock (auto-pauses at the end), `stepForward`/`stepBackward` move exactly one
  frame at the sequence fps (clamped to `[0, duration]`), notify handlers
  (`setPlayheadHandler`/`setStateHandler`) fire on every change. Drives a
  33 ms `QTimer` in the panel.
- `bl_render::PreviewEngine` (`src/render/preview_engine.cpp`) + `MediaDecodeSource`
  (`src/render/media_decode_source.cpp`) — CPU backend. `FrameCache` keys by
  `(mediaItemId, frame, outputSize)`; `MediaDecodeSource` lazily opens a
  `Demuxer` per media item, looks the decoder up in `CodecRegistry` and
  feeds packets until the requested frame is covered, seeking backward when the
  requested frame precedes the current position. When a seek lands mid-GOP it
  decodes from the previous keyframe; an in-flight `DecoderBridge::flush()` is
  used on divergence so stale decode state can't bleed into the requested
  frame. Container codecs decode from EXTRA data (`avcodec_parameters`) instead
  of `AVCodecContext.priv_data` re-initialization.
- `panels/preview_panel.hpp/.cpp` — `PreviewSurface` + `PreviewPanel`.
  Compositor output (BGRA `Format_RGB32`) is copied into a `QImage` via
  `raw.copy()` — BGRA memory order already matches little-endian RGB32, so no
  channel shuffle is needed. `PreviewPanel` owns a `TransportController`, a
  33 ms `PreciseTimer` `QElapsedTimer`-driven tick loop, a timecode label
  (`previewTimecode`), and rebuilds the engine when the project or the base
  sequence settings change.

Playhead sync is bidirectional and recursion-safe: MainWindow connects
`PreviewPanel::playheadChanged → TimelinePanel::setPlayhead` and
`TimelinePanel::playheadChanged → PreviewPanel::setPlayheadFromTimeline`.
`TransportController::setPlayhead` is a no-op when the position is unchanged,
so a scrub pushed down to the transport doesn't echo a second `playheadChanged`
back up. Timeline edits (`TimelinePanel::timelineChanged`) pull
`PreviewPanel::onTimelineChanged`, which re-derives the sequence duration and
re-renders the current frame without moving the playhead.

The render backend is intentionally simple today: playhead-tick rendering,
no dropped-frame policy, no A/V sync (real audio + sync are AUD-3/UI-6), and
no GPU path (RND-1).

**Time-keeping gotchas** (learnt the hard way):
- FFmpeg streams report `AVRational time_base` as *seconds per tick*;
  `bl::Time` stores *ticks per second*. Converting requires
  `Rational::make(time_base.den, time_base.num)` — swapping `num`/`den`
  inflates every frame to ~576x its true time, so a seek to frame 12 stalls at
  frame 0 and every requested time renders the same picture.
- `avg_frame_rate` can be legitimately unknown (`{0,1}`). `Rational::valid()`
  only checks the denominator, so decode code must also reject zero rates and
  fall back (e.g. `{24,1}`) or every cache key collapses to time 0.
- Stepping one frame at 23.976/29.97 into a microsecond-rate playhead rounds
  (`roundHalfEven`), so compare stepped positions with `EXPECT_NEAR(..., 1e-6)`,
  not exact `Time` equality.
