# Bucharest Lite Changelog

## v1.9.14 — 2026-10-14

### Added
- **PSNR / SSIM quality comparison.** **Tools → Compare Quality…** compares two
  media files (typically an original and an exported/transcoded copy) and
  reports objective quality metrics. The Qt-free `bl::compareQuality` analyzer
  (`bl_render/quality_metrics`) decodes both inputs through the existing
  `Demuxer` + `DecoderBridge` stack, normalizes them to a common size/pixel
  format, and computes per-frame + aggregate **PSNR** (dB, average/min/max,
  capped when the MSE is zero) and windowed 8×8 **SSIM** (standard C1/C2
  constants) over Rec.709 luma. Frame-count/dimension mismatches are reported
  explicitly; SSIM is a standard uniform-window approximation. `QualityMetricsDialog`
  is UI-only and emits a request the `MainWindow` services.
- **Media bin folders / collections.** Organise the media bin into folders.
  Additive JSON (`ProjectData.folders` + `MediaBinItem.folderId`; schema stays
  v1, legacy projects load at the root) and undoable controller operations
  (`addFolder`/`renameFolder`/`removeFolder`/`moveMediaToFolder`/`moveFolder`,
  each one `FunctionCommand`; deleting a folder rehomes its media **and** child
  folders to the root and moves are cycle-checked). The Media Bin gains a folder
  combo, New/Rename/Delete buttons and a "Move to Folder" submenu; the name
  filter is scoped to the current folder.
- **Project templates.** **File → New from Template…** starts a project from a
  reusable template. Qt-free `bl_timeline/project_template` carries sequence
  settings, an ordered track skeleton, optional seed track effects and optional
  seed titles; four built-ins ship (`1080p30-standard`, `4k-uhd`, `vertical-9x16`
  with a lower-third title, `audio-podcast` with two 48 kHz audio tracks).
  `Track` gained a serialized effect chain so a template can seed one, and the
  picker also lists user `*.blproj`/`*.json` templates from `<userDataRoot>/templates`.
- **Multi-monitor full-screen preview.** **View → Full Screen Preview**
  (`Ctrl+Shift+P`, remappable) detaches the single `PreviewPanel` into a
  frameless top-level window on a monitor of your choice; Escape or closing the
  window restores it into the Preview dock. There is still one preview engine,
  transport and scopes tap — the panel is reparented, never duplicated. The
  screen picker appears only when more than one display is present.

### Changed
- **Default workspace layout is now two rows.** The app opens with a compact
  two-row arrangement: the top row holds the **Media Bin**, **Preview** and
  **Scopes** docks side by side, and the **Timeline** spans the full window
  width beneath them. The **Inspector** and **Mixer** docks are unchanged but
  now start hidden (still reachable from **View**, Ctrl+3 / Ctrl+4). Scopes
  also gained a proper **View** menu entry. A `window/layoutVersion` marker
  resets the saved dock layout once so the new default takes effect on an
  existing install; your own arrangement is remembered as before afterwards.

### Removed
- **Dead proxy stub.** The non-functional `proxy_generator`
  (`include/bl_core/proxy/*` + `src/core/proxy/*`) was deleted and dropped from
  `src/core/CMakeLists.txt`; it was wired into no UI and the stale header
  declared a conflicting `ProxyJobParams`/`createProxyJob` overload. Proxy
  editing remains out of v1 scope (`docs/architecture/ARCHITECTURE.md` §12 is
  now marked post-v1).

### Notes
- **879/879** tests green (`linux-debug` + `linux-release`), and **879/879** on
  Windows (MSVC 14.44 + Qt 6.8.3, on the release VM); version bumped to
  **1.9.14**.

## v1.9.13 — 2026-10-14

