# Bucharest Lite — Kdenlive Timeline Parity Plan

**Status:** Phases 1–8 are implemented (model/controller foundations, interactive
track headers, lane order + zoom-to-fit, toolbar + operation modes, timeline zone,
A/V linking, context menus/clipboard/marker UI, and shortcuts/docs/tests).
**Scope:** Make the timeline behave like Kdenlive's timeline (UI phases built on the
already-shipped P1/TL-9 model + controller foundations).
**Lane layout decision:** keep `Split` (video band on top, audio band below) as the
default; add a user-selectable `Mixed` / `MixedReverse` toggle.
**Related docs:** `FEATURES.md`, `docs/architecture/ARCHITECTURE.md` (§5.2, §8.2, §8.6,
§19 #12), `PROJECT_STATUS.md`, `CHANGELOG.md`, `docs/user-guide.md`.

---

## 1. Goal & non-goals

### Goal
Deliver a Kdenlive-style timeline interaction surface on top of the existing domain
model and `TimelineEditController`. The model already implements nearly all required
semantics (stream flags, track lock/hide/mute/solo, groups, markers, track structure,
slip/roll, zones); the missing work is almost entirely **UI + interaction wiring**.

### Non-goals (unchanged from project scope)
- Proxy editing, nesting, multi-cam, motion tracking, stabilization.
- GPU rendering.
- Any change to the codec/plugin/export contracts.

---

## 2. Current state (what already exists)

| Area | Shipped | Location |
|---|---|---|
| Stream flags `hasVideo/hasAudio/enabled` | ✅ model + consumers gated | `clip.hpp`, `track.hpp` |
| Track `muted/soloed/locked/hidden/gain/pan/height` (all serialized) | ✅ model | `track.hpp`, `track.cpp` |
| Track structure insert/remove/move (last-video-track refused) | ✅ model + controller | `sequence.hpp`, `timeline_edit_controller.cpp` |
| Clip groups + `splitAudioVideo` | ✅ model + controller | `sequence.hpp`, `timeline_edit_controller.cpp` |
| Markers (add/edit/remove) | ✅ controller | `timeline_edit_controller.cpp` |
| `slipClip`, `rollTrim`, `liftZone`/`extractZone` | ✅ controller | `timeline_edit_controller.cpp` |
| Lock gating ("Track is locked") | ✅ controller | `ensureEditable` |
| `LaneOrder { Split, Mixed, MixedReverse }` helpers | ✅ geometry | `timeline_items.cpp` |
| Timeline view: ruler/playhead/lanes/clips, trim, move, marquee, ripple delete, split, snap, zoom | ✅ UI | `timeline_panel.cpp` |
| TrackHeader | ⚠ paint-only (no controls) | `timeline_panel.cpp` |
| Mixer track mutators (gain/pan/mute/solo/lock) | ✅ controller | `mixer_controller.*` |

**Gaps:** interactive track headers, lane-order switching, timeline toolbar/edit modes,
roll/slip/ripple/spacer interactions, zone in/out UI, A/V linked selection, context menus,
clipboard, marker UI, persistence of timeline view state, and remappable shortcuts.

---

## 3. Architecture guardrails

- `bl_timeline` stays Qt-free; no GUI concerns leak into the domain.
- Every model mutation goes through an undoable controller command
  (`TimelineEditController` for layout, `MixerController` for track properties).
  New mutators follow the existing "validate → push one `FunctionCommand`" pattern and
  set `lastError_` on refusal.
- Refusals surface in the status bar (never silent), matching existing behavior.
- `TimelinePanel` continues to read the model via `ProjectController` and rebuild on
  `projectChanged`/`undoChanged`.

---

## 4. Phased plan

### Phase 2 — Interactive track headers (Kdenlive track control panel)

**Deliverable:** a real left-hand track header column with per-track controls.

- Replace the paint-only `TrackHeader` with a `TrackHeader` container holding a
  `TrackHeaderRow` widget per lane (kept in vertical scroll sync with the view).
- Per-row controls, gated by track kind:
  - **Video:** name, lock, hide (`hidden`), delete, insert above/below, reorder drag.
  - **Audio:** name, lock, mute, solo, delete, insert above/below, reorder drag.
- Active-track highlight; click selects the "current" track (used by later phases).
- Name editing (double-click → `QLineEdit`), undoable.
- Add-track (+ video / + audio) and delete-track buttons at the column footer.
- Right-click row menu (rename, insert above/below, delete, lock, hide/mute, solo).
- Track height resize (drag the row's bottom edge) → `Track::height`.

> **Status — implemented except track-height resize.** The interactive header
> column, toggles, active track, inline rename, add/insert/remove/move (menu +
> drag-reorder within a kind) and the `+V`/`+A` toolbar buttons are shipped.
> *Deferred:* drag-to-resize a lane's height. `timeline_geometry` still derives
> lane heights from the per-kind constants and ignores `Track::height`; making
> it honor a per-track height needs a model-default change (`height_` is `60`
> for both kinds, serialized) plus the geometry/`contentHeight`/`laneTop`
> accumulation and the lane-order tests updated in lockstep. Tracked as a
> follow-up rather than folded into this increment.

**Wiring / new code:**
- `MixerController`: add `setTrackHidden(int flat, bool)` (mirror existing mutators);
  reuse `setTrackMuted/Soloed/Locked`.
- `TimelineEditController::insertTrack/removeTrack/moveTrack` already exist — surface them.
- Track name edit: add `setTrackName` (or reuse `MixerController` extension).
- `TimelinePanel` exposes `activeTrack()`/`setActiveTrack()` and emits it.

**Files:** `src/ui/panels/timeline_panel.{hpp,cpp}`, `timeline_items.{hpp,cpp}`,
`mixer_controller.{hpp,cpp}`, `src/ui/app/main_window.cpp` (if actions needed).

**Tests:** toggle lock/mute/hide/solo updates model + is undoable; delete refuses the last
video track; reorder/insert/delete; header row stays aligned after scroll and zoom.

---

### Phase 3 — Lane order + zoom-to-fit

**Deliverable:** Kdenlive lane-order switching, default `Split`.

- Add `LaneOrder order_` to `TimelinePanel`; thread it through `laneRect`, `flatAtY`,
  `rebuildFromModel`, `contentHeight`, and `TrackHeader` paint/rows.
- Toolbar combo: **Normal (Split) / Mixed / Mixed-reverse**.
- Persist the choice in QSettings (`timeline` group), restored on startup.
- Add a "Fit" zoom action (fit content width to the viewport) and a zoom slider.

**Files:** `timeline_panel.{hpp,cpp}`, `timeline_items.{hpp,cpp}` (geometry already
supports order), `main_window.cpp` for the action/shortcut.

**Tests:** `laneForFlat`/`flatForLane`/`laneTop` round-trip for each order; `flatAtY`
inverse under Mixed; switching order rebuilds without changing the model; fit zoom clamps.

> **Status — implemented.** `TimelinePanel` carries `laneOrder_` (default `Split`)
> exposed via `laneOrder()/setLaneOrder()` + `laneOrderChanged`; `laneRect`,
> `flatAtY`, `TrackHeader::layoutRows` and `rebuildFromModel` are all order-aware.
> The top bar gained a **Normal / Mixed / Mixed reverse** combo, a logarithmic
> `zoomSlider` beside the +/- buttons, and a **Fit** button
> (`fitZoomToViewport`, clamped to `[0.25, 64]`); the View menu also carries a
> **Fit Timeline to View** action (`timeline.fit`, `Ctrl+Shift+F`) bound to the
> same slot. `timeline_geometry` now exports
> `kContentMargin` (shared with `contentWidth`) so Fit can solve for the viewport
> width without duplicating the 56px margin. `MainWindow` persists the choice in
> QSettings under `timeline/laneOrder` — written on every change (crash-safe) and
> restored in `maybeRestoreLayout`. `contentHeight` stays order-independent
> (heights are per-kind), so it needed no change. Covered by
> `TimelineLaneOrderPanel.*`, `TimelineZoomControls.*` (5) and
> `MainWindow.laneOrderPersistsAcrossWindows` (1).

---

### Phase 4 — Timeline toolbar & operation modes

**Deliverable:** a Kdenlive-style toolbar and an edit-mode state machine.

- Introduce a small `TimelineState` value struct (edit mode, insert/overwrite, ripple,
  snapping, lane order) owned by `TimelinePanel`, persisted to QSettings.
- **Edit modes:** `Select`, `Range`, `Ripple`, `Roll`, `Slip`, `Spacer`.
  - `Select` — existing move/trim.
  - `Range` — drag on the ruler sets zone in/out (feeds Phase 5).
  - `Ripple` — move/trim ripples following clips (replaces Shift-only convenience but
    keep Shift as an accelerator).
  - `Roll` — drag a clip seam → `rollTrim`.
  - `Slip` — drag a clip body horizontally → `slipClip`.
  - `Spacer` — drag to insert/remove blank space on a track.
- **Insert/Overwrite toggle:** overwrite (default) vs insert; drag a clip with the
  insert mode (or Shift) uses `Track::insertClip`.
- **Ripple toggle** and **Snap toggle** (snap exists) in the toolbar.
- Mode-appropriate cursor feedback (already partially present for trim).

**New controller primitives (if needed):**
- `insertMove`/`overwriteMove` wrapper or an insert flag on `groupMove`.
- `addSpacer(track, at, duration)` / `removeSpacer` for Spacer mode.

**Files:** `timeline_panel.{hpp,cpp}`, `timeline_edit_controller.{hpp,cpp}`.

**Tests:** each mode's preview + commit produces the expected model change and one undo
step; insert mode ripples, overwrite mode does not; refusing edits on locked tracks.

> **Status — implemented.** `TimelinePanel` now owns a `TimelineState` value struct
> (edit mode, insert/overwrite, ripple, snap, lane order) exposed via
> `timelineState()/setTimelineState()` plus per-field helpers
> (`editMode()/setEditMode()`, `insertMode()/setInsertMode()`,
> `rippleEnabled()/setRippleEnabled()`, and the retained `snapEnabled()`),
> with a single `timelineStateChanged()` signal. The top bar gained an
> **edit-mode combo** (`Select/Range/Ripple/Roll/Slip/Spacer`) and **Insert** /
> **Ripple** toggle buttons. `TimelineView` drives all six modes: `Range` sets a
> panel-level zone (`setZone()/clearZone()/hasZone()/zoneIn()/zoneOut()` + a new
> `ZoneItem` overlay + `zoneChanged()`, feeding Phase 5), `Ripple` drags through
> `rippleMove`, `Roll` seams through `rollTrim`, `Slip` bodies through `slipClip`,
> and `Spacer` drags call `addSpacer`/`removeSpacer`. Select drags honour the
> Insert toggle via `insertMove` (falls back to the overwrite `groupMove`), and
> the Ripple toggle makes plain Delete ripple (`performDelete`). The four new
> controller primitives (`insertMove`, `rippleMove`, `addSpacer`, `removeSpacer`)
> use the atomic snapshot pattern (one `FunctionCommand` per edit). `MainWindow`
> persists the state under `timeline/editMode`, `timeline/insertMode`,
> `timeline/ripple` and `timeline/snap`, written on every change and restored in
> `maybeRestoreLayout` (clamped). Covered by 5 controller tests
> (`TimelineEditControllerModes.*`) and 9 panel tests (`TimelineEditState.*`)
> plus `MainWindow.editStatePersistsAcrossWindows`.

---

### Phase 5 — Zone / work area (in/out)

**Deliverable:** Kdenlive's zone strip on the ruler.

- In/out handles on the ruler plus a translucent zone overlay band.
- Toolbar buttons: **Set Zone In**, **Set Zone Out**, **Clear Zone**.
- Actions: **Lift** and **Extract** the zone → `liftZone`/`extractZone` (locked tracks
  skipped, per the P1 rule); "Delete Zone" convenience.
- Zone reflected in export range later (out of scope here; note as follow-up).

**Files:** `timeline_items.{hpp,cpp}` (zone item), `timeline_panel.{hpp,cpp}`,
`timeline_edit_controller.*` (already has the ops).

**Tests:** zone set/clear; lift keeps outside content; extract closes the gap; locked
tracks skipped; empty zone refused.

> **Status — implemented.** `TimelinePanel` owns the zone as before
> (`hasZone()`/`zoneIn()`/`zoneOut()`/`zoneDuration()`/`setZone()`/`clearZone()`)
> and gained `setZoneIn()`/`setZoneOut()` (clamped so `in <= out`; the first
> call opens a zero-length zone at that point) plus `liftZone()`/`extractZone()`
> wrappers over the controller's existing ops (a missing/empty zone or a no-op
> refuses with a status message). The top bar gained **Set In** / **Set Out** /
> **Clear Zone** buttons (object names `zoneInButton`/`zoneOutButton`/
> `clearZoneButton`) that act at the playhead, and `MainWindow` added a
> **Timeline** menu with **Set Zone In** (`I`), **Set Zone Out** (`O`),
> **Clear Zone** (`Shift+Z`), **Lift Zone** (`Ctrl+Shift+L`) and
> **Extract Zone** (`Ctrl+Shift+X`) actions bound to the panel. The `ZoneItem`
> band (already drawn over ruler + lanes) now also supports grabbing its in/out
> edges on the ruler: a press within 6 px of an edge starts a `ZoneIn`/`ZoneOut`
> drag that adjusts that edge live, and the hover cursor becomes a horizontal
> resize near either edge in any edit mode (a ruler click away from the handles
> still scrubs and no longer risks clearing the zone). Covered by 7 panel tests
> (`TimelineZone.*`) + `MainWindow.zoneActionsDriveTimelineZone`; the underlying
> `liftZone`/`extractZone` semantics remain covered by
> `TimelineEditControllerZones.*`.

---

### Phase 6 — A/V linking

**Deliverable:** Kdenlive-style linked audio/video behavior.

- **Linked selection:** selecting one clip expands to its `groupMembers`.
- **Linked move:** moving a video clip moves its linked audio half to the matching audio
  lane (and vice versa); validate kind + placement; single undo step (`groupMove`).
- **Linked trim/delete** operate on all group members where sensible.
- **Group/Ungroup:** Ctrl+G / Ctrl+Shift+G, plus context-menu entries.
- **Link badge** drawn on `ClipItem` for grouped clips.
- **Auto-split on drop** (decision: default-on, toggleable): dropping an A/V clip to a
  video track creates the linked video+audio halves via `splitAudioVideo` semantics.

**Files:** `timeline_panel.{hpp,cpp}`, `timeline_items.{hpp,cpp}`,
`timeline_edit_controller.{hpp,cpp}` (extend group move across kind lanes if needed).

**Tests:** select video → audio sibling selected; move/trim/delete affect both as one
undo; group/ungroup toggle; auto-split creates a valid group.

---

### Phase 7 — Context menus, clipboard, markers

**Deliverable:** right-click workflows and marker editing.

- `TimelineView::contextMenuEvent`:
  - **Clip:** Cut / Copy / Paste / Delete / Ripple Delete / Split at playhead /
    Split Audio / Lift Zone / Extract Zone / Group / Ungroup / Add Transition /
    Add Effect / Set Color Label / Enable-Disable / Rename.
  - **Track header:** rename / insert above / insert below / delete / lock / hide-mute /
    solo.
  - **Ruler:** add marker / edit marker / delete marker / set zone in/out.
  - **Empty area:** paste / add track.
- **Clipboard:** add copy/cut/paste to `TimelineEditController` (single undo step per
  paste), honoring kind lanes and `canPlaceAt`.
- **Markers:** add at playhead, drag to move, edit label/color (dialog), delete; wire to
  the existing marker mutators.
- Clip "Add effect"/"Add transition" route to the existing Inspector/transition paths.

**Files:** `timeline_panel.{hpp,cpp}`, `timeline_items.{hpp,cpp}`,
`timeline_edit_controller.{hpp,cpp}`, possibly `app/main_window.cpp`.

**Tests:** each menu action maps to the correct controller call; copy/paste round-trips;
marker add/drag/edit/delete; refusals reported.

---

### Phase 8 — Shortcuts, docs, tests, persistence

**Deliverable:** finish Kdenlive parity and document it.

- Register timeline actions in `ActionRegistry` (remappable); offer a Kdenlive-compatible
  mapping for common keys (S split, X/Cut, etc.) without breaking existing bindings.
- Persist `TimelineState` + lane order + active track; verify round-trip.
- Docs:
  - `docs/architecture/ARCHITECTURE.md` §5.2 (timeline view state), §8.6 TimelinePanel row,
    add a §19 deviation entry recording the UI phases.
  - `FEATURES.md` (Kdenlive timeline line — mark shipped).
  - `CHANGELOG.md` (new "Kdenlive-parity timeline, phase 2..N" entries).
  - `docs/user-guide.md` (track controls, edit modes, zone, linking, context menus).
  - `PROJECT_STATUS.md` phase entries + new test counts.
  - `README.md` feature table if user-visible.
- Test coverage: extend `tests/ui/test_timeline_panel.cpp` and
  `tests/ui/test_timeline_edit_controller.cpp`; keep the suites green in
  `linux-debug` + `linux-release` (+ `linux-tsan` for non-UI).

---

## 5. New/changed interfaces (sketch)

```cpp
// TimelineState (UI-only, persisted to QSettings)
enum class TimelineEditMode { Select, Range, Ripple, Roll, Slip, Spacer };
struct TimelineState {
    TimelineEditMode mode{TimelineEditMode::Select};
    bool insertMode{false};   // false = overwrite
    bool ripple{false};
    bool snap{true};
    timeline_geometry::LaneOrder laneOrder{timeline_geometry::LaneOrder::Split};
};

// Controller additions
bool MixerController::setTrackHidden(int flat, bool hidden);
bool MixerController::setTrackName(int flat, std::string name);
bool TimelineEditController::setTrackName(int flat, std::string name);       // if preferred
bool TimelineEditController::addSpacer(int flat, Time at, Duration dur);     // Spacer mode
bool TimelineEditController::removeSpacer(int flat, Time at, Duration dur);
// clipboard
void TimelineEditController::copyClips(const std::vector<std::pair<int,ClipId>>&);
void TimelineEditController::pasteClips(Time at);
```

> Exact placement (MixerController vs TimelineEditController vs a new
> `TimelineViewController`) is finalised per phase; the rule is: layout edits →
> `TimelineEditController`, track property edits → `MixerController`.

---

## 6. Decisions to confirm during implementation

1. **Persistence of timeline view state:** QSettings only (per-user) vs. also per-project
   (`SequenceSettings`). Recommendation: QSettings for mode/ripple/snap/active track;
   lane order may be per-project if users expect it to travel with the project.
2. **A/V auto-split on drop:** default-on with a toggle (recommended).
3. **Ripple accelerator:** keep Shift-for-ripple *and* the toolbar toggle (recommended).
4. **Track height resize:** in scope (recommended) — model field already serialized.
5. **Kdenlive key bindings:** add as a selectable scheme rather than overriding defaults.

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| TrackHeader refactor from paint to widgets breaks scroll sync / tests | Keep the existing `setScrollOffset` contract; add rows without changing the panel API; cover with offscreen tests |
| Lane-order threading touches every geometry call site | Centralize via a single `laneTop/flatForLane` accessor on `TimelinePanel`; add round-trip tests per order |
| Interaction modes enlarge `TimelineView` state machine | Extract mode handlers; unit-test preview vs commit separately |
| Linked move across kind lanes can partially fail | Validate against a working `Sequence` copy (existing drop-planning pattern); single undo via `runSequenceEdit` |
| Shortcut collisions with existing bindings | Route through `ActionRegistry`; offer an opt-in Kdenlive scheme |
| Test-count drift in docs | Update counts in the same PR as each phase |

---

## 8. Acceptance criteria

- Timeline visually and behaviorally matches Kdenlive for: track control panel, lane
  order modes, edit/operation modes, insert/overwrite, zone in/out, A/V linking,
  context menus, markers.
- Every timeline mutation is a single undo step; locked-track refusals are reported.
- All offscreen UI tests green in `linux-debug` and `linux-release`; core/model tests
  green under `linux-tsan`.
- Docs updated (`ARCHITECTURE`, `FEATURES`, `CHANGELOG`, user guide, `PROJECT_STATUS`,
  `README`) with accurate shipped-vs-planned status.

---

## 9. Suggested delivery order

1. Phase 2 (track headers) — highest visible parity, low model risk.
2. Phase 3 (lane order + fit) — small, isolated geometry plumbing.
3. Phase 4 (toolbar + modes) — unlocks editing parity.
4. Phase 5 (zone) — small add-on to Phase 4.
5. Phase 6 (A/V linking) — builds on groups.
6. Phase 7 (menus/clipboard/markers) — breadth.
7. Phase 8 (shortcuts/docs/tests) — finalize.

Each phase lands independently with its own tests and doc updates.
