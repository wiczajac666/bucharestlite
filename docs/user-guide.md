# Bucharest Lite — User Guide

## Workspace layout

Bucharest Lite opens with a compact two-row workspace:

- **Top row** — the **Media Bin**, **Preview**, **Source Monitor** and
  **Scopes** docks, side by side.
- **Bottom row** — the **Timeline**, spanning the full window width.

Two more docks, the **Inspector** (clip properties, keyframes, effects and
masks) and the **Mixer** (per-track and master levels), start hidden to keep
the workspace uncluttered. Show them — and toggle any panel — from the **View**
menu, or with their shortcuts: Ctrl+1 Media Bin, Ctrl+2 Preview, Ctrl+3
Inspector, Ctrl+4 Mixer, Ctrl+5 Timeline, Ctrl+6 Scopes, Ctrl+7 Undo History,
Ctrl+8 Source Monitor. Drag dock title bars to rearrange the workspace; your
layout and which panels are visible are remembered between sessions.

## New from a template

`File → New from Template…` starts a project from a reusable setup instead of
the plain one-track default. Pick a template, give the project a name, and the
new project is created with the template's timeline already in place.

- **What a template carries.** The sequence settings (resolution and frame
  rate, plus the audio sample rate), the track skeleton (how many video and
  audio tracks, in lane order), and any seed effects and titles the template
  ships with. Media is never carried over — the new project starts with an
  empty Media Bin.
- **Built-in templates.** Four ship with the application:
  - **1080p 30fps Standard** — 1920×1080 at 29.97fps, one video + one audio
    track. The everyday starting point.
  - **4K UHD** — 3840×2160 at 29.97fps, one video + one audio track.
  - **Vertical 9:16** — 1080×1920 at 29.97fps (social/reels/stories) with a
    seed **Lower Third** title that is rendered and appended automatically.
  - **Audio Podcast** — a cover-art video track plus two 48kHz audio tracks
    (host and guest), with an identity colour correction seeded on the cover
    track.
- **Seed titles.** A template that includes a title renders it to a still at
  the sequence resolution and appends it to the timeline for you — just like
  `File → Add Title…`, and it can be re-edited the same way. If a title cannot
  be rendered it is skipped with a status-bar message rather than failing the
  whole action.
- **Your own templates.** Save any project with the sequence and track layout
  you want, then drop a copy of its `.blproj` (or `.json`) file into
  `<user data directory>/templates/`. It then appears in the picker alongside
  the built-ins, under its project name. Only the sequence settings and track
  layout are taken from it; media and clips are ignored. Files that cannot be
  read or parsed are skipped.
- **Saving.** A project created from a template is unsaved (no file path yet)
  and is marked as having unsaved changes, so you are prompted for where to
  save it. `Ctrl+S`/`Save As…` works as usual.

## Importing Media

Media reaches the project three ways — pick whichever is convenient:

- **Image sequences.** Importing a still frame whose name ends in a padded
  number (`frame_0001.png`) automatically pulls in the whole sequence: the
  Media Bin stores one entry (`frame (240 frames)`) built from the FFmpeg
  pattern (`frame_%04d.png`), and the timeline decodes every frame as video.
  Dropping the other frames is harmless — they map to the same entry. A lone
  numbered image with no siblings stays an ordinary single-frame clip.

- **Drag and drop onto the Media Bin.** Drag one or more files from your file
  manager onto the Media Bin dock and they are imported immediately. Dropping a
  file that is already in the bin does nothing (it is not duplicated); dropping
  a folder is ignored rather than treated as media.
- **Drag and drop onto the timeline.** Dropping files directly on the timeline
  imports *and* places them: the first file goes exactly where you dropped it,
  and any further files in the same drop follow it in sequence. A green ghost
  previews the placement; a red cursor means the spot is occupied, and the drop
  will be rejected.
  - Video-only files land on video tracks, audio-only files on audio tracks, and
    files with both can go on either. When a later file has no room left on the
    track you dropped onto, it flows onto the next compatible track below.
  - Importing and placing is a **single undo step** — one Ctrl+Z puts the
    timeline and the Media Bin back exactly as they were.
