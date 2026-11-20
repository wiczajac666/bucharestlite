# TODO — Editing & Timeline Features Roadmap

Phased plan for the next editing and timeline work after v1.10.0. Builds on the
shipped Kdenlive-parity timeline (phases 1–8), Source Monitor / 3-point edits,
masks, and the pluggable compositor. Each phase ends with targeted tests, a
green `ctest --preset linux-release` (and Windows where applicable), doc
updates, and a commit.

**Status:** Phase 1 complete; Phase 2 complete.

### Phase 1 summary (complete)

- `eaeec65` — Loop playback / loop zone (`playback.loop`, `Ctrl+Shift+Space`), zone-aware transport looping with persistence.
- `40429fc` — Remove space at playhead, remove all spaces after playhead (track/sequence scope), with menu/context actions and tests.
- `1590c6c` — Ripple trim in/out to playhead actions.
- `7073030` — Centered playhead during playback (toggle, persisted).
- `27a09d8` — Numeric clip duration dialog and multi-selection resize (macro undo, partial refusals).
- `b00eb31` — Track height resize/collapse improvements (single-step drag commit, collapse/expand all) and per-track lane geometry.
- (closing commit) — Phase 1 tests/docs wrap-up.
**Scope:** timeline interaction, edit tools, clip/track workflow, and
timeline-adjacent editing polish.  
**Out of scope for this roadmap:** codec/export pipeline changes, GPU effect
offload, packaging-only work (handled in release cycles).

---

## Guiding principles

- **Qt-free core, undoable UI mutations.** Model/controller changes stay in
  `bl_timeline`/controllers; every user-visible mutation is one undo step via
  the existing "validate → single `FunctionCommand`" pattern and reports
  refusals through the status bar (never silent).
- **Locked-track safety.** Edits refuse or skip locked tracks consistently with
  current behavior.
- **Shared preview/export semantics.** Timeline edits must not introduce
  preview/export divergence (compositor/render paths stay the source of truth).
- **Incremental delivery.** Land phases independently with tests and docs;
  avoid bundling unrelated refactors.

## References

- `docs/TODO_V1_10.md` — v1.10 feature push (complete)
- `KDENLINE_TIMELINE.MD` — parity plan and §10 post-release gap audit (1.9.14)
- `docs/user-guide.md` — known UI limitations
- `FEATURES.md`, `README.md`, `docs/architecture/ARCHITECTURE.md`

---

## Phase 1 — Timeline interaction & quick editing wins

**Goal:** close the highest-value, low-risk interaction gaps called out in the
parity audit.

- [x] **Loop playback / loop zone**
  - [x] Toggle loop playback over the current timeline zone (and fall back to
        full sequence when no zone is set)
  - [x] Transport/UI wiring (play loops, stop/disable loop), shortcut action
  - [x] Status/toolbar affordance showing loop state (checkable menu/toolbar action)
  - [x] Tests: loop wraps at zone/sequence bounds, respects zone edits, single
        undo step not required (playback state) but state persists as designed
- [x] **Per-track height resize and collapse**
  - [x] Drag-to-resize lane height in track headers (honor `Track::height`,
        clamp min/max, per-track; single undo step on commit)
  - [x] Collapse/expand track and collapse/expand all convenience
  - [x] Update `timeline_geometry`/layout (`contentHeight`, hit-testing,
        scroll) and keep header/view scroll in sync (per-track lane geometry)
  - [x] Persist heights/collapsed state in project track data (`height`/custom)
  - [x] Tests: controller height/collapse undo/clamp/restore behavior;
        variable-height layout exercised via per-track lane geometry and
        rebuild paths (offscreen)
- [x] **Remove space / remove all spaces**
  - [x] `Remove Space` at playhead/gap selection on a track (ripple-close gap)
  - [x] `Remove All Spaces After Cursor` / sequence-wide gap cleanup variant
        (scoped, undoable)
  - [x] Menu/shortcut/context-menu entries with clear refusal messages
  - [x] Tests: single gap, multiple gaps, locked tracks, empty/no-op cases
- [x] **Ripple trim to playhead**
  - [x] Actions to ripple-trim selected/hovered edge to playhead (left/right
        equivalents, registered in `ActionRegistry`; defaults unmapped)
  - [x] Reuse/extend controller primitives; refuse invalid trims cleanly
  - [x] Uses existing trim/ripple controller behavior (validated, one undo step)
