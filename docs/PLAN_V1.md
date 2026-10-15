# Bucharest Lite — v1 Feature Completion Plan

Status of the full v1 backlog (the ESSENTIAL list in `FEATURES.md` plus the
"planned" rows in `README.md` that are in v1 scope per
`docs/architecture/ARCHITECTURE.md` §1). Rows are ordered by priority.

Legend: `[x]` shipped · `[~]` in progress · `[ ]` pending

## P0 — Timeline parity (continues the Kdenlive timeline work)

- [x] **Phase 7: clip/empty-area context menus** (rename, set zone in/out,
      cut/copy/paste/delete, enable/disable, split) — clip, ruler, empty-area
      and marker context menus in `TimelineView::contextMenuEvent`
- [x] **Phase 7: clipboard** — copy / cut / paste across tracks and sessions
      (`TimelineEditController::setClipboard`/`pasteClips`, panel wrappers,
      Ctrl+C/X/V + Edit/Timeline menu actions)
- [x] **Phase 7: marker UI** — add marker at playhead/click, rename via
      context menu, delete; `M` shortcut, Timeline menu action
- [x] **Waveform display** on audio clips (peaks cached per media item) —
      shipped as the P4 "Timeline waveform display" entry below;
      `bl::waveformColumns` + `ClipItem` painting + `AudioMeterEngine` cache

## P1 — Export

- [x] **Render presets** (YouTube 1080p/4K, Vimeo, WebM, DVD, Blu-ray,
      mobile) — `exportPresets()`, applied from the export dialog
- [x] **Per-range export** — zone/work-range export wired from the timeline
      to the export dialog (`setZone` + range combo)
- [x] **Per-clip export** — export the selected clip's span (range combo
      "Selected clip"; `TimelinePanel::selectedClipRange`)
- [x] **PSNR / SSIM validation** — compare two exported files and report
      quality metrics. **Tools → Compare Quality…** decodes both inputs
      frame-by-frame on the shared `Demuxer` + `DecoderBridge` stack (normalized
      to a common size/pixfmt), computes per-frame MSE→PSNR and windowed 8×8
      SSIM (standard C1/C2 constants) and reports avg/min/max, with explicit
      handling of frame-count/dimension mismatch and the MSE=0 case (PSNR cap).
      Qt-free analyzer (`bl_render/quality_metrics`) + UI-only dialog.
- [x] **Stream to IP** — publish instead of writing a file: an **Output →
      Target** choice *Save to File* / *Stream to IP*. RTMP/RTMPS publishes FLV
      (`rtmp://host/app/key`), UDP publishes MPEG-TS (`udp://host:port`); the
      container is inferred from the URL scheme and the codecs are constrained
      to H.264 + AAC. No real-time pacing (renders as fast as possible) and no
      SRT in v1.

## P2 — Media import

- [x] **Subtitle import** — SRT / ASS / WebVTT → timeline subtitle clips
      (`bl::parseSubtitles` + `importSubtitleCues`; File → Import Subtitles…)
- [x] **Image-sequence import** — `bl::detectImageSequence` expands a dropped
      frame to its whole `name%04d.ext` sequence and the media bin stores it
      under the FFmpeg pattern path so the renderer decodes every frame
- [x] **Proxy editing** — **decision: out of v1 scope** (high-res media plays
      directly via demand decode; see `docs/architecture/ARCHITECTURE.md` §1).
      The dead `proxy_generator` stub (`include/bl_core/proxy/*` +
      `src/core/proxy/*`) was **deleted** in v1: it was non-functional, wired
      into no UI, and the stale `src/core/proxy/proxy_generator.hpp` declared a
      conflicting `ProxyJobParams`/`createProxyJob` overload and included two
      non-existent headers (`bl_core/media_locator.hpp`,
      `bl_core/rational.hpp`). Implement post-v1 if a proxy workflow is wanted.

## P3 — Color & scopes

- [x] **Scopes** — Scopes dock with luma/RGB histogram, luma waveform and chroma
      vectorscope, tapped from the composited preview frame
      (`bl::computeScopes` in `bl_render/scopes.hpp`); audio meters already ship
      in the Mixer dock
- [x] **`.cube` LUT import** — `bl::parseCubeLut` / `loadCubeLutFile` /
      `sampleCubeLut` (1D + 3D, trilinear, DOMAIN_MIN/MAX) plus a `lut.cube`
      effect; Inspector → Add → LUT (.cube) prompts for the file
- [x] **Color wheels** — `color_wheels` effect (per-channel lift / gamma /
      gain, 9 sliders); the existing `levels_curves` effect covers curves

## P4 — UI / workflow

- [x] **Title editor** — `TitleSpec` model (`include/bl_timeline/title.hpp`),
      Qt renderer (`title_renderer`), dialog (`TitleEditorDialog`) with font,
      size, bold/italic, colour, alignment, anchor position, background colour,
      outline, and line-spacing settings; live preview; renders to a PNG at the
      sequence resolution (transparent for overlay); `File → Add Title…`
      imports the title as a media item and appends it to the timeline at
      playhead (user-settable duration); title specs are stored in the project
      file (`extensions["titles"][id]`) and the controller provides
      `addTitleToMediaBin` / `titleSpecFor` / `mediaIdForPath` / `updateTitle`;
      titles can be re-edited in place (Edit Title… from the media bin or a clip,
      re-rendering the still and updating the spec/name as one undo step);
      undoable (`addTitleToMediaBin` and `updateTitle` are each one
      `FunctionCommand`).