- **The `Add...` button**, which opens a multi-select file dialog.

Importing from the file manager onto the timeline also adds the files to the
Media Bin, so they show up in the filter and can be dragged again later.

Dragging a row *out* of the Media Bin onto the timeline also works, and does not
re-import anything.

If you would rather not drag at all: **right-click a row in the Media Bin and
choose `Add to Timeline`** (or simply double-click the row). The clip is
appended at the end of the first compatible track — video material to the top
video track, audio-only material to the first audio track — as a single undo
step. The right-click menu also offers `Remove from Project`. When something
cannot be placed (the spot is occupied, no compatible track, the file cannot
be read), the status bar says exactly why instead of failing silently.

## Media bin folders (collections)

The Media Bin can be organised into **folders** (collections). Folders form a
tree and are saved with the project — legacy projects simply open with
everything at the root.

- **Show a folder.** The combo at the top of the Media Bin selects which folder's
  contents the list shows. The first entry, **All / Root**, is the project root;
  every folder follows, with nested folders shown as `Parent / Child`. The name
  filter searches only within the selected folder.
- **Create a folder.** Click **New Folder** and type a name. The new folder is
  created *inside* whatever is currently selected (the root when **All / Root**
  is selected) and becomes the selection.
- **Rename or delete a folder.** With a non-root folder selected, **Rename
  Folder** and **Delete Folder** become enabled. Rename prompts for a new name.
  Deleting a folder
  **does not delete any media**: items that lived in it — and any child folders —
  are moved back to the project root, so nothing is lost.
- **Move media into a folder.** Right-click a row and pick **Move to Folder**,
  then choose the destination (root or any folder). Drag-into-folder is not
  supported in v1 — the context menu is the supported path.
- Every folder action (create/rename/delete/move) is a single undo step
  (`Ctrl+Z`), and so is moving an item between folders.

## Subtitles

Bucharest Lite treats subtitles as *soft* text: a subtitle lives on a clip
(a `subtitleText` + optional style) and is exported as a real text stream
(MOV_TEXT for MP4, SubRip for Matroska, WebVTT for WebM) — never burned into
the picture.

- **Type a caption on a clip.** Select a clip and edit the **Subtitle** field in
  the Inspector. The clip shows a `»S«` badge in the timeline.
- **Import a subtitle file.** `File → Import Subtitles…` accepts `.srt`, `.ass`,
  `.ssa`, `.vtt` and `.webvtt`. The importer reads the cues, sorts them and
  places them on a new dedicated **Subtitles** video track (one undo step).
  Cues that would overlap an existing clip on the target track are skipped, and
  a file with no usable cues reports so in a dialog instead of failing
  silently. Supported syntax: SRT `HH:MM:SS,mmm`, WebVTT (header, `NOTE`/`STYLE`
  blocks skipped, cue settings and `MM:SS.mmm` short form handled, markup
  tags stripped) and ASS/SSA (`[Events]`/`Format:` column order, override tags
  stripped, `\N` breaks, `\an` alignment → position).

## Titles

A title is an image-based title clip: text rendered onto a transparent PNG
that the compositor blends as a video layer (alpha channel is preserved, so the
video underneath shows through). Titles are created from `File → Add Title…`
(the same place as Subtitle import), which opens the **Title Editor** dialog.

- **Create a title.** `File → Add Title…` opens the Title Editor. Enter your text,
  choose a font and size, pick a colour, set the alignment and anchor position
  (horizontal/vertical fraction 0–100% of the canvas), enable a background
  rectangle with its own colour and padding, and add a text outline for contrast
  over video. The live preview on the right shows the result at the current
  sequence resolution (font/padding/outline are authored against a 1080p
  reference, so the same title is correct at any output size). Click **OK** to
  render the PNG, add the still to the media bin and place a clip on the
  timeline at the playhead. The **Duration** spin box (default 5 s) sets the
  clip length.