- [x] **Numeric duration dialog & multi-selection resize**
  - [x] Dialog to set clip duration numerically (HH:MM:SS:FF or frame count)
        with timecode awareness at sequence fps
  - [x] Apply to all selected clips where valid; report partial refusals (one
        undo step for multi-selection)
  - [x] Tests: single/multi selection, invalid/overlap refusals, undo (`TimelinePanel`)
- [x] **Playback/view polish**
  - [x] Optional fixed/centered playhead during playback (toggle, persisted)
  - [x] Zoom-to-fit, lane order, and scroll behavior use per-track lane
        geometry (`contentHeight`/`laneTop`/`laneHeight`) with variable heights
- [x] **Docs/tests:** update `docs/user-guide.md`, `CHANGELOG.md`,
      `PROJECT_STATUS.md`, `KDENLINE_TIMELINE.MD` (§10 status), shortcuts
  - [x] loop playback documented (user guide, changelog, timeline parity)
  - [x] Phase 1 tools/feedback documented in the user guide, changelog and
        project/parity status during the Phase 2 wrap-up

**Exit criteria:** loop + track height/collapse + gap removal + ripple-to-playhead
+ duration dialog usable from UI; suites green.

---

## Phase 2 — Edit tools & timeline targeting

**Goal:** round out the edit-tool set and make multi-track targeting explicit.

- [x] **Razor tool mode**
  - [x] `Razor` in edit-mode picker/cycle/action (`timeline.razor`, `R`) with
        crosshair cursor and click-to-split at cursor time/track (distinct
        from split-at-playhead); refusals reported
  - [ ] Optional: split across selected/linked tracks per modifier (define and
        document behavior)
  - [x] Split placement/locked-track refusal covered by existing
        `splitClip` controller semantics/tests; mode available in UI state/persistence
- [x] **Slide trim**
  - [x] `slideClip` controller primitive slides a clip between its two adjacent
        neighbors, trimming their facing edges by the same delta while keeping
        the clip's in/out and total span; bounded by neighbor spans and source
        limits, one undo step
  - [x] `Slide` edit mode with horizontal cursor, three-clip preview, snapping
        of the target start, and action `timeline.slide` (`Y`); mode persisted
  - [x] Controller tests (left/right slide, undo, bounds, source limits, locked
        refusals) and an offscreen drag test
- [x] **Expand clip / fill gaps**
  - [x] "Expand Clip to Fill Gaps" extends selected clips into adjacent gaps on
        their track (left to previous clip/0, right to next clip), bounded by
        source/media limits and non-overlap; one undo step
  - [x] Basic offscreen panel test for gap fill; refusals reported when no
        expansion is possible
- [x] **Active / target track**
  - [x] Active track state reflected in headers and used by paste/gap/ripple
        helpers; persisted/restored (`timeline/activeTrack`)
  - [x] Route insert/overwrite-from-source and add-to-timeline to the active
        track when compatible (fallback to first compatible lane); paste honors
        active-track hint
  - [x] Routing/refusal behavior covered by existing and new offscreen tests
  - [ ] Optional: separate explicit target tracks (deferred)
- [x] **Insert/overwrite zone from source**
  - [x] `source.insert_at_zone` / `source.overwrite_at_zone` place the Source
        Monitor marked range at the timeline zone-in (refuses if no zone),
        reusing 3-point insert/overwrite semantics (linked A/V, one undo step)
  - [x] Offscreen panel tests for placement, overwrite, and refusal without zone
- [x] **Spacer & ripple interactions audit**
  - [x] Verified spacer drag tool (`addSpacer`/`removeSpacer`), remove-space
        actions (at playhead / after cursor / sequence scope), and ripple
        delete/trim workflows cover common gap cases; existing controller/UI
        tests cover core behaviors (no further refinements required now)
- [x] **Docs/tests:** user guide (edit tools), shortcuts, changelog updated for
      the Phase 2 tool set; controller + offscreen UI tests cover bounds, undo,
      snapping and refusals.

**Exit criteria met:** razor/slide/expand + active-target track workflows are
available and covered by controller/UI tests.

**Phase 2 complete.** Optional follow-ups (not required for the exit criteria):
razor split across selected/linked tracks per modifier, and a separate explicit
target-track concept distinct from the active track.

---

## Phase 3 — Visualization, markers & track affordances

**Goal:** improve readability and navigation of complex timelines.

