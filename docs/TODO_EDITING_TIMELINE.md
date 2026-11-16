# TODO — Editing & Timeline Features Roadmap

Phased plan for the next editing and timeline work after v1.10.0. Builds on the
shipped Kdenlive-parity timeline (phases 1–8), Source Monitor / 3-point edits,
masks, and the pluggable compositor. Each phase ends with targeted tests, a
green `ctest --preset linux-release` (and Windows where applicable), doc
updates, and a commit.

**Status:** Phase 1 complete; Phase 2 not started.

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
- [ ] **Docs/tests:** update `docs/user-guide.md`, `CHANGELOG.md`,
      `PROJECT_STATUS.md`, `KDENLINE_TIMELINE.MD` (§10 status), shortcuts
  - [x] loop playback documented (user guide, changelog, timeline parity)

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
- [ ] **Slide trim**
  - [ ] Slide edit interaction (move clip content while preserving in/out and
        adjacent clip boundaries) with controller primitive and preview
  - [ ] Integrate with edit mode/cursor feedback and snapping
  - [ ] Tests: slide bounds, neighbors unchanged, undo, locked refusals
- [ ] **Expand clip / fill gaps**
  - [ ] "Expand Clip" (extend to fill adjacent gap up to source/media limits
        or to neighbors as appropriate) — define exact semantics
  - [ ] Tests: left/right expansion, no-overlap, media-limit clamping
- [ ] **Active / target track**
  - [ ] Explicit active (and optional target) track concept in UI state
  - [ ] Route insert/overwrite, paste, and edit actions to active/target tracks
        predictably; reflect in headers
  - [ ] Persist/restore state; tests for routing and refusals
- [ ] **Insert/overwrite zone from source**
  - [ ] "Insert/Overwrite clip/zone in timeline" actions leveraging Source
        Monitor marks and timeline zone/playhead (complement existing 3-point
        insert/overwrite)
  - [ ] Tests: marked/unmarked cases, linked A/V, undo grouping
- [ ] **Spacer & ripple interactions audit**
  - [ ] Verify spacer/remove-space/ripple tools cover common gap workflows;
        add small refinements uncovered by tests
- [ ] **Docs/tests:** user guide (edit tools), shortcuts, changelog

**Exit criteria:** razor/slide/expand + active-target track workflows available
and covered by controller/UI tests.

---

## Phase 3 — Visualization, markers & track affordances

**Goal:** improve readability and navigation of complex timelines.

- [ ] **Video thumbnails on timeline clips**
  - [ ] Generate/cache lightweight frame thumbnails for video clips (async,
        bounded cache, invalidated on trim/move/source changes)
  - [ ] Paint thumbnails in `ClipItem` respecting zoom/clip width; graceful
        fallback while loading
  - [ ] Tests: cache behavior, no UI-thread stalls, rebuild correctness
- [ ] **Clip labels & effect indicators**
  - [ ] Draw applied effect names/indicators on clips (truncated/summarized)
  - [ ] Optional clip color labels/tags if not already present; ensure
        readability over thumbnails/waveforms
  - [ ] Tests: rendering/state mapping (model → view), toggles if added
- [ ] **Clip-level markers & comments**
  - [ ] Add/edit/delete markers attached to clips (separate from sequence
        markers), with labels/comments and snap behavior
  - [ ] Serialize in project format (backward-compatible) and include in copy/
        paste semantics where sensible
  - [ ] Tests: model round-trip, undo, snap, clipboard
- [ ] **Sequence marker enhancements**
  - [ ] Marker comment text display/toggle on ruler; navigation (next/prev)
  - [ ] Tests: display state, navigation, persistence as designed
- [ ] **Timeline navigation aids**
  - [ ] Vertical zoombar / improved vertical navigation for tall timelines
  - [ ] Timecode-vs-frames position indicator toggle (consistent with rulers)
  - [ ] Tests: viewport/scroll correctness, formatting toggle
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