- **Use a title.** A title is an ordinary clip: it lands on the first video
  track with room and behaves like any other clip (move, trim, ripple, fade,
  stack it on a track above your video). Its transparent background means the
  video below shows through.
- **Edit a title.** Right-click a title — either its media-bin row or a clip on
  the timeline — and choose **Edit Title…**. The Title Editor re-opens with the
  saved design, and on accept the still is re-rendered in place so every clip
  using it shows the new text (one undo step; the **Duration** control is hidden
  because the clip's length is set on the timeline). The media-bin entry takes
  the new text's first line, while a clip already on the timeline keeps the label
  it was created with. A title is only offered this action when it actually has
  a stored design.
- **Where it lives.** The rendered PNG is written to the Bucharest Lite data
  directory (see `titleOutputDirectory()`), and the title's design is also saved
  in the project file (`extensions["titles"]`, keyed by the media id) so it
  survives save/load and backs the Edit Title flow. Removing the media-bin entry
  drops both.

## Timeline Editing

- **Clipboard.** `Ctrl+C` / `Ctrl+X` / `Ctrl+V` copy, cut and paste the current
  selection (also on the **Edit** and **Timeline** menus). Multiple clips keep
  their relative spacing; each paste is a single undo step and the pasted clips
  become the new selection.
- **Right-click menus.** Right-click a **clip** (cut, copy, paste, split at
  playhead, delete, ripple delete, enable/disable, set zone in/out), the
  **ruler** (add marker here, set zone in/out, clear zone), empty lane space
  (paste, add marker, select all), or a **marker** (rename, delete).
- **Markers.** Press `M` (or **Timeline → Add Marker**) to drop a marker at the
  playhead, or use the ruler/empty-area menu to add one where you clicked.
  Rename or delete a marker from its own right-click menu; **Edit Comment…**
  adds a multi-line note. A marker's label and comment appear in a small chip
  under its ruler flag — toggle them with the **Comments** button in the
  timeline toolbar or **View → Show Marker Comments**. Press `]` / `[` (or
  **Timeline → Next Marker / Previous Marker**) to jump the playhead between
  markers, scrolling the timeline to keep the target visible. Markers are
  undoable and take part in snapping.
- **Navigation aids.** The position readout in the timeline toolbar and the
  ruler labels show the playhead in timecode by default; toggle **Frames** (or
  **View → Show Positions in Frames**) to display plain frame counts instead.
  The vertical slider at the right edge of the timeline scales lane heights for
  tall projects (a view zoom — it does not change track heights in the model).
  Both settings are remembered between sessions.

### Edit tools (edit modes)

The **edit-mode picker** in the timeline toolbar (or **Cycle Edit Mode**, `E`)
selects how a drag on a clip behaves. The current mode is saved with the layout.

- **Select** (default): move/trim clips and marquee-select empty space.
- **Range**: drag on the ruler to set the timeline in/out zone.
- **Ripple**: dragging a clip shifts later clips to close the gap it leaves.
- **Roll**: drag the seam between two adjacent clips to move the cut in both
  directions; their combined span stays fixed.
- **Slip**: drag a clip to move its source in/out window inside a fixed timeline
  frame.
- **Slide** (`Y`): drag a clip between its two neighbors; their facing edges
  are trimmed by the same amount so the clip's in/out and the overall span stay
  fixed. Requires adjacent clips on both sides, is bounded by neighbor spans
  and source limits, snaps to frames/edges, and is one undo step.
- **Spacer**: drag on empty space (or a clip) to insert or remove time.
- **Razor** (`R`): click a clip to split it at the pointer position; refusals
  (e.g. a locked track) are reported in the status bar.

Locked tracks refuse layout edits — move, trim, split, delete, slip, slide and
roll — in every mode.

### Three-point editing

The **Source Monitor** dock sits next to the preview and follows the Media Bin
selection, showing that item's source frame and holding its in/out marks.

- **Mark the source.** Use **In (Alt+I)**, **Out (Alt+O)** and **Clear** in the
  monitor, or the same shortcuts from anywhere in the main window. Marks are
  clamped to the source duration; with no marks the whole source is used.
- **Insert** (`,` or the monitor button) places the marked range at the
  timeline playhead and pushes later material on the destination track to the
  right. **Overwrite** (`.`) places it at the playhead and replaces whatever it
  covers. **Insert/Overwrite at Zone** places the marked range at the timeline
  zone-in instead (requires a set timeline zone).
- **Destination.** The current track decides where the edit lands: audio-only
  sources go to an audio track, video or A/V sources to a video track.
- **Linked A/V.** An A/V source splits into video and audio halves that are
  grouped and count as a single undo step; split-off timeline fragments get
  fresh clip ids so their track settings stay independent.

## Undo History

Every edit is pushed onto the undo stack, and the **Undo History** dock lets
you browse and jump through that history.

- Open it via **View → Undo History…** or press `Ctrl+7`. The dock shows one
  entry per undo step (command name + approximate size; macros expand into
  indented children), marks the current position, and greys out future
  (redo) states.
- Click an entry to jump the undo stack to that state (`UndoStack::jumpToIndex`);
  the timeline follows and redo steps forward from there. **Clear History**
  empties the stack.

Undo history is **not** persisted in a project file (documented limitation).

## Effects & Color

Select a clip and use the Inspector's **Effects** group to stack effects. The
**Add…** menu lists every registered effect; each effect gets its own row with
an enabled checkbox, reorder buttons and parameter sliders (or a raw params
JSON field when it has no numeric sliders).

- **LUT (.cube).** `Add… → LUT (.cube)` opens a file picker and applies the
  chosen Adobe `.cube` colour LUT to the clip. Both 1D and 3D LUTs are
  supported (3D sampled with trilinear interpolation, `DOMAIN_MIN`/`DOMAIN_MAX`
  honoured). The file is read once and cached; the effect's params JSON shows
  the `path`, so it can be re-pointed by editing that field.
- **Color Wheels.** `Add… → Color Wheels` adds a per-channel lift / gamma /
  gain grade (nine sliders). Defaults are identity; use it for shadow lift,
  highlight roll-off and midtone gamma. **Levels / Curves** handles tonal
  curves via its black/white/gamma plus five control points.

## Masks (roto)

Select a clip and use the Inspector's **Masks** group to limit where that clip
is visible. Masks are polygon shapes: each one multiplies the clip's alpha
after its effect chain, so the masked-out area shows the tracks below instead
of the clip.

- **Add / remove.** **Add** creates a mask with a default rectangle; **Remove**
  deletes the selected mask. Every change is one undo step.
- **Shape.** The point table lists the polygon's vertices as normalised
  coordinates (0–1 across the frame), so a mask follows the project resolution
  rather than baking in preview pixels. Edit x/y cells in place, **Add point**
  to append a vertex, or remove the selected point. A polygon needs at least
  three points; while it has fewer it covers the whole frame so an in-progress
  mask never blanks the clip.
- **Smooth.** Replaces straight edges with a closed Catmull-Rom spline through
  every point, giving curved roto shapes without bezier handles.
- **Feather.** Softens the mask edge by a fraction of the shorter frame side
  (0 is a hard edge).
- **Opacity / invert / enabled.** Opacity scales coverage inside the mask,
  invert flips inside and outside, and enabled toggles a mask without deleting
  it. Several masks on one clip combine by union (the highest coverage wins).
- Masks are saved with the project and applied identically in preview and
  export.

## GPU compositing

Layer blending can run on the GPU through an OpenGL 3.3 backend. It is
**opt-in**; the portable CPU compositor remains the default.

- Set `BL_COMPOSITOR=gpu` (or `auto`) before launching. Decoding, effects and
  masks still run on the CPU; only the per-layer blend moves to the GPU, using a
  GLSL shader that matches the CPU arithmetic pixel for pixel.
- If an OpenGL 3.3 context cannot be created — headless host, missing driver,
  framebuffer failure — Bucharest Lite quietly keeps the CPU backend. A
  mid-frame GPU failure also re-composites that frame on the CPU, so the
  preview never shows a partial image.
- Export always composites on the CPU on its worker thread.

## Transitions

A transition is attached to the **outgoing** clip of an adjacent pair and
blends it toward the **next adjacent clip** on the same track. The transition
spec holds a kind, a duration, an alignment and free-form params; the model
(`Track::addTransition`) only accepts a duration up to the shorter of the two
clips, so a transition can never outrun its neighbours. The same compositor
draws preview and export, so what you see is what you render.

**Kinds**

- **Crossfade** — a linear per-channel blend between the outgoing (A) and
  incoming (B) frames; at the midpoint it is an exact 50/50 mix.
- **Dissolve** — the standard alpha crossfade. It behaves like a crossfade
  (deterministic, no dithering).
- **Fade through black** — there is no separate "Fade" kind in the model, so a
  fade is a crossfade flagged through-black: set `throughBlack` to `true` (or
  set `fade` to the string `"through_black"`) and the outgoing frame dims to
  black over the first half while the incoming frame lifts from black over the
  second. At the exact midpoint the frame is black.
- **Wipe** — the incoming frame is revealed from an edge. `direction` is
  `"left"` (default), `"right"`, `"up"` or `"down"`; an integer `softness`
  (pixels, default 0, clamped to the smaller frame dimension) draws a linear
  ramp straddling the moving edge, so a soft edge fades in gradually.
- **Slide** — the incoming frame slides in from `direction` (default
  `"left"`) while the outgoing frame slides out by the same amount; pixels that
  sample outside their source frame render black.

**Alignment** controls *where* in time the transition sits relative to the cut
between A and B:

- **Left** — entirely on the outgoing tail: `[aEnd - duration, aEnd)`. The
  incoming clip is not yet on the timeline, so it is held at its first frame.
- **Center** (default) — straddles the cut: `[aEnd - duration/2, aEnd +
  duration/2)`, splitting the transition evenly either side of the edit.
- **Right** — entirely on the incoming head: `[aEnd, aEnd + duration)`. The
  outgoing clip is held at its last frame.

The half-duration is rounded to whole ticks, so an even frame count stays
exact. Outside the active span the track renders normally (the outgoing clip
before it, the incoming clip after it). Params are free-form JSON carried on
the model; an unrecognised `direction` or `softness` (or an unknown kind) falls
back to the documented defaults rather than failing.

## Scopes

The **Scopes** dock (toggle with `Ctrl+6`) measures the *composited* preview
frame, so it reflects every track, effect and LUT exactly as it will export.
Use the drop-down at the top of the dock to switch views:

- **Histogram** — 256-bin luma plus per-channel R, G and B curves (luma in
  white, R/G/B tinted). Read it for clipping (mass piled at the far left or
  right) and for the overall balance of a grade.
- **Waveform** — a luma waveform monitor: the x-axis follows the image's
  horizontal position and the y-axis is luma (black at the bottom, white at the
  top), with brightness showing how much of each column sits at that level.
  Flat, even traces mean a well-exposed shot; a trace bunched at the bottom or
  top means under/over-exposure.
- **Vectorscope** — chroma only: the centre is neutral gray (`Cb = Cr = 128`)
  and distance from the centre is saturation, with the graticule ring drawn for
  reference. A grey-only scene plots as a single dot at the centre; an
  over-saturated cast pushes the trace toward an edge.

Luma and chroma use Rec.709 weights. Big frames are measured on a subsampled
grid so the dock stays responsive during playback; an empty timeline clears the
views. Scopes are a *measurement* only — they never change your media.

## Quality measurement (PSNR / SSIM)

**Tools → Compare Quality…** compares two media files (typically an original
and an encoded/exported copy) and reports objective quality metrics:

- **PSNR** (peak signal-to-noise ratio, dB) — per-frame and aggregate
  (average/min/max). Higher is better; identical frames are reported at a
  capped value rather than infinity.
- **SSIM** (structural similarity, 0–1) — windowed 8×8 over Rec.709 luma with
  the standard C1/C2 constants, averaged across frames. 1.0 means identical.

Both files are decoded through the same stack the preview and thumbnails use
and normalized to a common size/pixel format before comparison. Frame-count or
dimension mismatches are reported explicitly rather than silently truncated,
and an optional frame limit keeps very long comparisons quick.

SSIM here is a standard approximation (uniform 8×8 windows), intended as a
relative guide rather than a reference implementation.

## Full-screen preview on a second monitor

To watch the timeline full-screen — on a second monitor while you edit on the
first, or just larger on a single display — use **View → Full Screen Preview**
(`Ctrl+Shift+P`, remappable in the Keyboard Shortcuts dialog).

- With **more than one monitor connected**, a small picker lists the screens by
  name; the first non-primary display is preselected. With a single monitor the
  preview simply fills it — no prompt.
- The preview dock is *moved*, not duplicated: the same panel (and the same
  play/pause, step and timecode controls) reopens as a frameless full-screen
  window, so playback, compositing and the Scopes tap keep running exactly as
  before. Your layout is untouched.
- Press **Escape** to return, or close the window (Alt+F4) — the preview docks
  back where it was. The menu entry can also be toggled off.

While the preview is detached the Preview dock is empty; this is expected. If
the window is still open when you quit, it is restored and torn down with the
main window.

## Audio

- **Audible preview playback.** Pressing play is audible: on Linux the preview
  opens an ALSA device at 48 kHz (`BL_AUDIO_DEVICE` selects the PCM device;
  unset or `default` uses the system default, and a missing device falls back to
  a silent null sink), on Windows it uses WinMM waveOut. Clips are resampled to
  48 kHz, mono and >2-channel sources are mapped to stereo, and clip/track gain,
  pan, mute/solo, speed and reverse playback are applied by the feeder. Seeking
  flushes queued audio so playback does not lag the playhead.
- **Loop playback.** Toggle **Playback → Loop Playback** (`Ctrl+Shift+Space`,
  also in the Edit/Timeline menus and toolbar) to loop playback over the
  current timeline zone. When no zone is set, playback loops over the full
  sequence. The loop range updates when the zone changes, and the loop state is
  remembered between sessions.
- **Waveform display.** Every audio clip draws its waveform inside the clip
  body on the timeline, so you can read the shape of a take and cut on
  transients at a glance. The shape is reduced from the same background
  peak/RMS analysis that feeds the mixer meters, and appears as soon as that
  analysis settles; nothing to configure. Zoom in for more detail (a zoomed-out
  clip shows its loudest transients, a zoomed-in one stays smooth). Video clips
  whose media also has an audio stream draw it too; the video-only half of a
  split A/V pair does not.
- **Video filmstrip.** Video clips show a strip of frames sampled across the
  clip's source range, so you can scan the footage and find a cut point without
  opening the source monitor. The strip is decoded in the background and cached;
  frames that are still loading show a neutral placeholder and fill in without
  interrupting playback or an edit. Zoom in to see more cells (up to 12 across a
  clip), or zoom out for a single representative frame. Trimming a clip
  re-samples its new in/out range; moving it keeps the cached frames.
- **Clip labels & effects.** Each clip draws a small chip for every effect on
  its stack, using the effect's display name (truncated to fit, with a `+N`
  summary when the stack is longer). Bypassed effects are dimmed. The clip's
  color label appears as a thin bar across the top, and the clip name has a
  translucent backing so it stays legible over filmstrips and waveforms. Use
  the **Labels** button in the timeline toolbar or
  **View → Show Clip Labels & Effects** to hide these overlays; the choice is
  remembered between sessions. All eight color labels available in the
  Inspector render as distinct colors.