- [x] **Video thumbnails on timeline clips** — sampled filmstrips, not just a
      first frame. `readThumbnailAt()` seeks and decodes the frame covering a
      source time; `TimelineClipThumbnailEngine` is a Qt-free, JobManager-backed
      async cache keyed by (media id, source time, width) with a bounded LRU and
      `remove()`/`retain()` invalidation. Trimming changes the sampled source
      times (new keys), moving reuses them, and source re-links are invalidated
      by path tracking in `MainWindow`.
  - [x] Generate/cache lightweight frame thumbnails for video clips (async,
        bounded cache, invalidated on trim/move/source changes)
  - [x] Paint thumbnails in `ClipItem` respecting zoom/clip width; graceful
        fallback while loading. Cell count tracks clip width (1–12), each cell
        letterboxes its frame, and undecoded cells draw a neutral placeholder.
        Also fixed the BGRA→`QImage` mapping (was swapping red and blue).
  - [x] Tests: cache behavior (hit/idempotent, LRU eviction, prune), no
        UI-thread stalls (enqueue off-thread, non-blocking, callback never on
        caller), rebuild correctness (time-window sampling, late frames via
        refresh, zoom cell counts, pixel-level BGRA color). Full suite green.
- [x] **Clip labels & effect indicators** — clips draw up to three truncated
      effect-name chips at the top-left (resolved via `EffectRegistry`'s human
      display names, raw ids for unregistered effects, bypassed effects dimmed)
      plus a `+N` summary when the stack does not fit. The clip's color label is
      reinforced with a thin top bar so the tag stays visible over a filmstrip,
      and the name gets a translucent backing for legibility over waveforms.
      The palette now covers all eight labels offered by the Inspector (was six).
  - [x] Draw applied effect names/indicators on clips (truncated/summarized)
  - [x] Optional clip color labels/tags if not already present; ensure
        readability over thumbnails/waveforms
  - [x] Tests: rendering/state mapping (model → view), toggles if added —
        Qt-free `clipEffectDisplays()` unit tests, offscreen attach/paint/toggle
        tests, eight-label distinctness, and MainWindow persistence. The
        `Labels` toolbar button / `View → Show Clip Labels & Effects` action
        repaint items without rebuilding (so drags are not cancelled) and are
        persisted as `timeline/showClipLabels`.
- [x] **Clip-level markers & comments** — clips carry their own markers with a
      label, free-form comment and color, stored as a `Duration` offset from the
      clip start so they travel with the clip. Right-click a clip to add one at
      the cursor, or right-click an existing marker triangle to edit/delete it.
      The editor offers label, multi-line comment and the eight-color palette.
      Marker offsets are absolute snap targets in `snapToNearest()`, alongside
      clip edges and sequence markers.
  - [x] Add/edit/delete markers attached to clips (separate from sequence
        markers), with labels/comments and snap behavior — one undo step each
  - [x] Serialize in project format (backward-compatible: old clips have no
        `markers` key, old markers have no `comment`/`color`) and include in
        copy/paste — the clipboard deep-copies the whole `Clip`, so markers
        ride along with no rebasing
  - [x] Tests: model round-trip, undo, snap, clipboard — Qt-free model/JSON
        tests, controller add/edit/remove/undo + snap + paste tests, offscreen
        panel attach/paint/hit-test tests and a dialog test. Full suite green.
- [x] **Sequence marker enhancements** — sequence markers now carry an optional
      free-form `comment` (backward-compatible JSON: old markers load with an
      empty comment, and comment-only edits are undoable). `MarkerItem` draws the
      label and comment in a chip just below the ruler triangle; the `Comments`
      toolbar button / `View → Show Marker Comments` toggle switches the text on
      and off with a repaint only (no scene rebuild) and persists as
      `timeline/showMarkerComments`. `Timeline → Next Marker` (`]`) and
      `Previous Marker` (`[`) move the playhead to the nearest marker on that
      side and scroll it into view, scanning the model rather than assuming the
      markers are sorted.
  - [x] Marker comment text display/toggle on ruler; navigation (next/prev)
  - [x] Tests: display state, navigation, persistence as designed — Qt-free
        marker comment/equality/JSON tests, a controller comment-only undo test,
        offscreen panel toggle/pixel/navigation/hit-test tests, and MainWindow
        action + QSettings round-trip tests. Full suite green.