### Added
- **Stream to IP (RTMP / UDP).** An export can now publish to a network
  endpoint instead of writing a file. The export dialog's **Output → Target**
  switches between *Save to File* and *Stream to IP*; the latter shows a URL
  field and disables the file path/container controls, since the container is
  inferred from the URL scheme:
  - `rtmp://` / `rtmps://` → **FLV** (e.g. a platform ingest server or a local
    `rtmp://localhost:1935/live/key` relay)
  - `udp://host:port` → **MPEG-TS** (unicast or multicast)
  - Streaming is constrained to **H.264 + AAC**; the dialog forces those codecs
    and offers only AAC while streaming, and planning rejects anything else.
  - There is **no real-time pacing** — the export sends as fast as it encodes,
    so a receiver must buffer. **SRT is not supported in v1.**
  - `ExportSettings` gained `ExportTarget` + `streamUrl` and the Qt-free
    `streamFormatForUrl`/`isStreamUrl` helpers; `ExportPlan` infers the
    container and codecs; `Muxer::open` gained an explicit-muxer-name overload
    (the 2-arg form delegates to it) and `ExportRunner` passes the resolved
    target + format, skipping partial-file cleanup for streams.
  - Tests: `ExportSettings.StreamFormatForUrl`/`IsStreamUrl`,
    `ExportRunner.StreamPlanInfersFlvForRtmp`/`StreamPlanInfersMpegtsForUdp`/
    `StreamPlanRejectsNonH264`/`StreamPlanRejectsUnknownScheme`, an end-to-end
    `ExportRunner.StreamsUdpToLocalReceiver` against a local POSIX UDP socket,
    `ExportRunner.ExplicitFlvFormatOverloadMuxesFlv` (the explicit-format
    overload selects the FLV muxer for an extension-less target), and
    `ExportDialog.StreamTargetForcesH264Aac`. **878/878** green
    (878 = 869 + 9).

## v1.9.12 — 2026-10-14

### Added
- **Transition rendering (preview + export, shared composition path).** The
  compositor now evaluates `Clip::transitionOut` toward the next adjacent clip
  as a first-class operation ahead of the per-clip pass, replacing the old
  tail-only linear crossfade:
  - **Kinds** — crossfade, dissolve (the standard alpha crossfade),
    fade-through-black (a crossfade flagged via `throughBlack` /
    `fade: "through_black"`), wipe and slide.
  - **Alignment** — `Left` spans the outgoing tail `[aEnd - dur, aEnd)`,
    `Center` straddles the cut `[aEnd - dur/2, aEnd + dur/2)`, `Right` spans
    the incoming head `[aEnd, aEnd + dur)`; the non-active side is held on its
    first/last frame.
  - **Params** — wipe/slide `direction` (`left`/`right`/`up`/`down`, default
    `left`) and wipe integer `softness` (pixels). Parameter reads are total:
    unknown/mistyped values fall back to defaults.
  - Both clips' effect chains run before blending, and the outgoing/incoming
    sides are gated exactly like the normal loop (disabled/audio-only incoming
    clips skip the transition so the outgoing clip renders normally). Preview
    and export both composite through `Compositor::renderFrame`.
  - 12 new compositor tests with exact expected bytes; **867/867** green
    (867 = 855 + 12).

## v1.9.11 — 2026-10-14

### Added
- **Kdenlive-parity timeline, phase 1: model + controller foundations**
  (no visible UI change yet — the UI phases build on these seams):
  - **Clip stream flags** `hasVideo`, `hasAudio`, `enabled` (additive JSON
    keys; older projects load as both-streams-and-enabled). The compositor
    skips disabled and audio-only clips and clips on hidden tracks, and the
    audio mixdown, meter analysis and EDL exporter skip disabled or
    audio-less clips. Duration scans in export planning and the preview
    panel deliberately ignore the flags so timeline totals do not change
    when clips are muted.
  - **Track `hidden` flag** and per-clip `setClipEnabled` (mute without
    deleting).
  - **Clip groups, markers and track structure on `Sequence`**: groups
    (JSON key `"groups"`, span video + linked-audio tracks, merge on
    overlap, prune on track removal), marker find/set/remove mutators, and
    `insert`/`remove`/`moveTrack` (removing the last video track is
    refused).
  - **`TimelineEditController` lockdown and new mutators**: layout edits on
    a locked track refuse with `"Track is locked"`; every refusal is
    reported through `lastError`. New: `setClipEnabled`, `slipClip`,
    `rollTrim`, `groupClips`/`ungroupClips`, `splitAudioVideo` (halves
    linked as a group), timeline-marker add/edit/remove,
    `insert/remove/moveTrack`, `liftZone`/`extractZone` (zone ops skip
    locked tracks).
  - **`timeline_geometry::LaneOrder`** (Split / Mixed / MixedReverse) — the
    flat-index ↔ lane mapping seam for the upcoming A/V lane UI; `Split`
    reproduces the current layout exactly.