- **Clip markers & comments.** Right-click a clip and choose **Add Clip Marker
  Here** to pin a note to a moment in the clip. The dialog takes a label, a
  multi-line comment and a color. Markers appear as small triangles along the
  clip's bottom edge; right-click one to **Edit Clip Marker…** or **Delete Clip
  Marker**. A marker's position is relative to the clip, so it follows the clip
  when you move or copy it, and it becomes a snap target while editing. Add,
  edit and delete are each a single undo step.
- **Mixer.** The **Mixer** dock has one strip per audio track (gain, pan, mute,
  solo) plus a master bus. Its live L/R meters follow the playhead, so they are
  snapshots of the mix *at that instant* rather than a tap on the device output
  (see *Known UI limitations (v1)*).

## Proxy workflow (v1.9.15)

Bucharest Lite can edit high-resolution media using low-resolution **proxies**.

- **Enable.** The **Use Proxies** toolbar button toggles proxy usage globally
  (persisted between sessions).
- **Auto-generate.** While enabled, adding or importing media whose source is
  at least the configured threshold wide (3840 px default) automatically queues a
  low-res proxy (H.264, half resolution, high quality). Progress is reported in
  the status bar, and the timeline clip shows a ✓ (ready), ⟳ (generating) or ✕
  (failed) proxy badge.