- [x] **Timeline navigation aids** — a vertical zoombar (logarithmic 0.5×–4×
      slider beside the view) scales every lane's rendered height without
      touching the model or undo stack; `timeline_geometry` helpers take an
      optional `verticalScale` (default 1, so existing layout/callers are
      unchanged), the ruler keeps its fixed height, and the header/scene/scroll
      range track the scaled geometry. A `Frames` toolbar button / `View → Show
      Positions in Frames` action toggles the playhead readout and ruler labels
      between `HH:MM:SS:FF` and plain frame counts via a shared
      `formatTimelinePosition()` helper and a `RulerItem::showFrames` flag.
      `TimelineState` persists `verticalZoom` (`timeline/verticalZoom`) and
      `showFrames` (`timeline/showFrames`).
  - [x] Vertical zoombar / improved vertical navigation for tall timelines
  - [x] Timecode-vs-frames position indicator toggle (consistent with rulers)
  - [x] Tests: viewport/scroll correctness, formatting toggle — offscreen panel
        tests for the position label + ruler pixel formatting, vertical-zoom
        lane geometry/scene-rect/scroll range and header alignment, plus a
        MainWindow frames-toggle persistence round-trip. Full suite green.
- [ ] **Track compositing/effects toggles (evaluate)**
  - [ ] Investigate per-track compositing enable/disable and "disable all
        timeline effects" preview toggle; implement if low-risk, else document
        deferral
- [ ] **Docs/tests:** performance notes for thumbnails, user guide updates

**Exit criteria:** thumbnails/markers/labels materially improve timeline
scanability without regressions in playback responsiveness.

---

## Phase 4 — Source, speed & editing workflow polish

**Goal:** tighten source-to-timeline workflows and speed-editing UX.

- [ ] **3-point/bin integration polish**
  - [ ] Bin-level/source in-out affordances aligned with Source Monitor state
        (where missing), clearer feedback for mark ranges and invalid states
  - [ ] Ensure insert/overwrite, zone, and target-track behaviors are
        consistent and documented
  - [ ] Tests: bin↔monitor mark flow, edit placement, undo
- [ ] **Speed editing UX**
  - [ ] Ctrl+drag (or equivalent, configurable) on clip edge/body to change
        speed with visual feedback (respect existing speed/reverse model)
  - [ ] Keep audio/video mapping and timeline duration consistent; undoable
  - [ ] Tests: speed change math, linked A/V behavior, limits
- [ ] **Audio pitch compensation (evaluate/plan)**
  - [ ] Assess feasibility of pitch-preserving speed changes in
        preview/mixdown; spike + document (implement only if tractable within
        current audio pipeline), otherwise defer explicitly
- [ ] **Selection & clipboard refinements**
  - [ ] Multi-track selection behaviors, paste placement relative to
        active/target track and playhead, small parity fixes found in use
  - [ ] Tests: cross-track paste, grouped selections, locked-track handling
- [ ] **Docs/tests:** 3-point workflow, speed controls, known limitations

**Exit criteria:** faster, more predictable source/speed editing; any deferred
items recorded with rationale.

---

## Phase 5 — Larger timeline capabilities (staged / evaluate)

**Goal:** tackle high-effort features behind clear design/feasibility gates.
Items here may span releases; each sub-item requires a short design note before
implementation.

- [ ] **Timeline preview render (pre-render/cache)**
  - [ ] Design: cache keying (effects/masks/speed), storage, invalidation,
        zone render controls, and interaction with proxies/GPU backend
  - [ ] Implement minimal pre-render for zone/sequence with progress/cancel and
        safe fallback to live render
  - [ ] Tests: invalidation correctness, cache reuse, playback parity
- [ ] **Nested sequences / containers** *(design gate)*
  - [ ] Model representation, rendering recursion limits, and edit semantics;
        prototype or defer with documented decision
- [ ] **Multi-cam workflow** *(design gate)*
  - [ ] Sync/group model, angle switching UI, and flatten/render behavior;
        prototype or defer with documented decision
- [ ] **Docs:** architecture notes, user guide, explicit shipped-vs-deferred
      status per sub-item

**Exit criteria:** preview render delivers measurable playback benefit on target
projects; nested/multicam either land as scoped MVPs or are formally deferred
with recorded designs.

---

## Cross-cutting checklist (every phase)

- [ ] Controller/model validation + one undo step per logical edit (macros only
      where multiple ops must be atomic)
- [ ] Offscreen UI tests for panel/view behavior; unit tests for controller/model
- [ ] No new `-Wsign-compare`/warnings (build stays `-Werror` clean)
- [ ] `CHANGELOG.md`, `docs/user-guide.md`, `PROJECT_STATUS.md`, and relevant
      architecture/feature docs updated
- [ ] `ctest --preset linux-release -j4` green before commit

## Tracking

- Mark items `[x]` as they land, note commit hashes per phase (like
  `TODO_V1_10.md`), and append a short "Phase N done" summary when a phase
  closes.
- Revisit priorities after Phase 1 based on user feedback and performance data.