- **Kdenlive-parity timeline, phase 2: interactive track headers** (the
  paint-only header column is now a live control surface):
  - `TrackHeader` is a container of one `TrackHeaderRow` widget per lane,
    repositioned from the shared lane geometry and kept in vertical scroll
    sync with the view. Each row shows the track name and kind-gated toggles:
    lock + hide for video; lock + mute + solo for audio.
  - Active-track highlight (`TimelinePanel::activeTrack`/`setActiveTrack`,
    `activeTrackChanged`); clicking a clip or an empty lane makes that track
    current.
  - Inline rename (double-click the name → `QLineEdit`, Enter commits / Esc
    cancels), undoable.
  - Per-row context menu (rename, insert above/below, move up/down, lock,
    hide/mute, solo, delete) plus drag-to-reorder within a kind; `+V`/`+A`
    buttons in the timeline top bar append empty tracks.
  - `MixerController::setTrackHidden`/`setTrackName` (mirroring the existing
    per-track mutators, one undo command each). Every header action is a
    single undo step and refusals route through `statusMessage`.
- **Kdenlive-parity timeline, phase 3: lane order + zoom-to-fit**:
  - `TimelinePanel::laneOrder`/`setLaneOrder` (+ `laneOrderChanged`) with
    `laneRect`, `flatAtY`, `TrackHeader::layoutRows` and `rebuildFromModel`
    now order-aware. The top bar gained a **Normal / Mixed / Mixed reverse**
    combo; `contentHeight` is unchanged (heights stay per-kind).
  - A logarithmic zoom `QSlider` spanning 0.25–64 px/frame (kept in sync
    with the +/- buttons and Ctrl+wheel) and a **Fit** button
    (`fitZoomToViewport`, clamped) that sizes the sequence to the viewport;
    the View menu's **Fit Timeline to View** (`Ctrl+Shift+F`) shares the slot.
  - `timeline_geometry` exposes `kContentMargin` (shared with
    `contentWidth`) so Fit solves for the viewport width without a
    duplicated constant.
  - The layout choice persists in QSettings (`timeline/laneOrder`), written
    on every change and restored on startup.