- **Per-clip overrides.** Each clip can force proxy use on/off for its own
  media; the choice is persisted in the project and is a single undo step.
- **Preferences.** Proxy behaviour (directory, auto-proxy minimum width,
  resolution factor, CRF, codec, concurrency) is configured through the
  preferences dialog's proxy group.
- **Export.** Export always relinks to the full-resolution originals; a missing
  original fails the export loudly rather than silently exporting proxy quality.

Proxy generation requires an `ffmpeg` binary on your `PATH`.

## Export Workflow

Bucharest Lite uses a composition-based export pipeline where what you see in the preview is what you export. The export pipeline reuses the same `Compositor::renderFrame()` code path as the interactive preview engine.

### Export Dialog

Open the export dialog via `File → Export...` or press `Ctrl+E`. The dialog collects the output target and format options, then renders the whole composition into the chosen file in the background while you keep using the app.

#### Export Settings Fields

| Field | Description |
|---|---|
| **Target** | `Save to File` (default) or `Stream to IP`. Streaming publishes to a network endpoint instead of writing a file; see *Stream to IP* below. |
| **Output Path** | File path for the exported video. Use the `Browse...` button. Supported extensions: `.mp4`, `.webm`, `.mkv`. Disabled while the target is `Stream to IP`. |
| **Preset** | A one-click encode profile — YouTube 1080p/4K, Vimeo 1080p, WebM (VP9), DVD-sized MPEG-4, Blu-ray-sized H.264, Mobile 720p — or `Custom`. Picking a preset fills the format fields below; it does not change the output path or the range. |
| **Range** | `Entire project` (default), `Timeline zone` (the in/out zone set with `I`/`O`) or `Selected clip` (the span of the current timeline selection). The zone/clip options are enabled only when one exists. |
| **Container** | `MP4 (.mp4)`, `WebM (.webm)` or `Matroska (.mkv)`. MP4 pairs with H.264/MPEG-4; WebM pairs with VP9/AV1/Theora; Matroska accepts every shipped codec. The dialog keeps container and codec in sync. |
| **Video Codec** | Installed codec plugins: H.264 (libx264), VP9, AV1 (SVT-AV1 or libaom), MPEG-4, Theora. |
| **Quality (CRF)** | Constant-rate factor from 0 to 51, default 23. Lower = higher quality and larger file; 0 uses the encoder's default rate control. |
| **Resolution** | `Full`, `Half`, `Quarter`, or `Custom` width/height. Scaling applies relative to the project sequence size. |
| **Include Audio** | When enabled (default), the exported file gets an audio track: every audible clip/track in the timeline is mixed down (same gains, pan, volume keyframes and mute/solo as the mixer meters) and encoded as AAC or FLAC (MP4) or Opus or Vorbis (WebM). When disabled, the output contains video only. |
| **Audio Codec** | AAC or FLAC for `.mp4`; Opus or Vorbis for `.webm`. Defaults to AAC/Opus and follows the selected container. |