- [x] **Timeline waveform display** — Qt-free `bl::waveformColumns` reduces an
      analyzed `AudioMeterEnvelope` to one normalized amplitude per column over
      a source range; `ClipItem` paints the shape inside each audio clip;
      `TimelinePanel::setWaveformResolver` / `refreshWaveforms` resolve each
      clip's envelope and MainWindow wires the shared `AudioMeterEngine`'s cache
      lookup in, repainting clips as background analysis settles.
- [x] **Bin folders / collections** — user-created folders on the media bin
      (`ProjectData::folders` + per-item `folderId`, additive JSON keys that keep
      legacy projects loading at the root); `ProjectController`
      `addFolder`/`renameFolder`/`removeFolder`/`moveMediaToFolder`/`moveFolder`
      (each one `FunctionCommand`; delete rehomes media and child folders to the
      root); the Media Bin dock gains a folder combo, New/Rename/Delete buttons
      and a "Move to Folder" context submenu, with the name filter scoped to the
      selected folder
- [x] **Project templates** (new-project from template) — Qt-free
      `bl_timeline/project_template` (`ProjectTemplate` = sequence settings +
      ordered track skeleton + seed track effects + seed titles;
      `builtinTemplates`/`findTemplate`/`instantiateTemplate`); four built-ins
      (`1080p30-standard`, `4k-uhd`, `vertical-9x16` with a Lower Third title,
      `audio-podcast` with two audio tracks + an identity colour correction);
      `ProjectController::newProjectFromTemplate` adopts an empty-path, dirty
      project; **File → New from Template…** picker
      (`TemplatePickerDialog`) lists built-ins plus any user `*.blproj`/`*.json`
      under `<userDataRoot>/templates`, rendering the seed titles to stills on
      creation
- [x] **Multi-monitor full-screen preview** — **View → Full Screen Preview**
      (`Ctrl+Shift+P`) detaches the single `PreviewPanel` into a frameless
      top-level `FullScreenPreviewWindow` and fills a chosen monitor (a screen
      picker appears only when more than one screen exists, defaulting to the
      first non-primary display; a single-screen setup is dialog-free); Escape
      or closing the window restores the same panel straight back into its dock.
      No second preview engine/transport/render tap is created — the one panel
      is reparented, so transport, compositing and the scopes tap all move with
      it and stay in lockstep with the timeline.

## P5 — Compositing & rendering

- [x] **Transition rendering** — the compositor evaluates `Clip::transitionOut`
      toward the next adjacent clip as a first-class operation ahead of the
      per-clip pass: crossfade, dissolve, fade-through-black, wipe and slide,
      honoring `TransitionAlignment` (Left tail / Center straddle / Right head
      spans) and `TransitionSpec::params` (wipe/slide `direction`, wipe
      `softness`). Preview and export share the single
      `Compositor::renderFrame` path.

## Explicitly not v1 (per ARCHITECTURE.md §1 + README scope notes)

GPU/Vulkan rendering, motion tracking, stabilization, multi-cam, nested
compositions (model exists; UI is v2), auto-subtitle generation, node
grading/OCIO, OpenFX/frei0r/LADSPA plugin loading, editable Bézier keyframe
handles, online resources (pan/Zoom/in-shot libraries). These stay planned.

---

## Session log

- **Baseline** (2026-10-11): `linux-debug` builds clean (fixed stale AUTOMOC
  cache); **697/697** tests pass.
- **Phase 7** — done (context menus, clipboard, marker UI); **709/709**.
- **Export presets + zone/range export** — done; **715/715**.
- **Subtitle import (SRT/ASS/WebVTT)** — done; **728/728**.
- **Per-clip export** — done; **731/731**.
- **Image-sequence import** — done; **740/740**.
- **`.cube` LUT import + `lut.cube` effect** — done; **755/755**.
- **Color Wheels effect (lift/gamma/gain)** — done; **758/758**.
- **Title Editor** — `TitleSpec` model, Qt renderer (`QImage renderTitleImage`),
  dialog (`TitleEditorDialog`) with font/size/colour/position/background/outline
  controls + live preview; `File → Add Title…` creates a PNG title at sequence
  resolution and appends it to the timeline at playhead (user-settable duration);
  `addTitleToMediaBin` stores the spec under `extensions["titles"][id]` so the
  design survives save/load and backs re-editing; one undo step.
  **780/780** green (780 = 758 + 22).
- **Timeline waveform display** — Qt-free `bl::waveformColumns` (envelope →
  per-column amplitude), `ClipItem` waveform painting, `TimelinePanel`
  resolver + `refreshWaveforms`, MainWindow wiring to the `AudioMeterEngine`.
  **790/790** green (790 = 780 + 10).