- **Kdenlive-parity timeline, phase 4: toolbar & operation modes**:
  - `TimelineState` value struct (`TimelinePanel::timelineState`/
    `setTimelineState` + per-field helpers) covering edit mode,
    insert/overwrite, ripple, snap and lane order, with one
    `timelineStateChanged` signal.
  - A **Select / Range / Ripple / Roll / Slip / Spacer** edit-mode combo and
    **Insert**/**Ripple** toggle buttons in the timeline top bar.
    - `Range` drags on the ruler set a panel-level zone
      (`setZone`/`clearZone`/`zoneIn`/`zoneOut` + a `ZoneItem` overlay),
      the groundwork for Phase 5's lift/extract.
    - `Ripple` drags call `rippleMove`; `Roll` seams call `rollTrim`;
      `Slip` bodies call `slipClip`; `Spacer` drags call
      `addSpacer`/`removeSpacer`.
    - Select drags use `insertMove` when Insert is on (otherwise the
      overwrite `groupMove`), and the Ripple toggle makes plain Delete
      ripple.
  - Four new atomic controller primitives: `insertMove`, `rippleMove`,
    `addSpacer`, `removeSpacer` (one `FunctionCommand` per edit).
  - The edit state persists in QSettings (`timeline/editMode`,
    `timeline/insertMode`, `timeline/ripple`, `timeline/snap`), written on
    every change and restored on startup.
- **Kdenlive-parity timeline, phase 5: timeline zone (in/out)**:
  - The top bar gained **Set In** / **Set Out** / **Clear Zone** buttons
    (they act at the playhead); `TimelinePanel::setZoneIn`/`setZoneOut`
    clamp so `in <= out` and the first call opens a zero-length zone.
  - The translucent `ZoneItem` band now has grabbable in/out edges on the
    ruler: pressing within 6 px of an edge drags it, and the hover cursor
    becomes a horizontal resize near either edge in every edit mode.
  - **Lift Zone** / **Extract Zone** (`liftZone`/`extractZone` wrappers over
    the existing controller ops) remove the zone range — lift leaves a gap,
    extract ripples later clips left — skipping locked tracks, each as one
    undo step. A missing/empty zone refuses with a status message.
  - `MainWindow` added a **Timeline** menu: Set Zone In (`I`), Set Zone Out
    (`O`), Clear Zone (`Shift+Z`), Lift Zone (`Ctrl+Shift+L`) and Extract
    Zone (`Ctrl+Shift+X`).
- **Kdenlive-parity timeline, phase 6: A/V linking**:
  - **Linked selection** — Ctrl+click on a clip expands the selection to the
    clip's whole group (`TimelinePanel::setSelection` → `expandSelection` over
    `Sequence::groupMembers`); a link badge is drawn on `ClipItem` for grouped
    clips.
  - **Group / ungroup** — `Ctrl+G` / `Ctrl+Shift+G` in `TimelineView`, backed
    by `TimelineEditController::groupClips`/`ungroupClips` (one undo step
    each).
  - **Linked edit** — moves go through `groupMove`, trims through `trimLinked`
    (so the video and audio halves stay aligned) and deletes through the
    atomic `removeClips`, making every linked edit a single undo step.
  - **Auto-split on drop** — an A/V clip dropped on a video lane creates
    linked video + audio halves, toggleable from the timeline A/V button.
- **Kdenlive-parity timeline, phase 8: shortcuts, docs, tests**:
  - Timeline actions (Fit Timeline to View, zone in/out/clear,
    lift/extract) are registered in `ActionRegistry` with remappable key
    chords.
  - `TimelineState` (edit mode, insert/overwrite, ripple, snap, `autoSplitAv`)
    and lane order persist to QSettings and round-trip across restarts
    (`timeline/autoSplitAv` defaults to `true`, written on every
    `timelineStateChanged`, restored in `maybeRestoreLayout`).
  - Docs refreshed (`FEATURES.md`, `PROJECT_STATUS.md`, `docs/user-guide.md`,
    `README.md`); UI suites green in `linux-debug` + `linux-release`,
    core/model suites green under `linux-tsan`.
- **Kdenlive-parity timeline, phase 7: context menus, clipboard, marker UI**:
  - `TimelineEditController` gained a selection clipboard
    (`setClipboard`/`clearClipboard`/`hasClipboard`/`pasteClips`): paste
    reproduces each copied clip's offset from the earliest copied start, drops
    onto the source lane, the active lane, or the first compatible lane with
    room, and is a single undo step. `TimelinePanel` wrappers
    (`copySelection`/`cutSelection`/`pasteAt`/`pasteAtPlayhead`) select the
    freshly pasted clips.
  - `TimelineView::contextMenuEvent` adds marker (rename/delete), ruler
    (add marker / set zone in-out / clear zone), clip (cut, copy, paste, split,
    delete, ripple delete, enable/disable, zone in/out) and empty-area (paste,
    add marker, select all) menus.
  - Marker UI on top of the existing marker model/render: add at playhead (the
    `M` shortcut and Timeline → Add Marker), add at click (ruler/empty-area
    menus), rename and delete from the marker menu. Each is one undo step.
  - New remappable actions `timeline.cut`/`timeline.copy`/`timeline.paste`/
    `timeline.marker_add` (`Ctrl+X`/`Ctrl+C`/`Ctrl+V`/`M`), also on the Edit
    and Timeline menus; `Ctrl+C/X/V` inside a text field still edits text.
- **Export presets and range export**:
  - `exportPresets()` (Qt-free) ships YouTube 1080p/4K, Vimeo 1080p, WebM
    (VP9), DVD-sized MPEG-4, Blu-ray-sized H.264 and Mobile 720p profiles; the
    export dialog gained a **Preset** combo that applies an encode profile
    without touching the output path or range.
  - The export dialog gained a **Range** combo: Entire project / Timeline zone
    / Selected clip. `setZone`/`setClipRange` enable the options only when a
    zone or a clip selection exists, and the values flow through
    `ExportRange::InOut`/`ExportRange::Clip` into `frameRange`. MainWindow
    feeds the timeline zone and the selected clip's span into the dialog.
- **Image-sequence import**:
  - New Qt-free `bl::detectImageSequence`
    (`include/bl_core/image_sequence.hpp`): a dropped frame whose name ends in
    a ≥2-digit run (`frame_0001.png`) expands to the whole sequence when the
    directory holds siblings with the same stem, digit width and extension.
  - `ProjectController::addToMediaBin` stores a sequence once under its FFmpeg
    pattern path (`frame_%04d.png`) with a `<stem> (N frames)` display name,
    and dedupes the sibling frames onto that entry; the renderer's image2
    demuxer decodes the whole run from the pattern. The media "Add…" dialog
    now offers an Image Sequences file filter.
- **Color Wheels effect**:
  - New registered `color_wheels` effect ("Color Wheels") with per-channel
    lift / gamma / gain (9 sliders, defaults are identity), applied directly on
    the pixel buffer; complements the existing `levels_curves` effect, which
    covers curves.
- **`.cube` LUT support**:
  - New Qt-free `bl::parseCubeLut` / `loadCubeLutFile` / `sampleCubeLut`
    (`include/bl_render/cube_lut.hpp`): parses Adobe `.cube` 1D and 3D LUTs
    (TITLE, LUT_1D_SIZE/LUT_3D_SIZE, DOMAIN_MIN/MAX, `#` comments), rejects a
    table whose length disagrees with its size, and samples with trilinear
    (3D) or linear (1D) interpolation and edge clamping.
  - New registered effect `lut.cube` ("LUT (.cube)") that applies the LUT
    named by its `path` param, with a process-wide parsed-LUT cache so the
    file is read once. The Inspector's Add menu opens a file dialog
    (`addCubeLutEffect`) when a LUT effect is chosen instead of inserting an
    inert effect, and the raw params JSON shows the path so it can be
    re-pointed by hand.
- **Subtitle import (SRT / ASS / WebVTT)**:
  - New Qt-free `bl::parseSubtitles` / `parseSubtitleFile` /
    `subtitleFormatForPath` (`include/bl_timeline/subtitle_import.hpp`): parses
    SRT, WebVTT (header/NOTE/STYLE blocks skipped, cue settings and
    `MM:SS.mmm` short form handled, tags stripped) and ASS/SSA
    (`[Events]`/`Format:` column order honored, override tags stripped, `\N`
    breaks, `\an` alignment → position). Cues are sorted and malformed/inverted
    ones skipped.
  - `TimelineEditController::importSubtitleCues` turns parsed cues into
    soft-subtitle clips on a new dedicated "Subtitles" video track (one undo
    step, overlapping cues skipped); MainWindow gained **File → Import
    Subtitles…** (`file.import_subtitles`).
- **Title editor (text titles as still images)**:
  - New Qt-free `bl::TitleSpec` + `TitleColor`
    (`include/bl_timeline/title.hpp`) describing a text title (font family,
    size, bold/italic, text/background/outline colours, alignment, fractional
    anchor, padding, outline width, line spacing). It round-trips through
    `nlohmann::json` and `clampTitleSpec` bounds every numeric field, so a
    hand-edited project cannot produce a degenerate render.
  - New UI rasteriser `bl::ui::renderTitleImage` / `saveTitlePng` /
    `titleOutputDirectory` (`src/ui/title/title_renderer.*`) draws the spec to a
    transparent ARGB32 still; the spec's pixel metrics are authored against a
    1080p reference height and scaled to the canvas, so the same spec renders
    correctly at any resolution.
  - New `TitleEditorDialog` (`Add Title…`, `file.add_title`) edits a spec with a
    live preview at the sequence resolution (text, font, size, bold/italic,
    colours, alignment, X/Y position sliders, background, padding, outline and
    an on-timeline duration, default 5 s).
  - `ProjectController::addTitleToMediaBin` adds the rendered PNG *and* stores
    the spec under `extensions["titles"][id]` as one undo step
    (`titleSpecFor` / `mediaIdForPath` read it back); removing the item drops
    the spec too. `TimelinePanel::addMediaToTimeline` gained an optional
    duration override so a still title holds for its whole span (the decoder
    repeats the last frame).
- **Timeline waveform display on audio clips**:
  - New Qt-free `bl::waveformColumns` (`include/bl_audio/audio_meter_analysis.hpp`)
    reduces an analyzed `AudioMeterEnvelope` to one normalized amplitude per
    output column over a source range (max |amplitude| across channels and
    windows, clamped to `[0, 1]`; ranges past the analysis read as silence).
  - `ClipItem` gained `setWaveform()` and draws the reduced shape inside each
    audio-bearing clip, under the clip name (video-only clips draw none).
  - `TimelinePanel::setWaveformResolver()` / `refreshWaveforms()` let the
    panel resolve each clip's envelope; `MainWindow` wires the shared
    `AudioMeterEngine`'s cache lookup in and repaints the clips as background
    analysis settles (no full rebuild, so an in-progress drag is not
    cancelled).
- **Title re-editing (edit-in-place)**:
  - `ProjectController::updateTitle` replaces a title's stored spec and
    re-renders its still from the spec at the sequence resolution as one undo
    step; undo re-renders from the *old* spec, so the PNG stays a pure function
    of the design. It refuses (changing nothing) when the id is not a title, the
    stored spec is unreadable, or the still cannot be written.
  - **Edit Title…** on a title's media-bin row (`MediaBinPanel::editTitleRequested`)
    and on a title clip's context menu (`TimelinePanel::editTitleRequested`) both
    open the Title Editor pre-filled with the saved design
    (`MainWindow::editTitleClip`, with the dialog-free tail `applyTitleEdit`).
    The Duration control is hidden in edit mode (`TitleEditorDialog::setDurationVisible`)
    because the clip's length is owned by the timeline.
- **Video scopes (Scopes dock)**:
  - New Qt-free `bl::computeScopes` (`include/bl_render/scopes.hpp`) extracts
    three views from one composited BGRA frame in a single pass: Rec.709 luma +
    per-channel RGB 256-bin **histograms**, a `waveformColumns` x 256 luma
    **waveform** grid, and a **vectorscope** grid over Rec.709 Cb/Cr. A `step`
    parameter subsamples the frame so a 1080p image costs about the same as a
    small one.
  - New **Scopes** dock (`ScopesPanel` + `ScopeView`, Ctrl+6) with a
    Histogram / Waveform / Vectorscope selector, tapped from the composited
    preview frame via the new `PreviewPanel::frameRendered` signal. The
    subsample stride is chosen from the frame size (`ScopesPanel::sampleStep`),
    and an empty frame clears the views.

### Fixed
- **Clip edges and markers did not participate in snapping**:
  `TimelineEditController::snapToNearest` returned after frame-grid
  rounding before its clip-edge/marker loops ever ran (the rounded frame
  always compared equal), so drop/move snapping never considered clip
  boundaries or markers. It now picks the nearest edge/marker within the
  pixel tolerance and falls back to frame rounding, and marker positions
  are snap candidates alongside clip edges.

## v1.8.14 — 2026-10-10

### Added
- **Media Bin context menu** (`FEATURES.md` UI & Workflow): right-clicking a
  media row offers **Add to Timeline** and **Remove from Project**. The policy
  was already `Qt::CustomContextMenu` (UI-4) but no
  `customContextMenuRequested` connection existed, so right-clicking did
  nothing. "Add to Timeline" probes the media and appends a clip at the end of
  the first compatible track (video → the first video lane, audio-only → the
  first audio lane; an empty lane starts at 0) as a single undoable command
  through `TimelinePanel::addMediaToTimeline()`. Every refusal — media missing
  from the project, unreadable file, no streams, unknown duration, no
  compatible track, occupied lane end — is reported in the status bar instead
  of failing silently. A click in the empty area shows no menu; right-clicking
  a non-selected row promotes it to the selection first.
- **Drag feedback and precise drop refusals on the timeline**: dragging a row
  out of the Media Bin now shows a pixmap of the row (thumbnail + name +
  detail, painted through the item delegate) instead of a bare cursor, and
  every rejected drag or drop states *why* in the status bar: dropped on the
  ruler, spot occupied, no compatible track at the cursor, unknown duration,
  unreadable file, a file-manager batch with unreadable members (such a batch
  still drags, with a warning), or no room for all dropped files. Drag-move
  messages are deduplicated (`TimelineView::reportDropProblem`) so the
  per-pixel event stream cannot flood the status bar.

### Fixed
- **Double-clicking a Media Bin row deleted it**: `itemActivated` was wired to
  `removeSelected()`, so double-click or Enter removed the row from the
  project — almost certainly never the intent of "activating" a clip. It now
  emits the same `addToTimelineRequested(id)` signal as the new context menu,
  and the row stays in the project. Removal remains available (undoable)
  through the Remove button and the context menu.

## v1.8.12 — 2026-10-06

### Added
- **Drag-and-drop media import from the file manager** (`FEATURES.md` UI &
  Workflow): media files could only be added through the `Add...` file dialog.
  Neither the Media Bin nor the timeline accepted an external drop — the bin
  list was `DragOnly` and the timeline matched only the internal
  `application/x-bucharest-media` payload — so dropping files from Nautilus,
  Explorer or any other shell silently did nothing. Both are now drop targets for
  `text/uri-list`:
  - **Media Bin** imports every dropped local file through the existing
    undoable `ProjectController::addToMediaBin`. Remote (`http(s)://`) URLs and
    directories are filtered out by the shared
    `media_bin_detail::localFilePaths()` helper. The list moved to `DragDrop`
    mode but still *rejects* the internal media-id payload, because accepting it
    would let `QListWidget` reorder rows on a drop.
  - **Timeline** imports *and* places. Files are probed on drag-enter so the
    placement ghost shows real durations before the mouse button is released,
    and nothing is imported until the drop actually commits — an abandoned drag
    leaves the project untouched. Video-only media routes to video lanes,
    audio-only to audio lanes, A/V to either. Multiple files place in sequence
    starting at the drop point, spilling onto the next compatible lane when one
    is occupied; the first file must fit exactly where it was dropped, matching
    the existing single-item behaviour of rejecting rather than silently
    relocating. A file-manager drop is one `UndoStack` macro, so N imports plus
    N clips revert with a single Ctrl+Z.

### Fixed
- **Dropping media files onto the Media Bin or the timeline did nothing**: see
  the `Added` entry above — the feature was listed in `FEATURES.md` but had
  never been implemented, and the `Unreleased` docs described only the
  media-bin-row → timeline drag (UI-10).
- **Distro-shipped codec plugins installed to an architecture-dependent path
  under `/usr/share`**: `lintian` rejected every build with
  `arch-dependent-file-in-usr-share` (9 errors, one per plugin), which blocks a
  Debian upload. The install location and the runtime scan root were also two
  independent hardcoded copies of the same string. Both now derive from a single
  `BL_SYSTEM_PLUGIN_DIR`, set from `GNUInstallDirs`, so they cannot drift again.
  Plugins now land in `${libdir}/bucharest-lite/plugins` — the multiarch triplet
  on Debian/Ubuntu, plain `libdir` elsewhere — and the RPM spec was moved off
  `%{_datadir}` to match.
- **Flatpak could never find its own plugins**: `paths.cpp` looked in
  `/usr/share/bucharest-lite/plugins`, which does not exist inside the sandbox
  where the prefix is `/app`, so the bundle shipped nine plugins the app never
  scanned for. Deriving the path from `CMAKE_INSTALL_PREFIX` fixes this as a
  side effect. The Flatpak smoke test only asserted the process stayed alive, so
  it never caught the missing plugins.
- **`no-manual-page`**: added `docs/bucharest-lite.1` and an
  `override_dh_auto_install` in `debian/rules`, since `dh_installdocs` does not
  pick up roff sources.
- **Invalid maintainer address in `debian/changelog`**: the older entries carried
  a non-routable mail host and were flagged as
  `bogus-mail-host-in-debian-changelog`. The package author is now uniformly
  `Wictoria Zajac <wiczajac666@github.com>` across `debian/control`,
  `debian/changelog`, `debian/copyright` and the RPM `%changelog`, replacing
  both the invalid address and the divergent `Harlemi` attribution.
- **`version.rc.in` broke the Windows resource compile**: a markdown backtick in
  a `;` comment is not inert to `rc.exe`, which runs the MSVC preprocessor over
  `.rc` files. It failed with `RC2018: unknown character '0x60'` and
  `RC1116: RC terminating after preprocessor errors`, killing the link step.

### Added
- **Matroska export in the export engine**: the muxer now picks the soft-text
  subtitle codec from the resolved output format instead of assuming MP4 —
  `MOV_TEXT` for MP4/MOV, SubRip for Matroska, WebVTT for WebM — so a timeline
  with subtitle clips exports to `.mkv` with a real, recoverable subtitle
  track. Matroska is accepted as an export container and carries every audio
  codec the app offers, including AAC. This is the engine half only: MKV is not
  yet selectable in the export or batch dialogs.

### Removed
- **`MKVMuxer`**: an unwired, never-called duplicate of the generic `Muxer`
  that hardcoded H.264/AAC and could not carry codec private data. Matroska
  goes through the same `Muxer` as every other container.

### Changed
- **Plugin ABI v2 → v3**: `BlCodecPlugin` gains an optional trailing
  `get_extradata()` entry and `BlCodecConfig` gains `enc_flags` with
  `BL_ENCFLAG_GLOBAL_HEADER`. Encoded packets never carried codec private data,
  so `AVCodecParameters::extradata` was left empty and containers that must
  write a self-contained track header could not. The FFmpeg codec plugins now
  publish H.264 SPS/PPS, AAC `AudioSpecificConfig` and the equivalents for the
  other codecs, and `ExportEngine` hands them to the muxer. Plugins that do not
  implement the query simply leave the slot `NULL`; the host handles that.
  Note that `BL_ENCFLAG_GLOBAL_HEADER` is what makes H.264 publish at all —
  libx264 otherwise repeats its parameter sets in-band on every keyframe. MP4
  output is unaffected (it still carries a correct `avc1` configuration record);
  this is the prerequisite for Matroska export, which is still not exposed in
  the export UI.

## v1.7.38 — 2026-10-02

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
> **Accuracy note:** a number of entries below were aspirational at release and
> are **not** shipped in the codebase (which is true of every release through
> 1.1.1): GPU-accelerated OpenGL compositing, proxy editing, SRT/ASS subtitle
> import & burn-in, color wheels/curves, title animation, image sequences, and
> scopes. The v1.1.1 section above is accurate; see `README.md` (Features) and
> `docs/user-guide.md` (Known limitations) for the shipped-vs-planned split.
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