### Batch Export

Multiple export jobs can be queued via `File → Batch Export...`. Each job runs with its own output path and format settings; the Render button processes the queue one job at a time and updates each row's status live.

#### Batch Queue Operations

- **Add Job...**: opens the export dialog; the chosen settings become a queued job.
- **Remove**: remove the selected job from the queue (a job currently rendering cannot be removed).
- **Run**: render all pending/failed jobs in order.
- **Cancel** (shown while rendering): stops the current job; the remaining queue is kept for later.
- **Persist**: the queue is saved to disk on every change (`export_queue.json` in the app data directory), so interrupted jobs show up again the next time you open Batch Export.

A job that fails stays in the queue marked `Failed`; fix the underlying problem and press Run again to retry only the unfinished jobs. Closing the dialog does not stop a queued run until the current job's output file is closed.

### Export Progress

During export, a progress window shows:
- Current frame being rendered/encoded
- Estimated time remaining
- Speed (frames/second)
- Cancel button to abort the current export

### Stream to IP

Instead of writing a file, the export can be published to a network endpoint.
Pick **Target → Stream to IP** in the export dialog and enter the destination URL:

- **RTMP** — `rtmp://host[:port]/app/key` (or `rtmps://` for TLS). Use this to
  push to a streaming platform's ingest server or a local RTMP relay such as
  `rtmp://localhost:1935/live/mykey`. The output is muxed as **FLV**.