- **Title re-edit** — `ProjectController::updateTitle` re-renders a title's still
  in place (spec + PNG + bin name, one undo step; undo re-renders from the old
  spec, so the file stays a pure function of the design). **Edit Title…** on the
  media-bin row and on a timeline clip (both funnel into `MainWindow::editTitleClip`
  → `applyTitleEdit`), with the Duration control hidden in edit mode.
  **794/794** green (794 = 790 + 4).
- **Video scopes** — Qt-free `bl::computeScopes` (`bl_render/scopes.hpp`) fills
  luma/RGB histograms, a luma waveform grid and a chroma vectorscope in one
  pass (Rec.709, subsampled via `step`); new **Scopes dock** (`ScopesPanel` +
  `ScopeView`) with a Histogram/Waveform/Vectorscope selector, fed by a new
  `PreviewPanel::frameRendered` tap of the composited frame.
  **809/809** green (809 = 794 + 15).
- **PSNR / SSIM validation** — Qt-free `bl_render/quality_metrics`
  (`bl::compareQuality`): decodes both inputs through the existing
  `Demuxer` + `DecoderBridge` stack, normalizes to a common size/pixfmt via
  `swscale`, and reports per-frame + aggregate PSNR and windowed 8×8 SSIM
  (standard C1/C2 constants) with avg/min/max, explicit frame-count/dimension
  mismatch handling and an MSE=0 PSNR cap. **Tools → Compare Quality…**
  (`QualityMetricsDialog`, UI-only, emits a request the `MainWindow` services)
  wires it up. **823/823** green (823 = 809 + 14).
- **Bin folders / collections** — additive JSON (`ProjectData::folders` +
  `MediaBinItem::folderId`, schema stays v1; legacy projects load unchanged);
  undoable `ProjectController` folder mutators (delete rehomes media + children
  to the root) and a Media Bin folder combo + New/Rename/Delete + "Move to
  Folder" submenu. **841/841** green (841 = 823 + 18).
- **Project templates** — Qt-free `bl_timeline/project_template` model
  (`ProjectTemplate`: sequence settings + ordered track skeleton + seed track
  effects + seed titles) with four built-ins and `instantiateTemplate`;
  `Track` gained a serialised effect chain so a template can seed one;
  `ProjectController::newProjectFromTemplate` adopts an empty-path, dirty
  project; **File → New from Template…** picker lists built-ins plus user
  `*.blproj`/`*.json` under `<userDataRoot>/templates`, rendering seed titles to
  stills on creation. **855/855** green (855 = 841 + 14).
- **Transition rendering (shared composition path)** — `Compositor` now
  evaluates `Clip::transitionOut` as a first-class op ahead of the per-clip
  loop: crossfade, dissolve, fade-through-black and wipe/slide with
  alignment-aware spans (Left/Center/Right) and params (`direction`,
  `softness`); the outgoing clip's tail-only crossfade and the `findNextClip`
  helper are gone. Preview and export both composite through
  `Compositor::renderFrame`, so one path covers both. 12 new compositor tests,
  **867/867** green (867 = 855 + 12).
- **Multi-monitor full-screen preview** — **View → Full Screen Preview**
  (`Ctrl+Shift+P`, remappable) reparents the one `PreviewPanel` into a frameless
  top-level `FullScreenPreviewWindow` (`src/ui/widgets/fullscreen_preview_window.*`)
  sized to a monitor of the user's choice; Escape (or closing the window) emits
  `closeRequested`, which `MainWindow::toggleFullScreenPreview` turns back into a
  dock restore. A screen picker is shown only when `QGuiApplication::screens()`
  has more than one entry (defaulting to the first non-primary monitor), so the
  offscreen single-screen test path never blocks on a dialog. No duplicate
  engine/transport/frame tap: `preview->setWidget(nullptr)` detaches the panel
  and `takePreview()` hands it back. New tests
  `MainWindow.FullScreenPreviewActionIsRegistered` +
  `MainWindow.FullScreenPreviewTogglesDetachAndRestore`. **869/869** green
  (869 = 867 + 2).
- **Stream to IP (RTMP/UDP)** — `ExportSettings` gained `ExportTarget` +
  `streamUrl` and the Qt-free `streamFormatForUrl`/`isStreamUrl` helpers
  (`rtmp`/`rtmps` → `flv`, `udp` → `mpegts`); `ExportPlan` branches on the
  target, infers the container and constrains the codecs to H.264 + AAC;
  `Muxer::open` got an explicit-format overload (`avformat_alloc_output_context2`
  with the muxer name) with the 2-arg form delegating to it; `ExportRunner`
  passes the resolved target + format and only unlinks a partial output for
  file targets; `ExportDialog` gained the **Target** combo + stream URL row,
  forcing H.264/AAC and offering only AAC while streaming. No real-time pacing,
  no SRT. `ExportRunner.ExplicitFlvFormatOverloadMuxesFlv` proves the explicit
  format drives the FLV muxer, and `ExportRunner.StreamsUdpToLocalReceiver`
  proves the path end to end against a local POSIX UDP receiver.
  **878/878** green (878 = 869 + 9).