- **UDP** — `udp://host:port`, e.g. `udp://127.0.0.1:5000` or
  `udp://239.0.0.1:1234` for multicast. The output is muxed as **MPEG-TS**.

While the target is `Stream to IP` the file path and container are irrelevant
(the container is inferred from the URL scheme) and the codecs are fixed to
**H.264 video + AAC audio** — FLV and MPEG-TS both carry that pair and nothing
else this version ships. Include-audio still toggles the AAC track. The Export
button enables as soon as the URL is non-empty, so a valid URL can be sent with
no output file selected.

Two things to know:

- **There is no real-time pacing.** The export renders and sends as fast as the
  machine can encode, so the stream arrives much faster than real time. A
  receiver (e.g. `ffplay`, VLC, or an ingest server) must buffer; playing the
  stream live on a second machine may drop frames until it settles. This is
  intended for reliable delivery of a finished clip, not live capture.
- **SRT is not supported in v1.** Only `rtmp`/`rtmps` and `udp` URLs are
  accepted; anything else fails planning with *"unsupported stream URL (use
  rtmp:// or udp://)"*.

### Troubleshooting

| Issue | Solution |
|---|---|
| Export fails with "missing codec" | Install the required codec plugin or select a different codec |
| Export fails with "unsupported container" | Use `.mp4`, `.webm` or `.mkv`; other containers are not built in this version |
| Stream export fails with "unsupported stream URL" | Use an `rtmp://`, `rtmps://` or `udp://` URL; SRT is not supported in v1 |
| Green frames or color shift | Ensure project color space matches the export target (BT.709 vs BT.2020) |
| Very large output files | Lower the quality (increase CRF) or select a lower resolution |

### Export Limitations (v1)

- The **Range** combo exports the whole timeline, the timeline zone, or the
  selected clip's span. Arbitrary multi-clip ranges are not available.
- Timeline audio is mixed down to a single stereo track (2 channels max, decoded from each clip's available channels and downmixed/resampled as needed). Audio applies to the entire timeline only — per-clip audio effects do not run during export
- Export containers are limited to MP4 and WebM. Matroska (`.mkv`) **import** works — MKV files open, probe, preview and thumbnail correctly, and their codec private data is preserved — and MKV **export** is now available in the UI (since v1.1.1).
- Nested compositions are not supported — flat sequences only
- Multi-cam source clips are not supported for export
- Hardware acceleration (NVDEC/VAAPI/VideoToolbox) is not available in v1

### Known UI limitations (v1)

- Live meters reflect the playhead position, so they are snapshots of the mix
  at that instant rather than a tap on the device output (further per-clip or
  time-ranged metering is deferred).
- GPU compositing is opt-in and blends layers only; decoding, effects and masks
  stay on the CPU. There is no preferences UI for it — set `BL_COMPOSITOR`
  before launching.
- Masks are polygonal (optionally smoothed). Bezier handle editing and mask
  animation/keyframing are not implemented yet.