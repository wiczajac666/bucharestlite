#pragma once

#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_core/media_source.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/sequence.hpp>

#include "panels/mixer_controller.hpp"
#include "panels/timeline_edit_controller.hpp"
#include "panels/timeline_items.hpp"

#include <QGraphicsView>
#include <QSet>
#include <QWidget>

#include <map>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QGraphicsLineItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QSlider;
class QToolButton;
class QWheelEvent;

namespace bl::ui {

class ClipItem;
class ProjectController;
class TimelineEditController;
class TimelinePanel;
class TrackHeader;
class TrackHeaderRow;
class TimelineView;

// ---------------------------------------------------------------------------
// TrackHeaderRow: one interactive lane header. Shows the track name plus
// kind-appropriate toggles (lock; hide for video / mute+solo for audio), opens
// the track context menu, supports inline rename and drag-reorder.
// ---------------------------------------------------------------------------
class TrackHeaderRow : public QWidget {
    Q_OBJECT
public:
    TrackHeaderRow(TimelinePanel* host, int flat, QWidget* parent = nullptr);

    int flatTrack() const { return flat_; }

    // Syncs widget state (name, toggle checks, kind-gated visibility) from the
    // model. Called whenever the header is rebuilt.
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void beginRename();
    void commitRename();
    void cancelRename();
    void openMenu(const QPoint& globalPos);

    TimelinePanel* host_{nullptr};
    int flat_{0};
    bool video_{false};
    bool active_{false};
    bool renameActive_{false};
    bool dragArmed_{false};
    bool dragActive_{false};
    QPoint pressGlobal_;

    QLabel* nameLabel_{nullptr};
    QLineEdit* nameEdit_{nullptr};
    QToolButton* lockBtn_{nullptr};
    QToolButton* avBtn_{nullptr};   // Hide (video) / Mute (audio)
    QToolButton* soloBtn_{nullptr}; // audio only
    QToolButton* menuBtn_{nullptr};
};

// ---------------------------------------------------------------------------
// TrackHeader: the fixed left-hand column of lane titles, kept in vertical
// sync with the view's scrollbar. Hosts one TrackHeaderRow per lane.
// ---------------------------------------------------------------------------
class TrackHeader : public QWidget {
    Q_OBJECT
public:
    explicit TrackHeader(TimelinePanel* host, QWidget* parent = nullptr);

    void setScrollOffset(int offset);

    // (Re)creates and refreshes one row per lane, then repositions them.
    void rebuild();
    TrackHeaderRow* rowFor(int flat) const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void layoutRows();

    TimelinePanel* host_{nullptr};
    int offset_{0};
    std::vector<TrackHeaderRow*> rows_;
};

// ---------------------------------------------------------------------------
// TimelineView: the QGraphicsView that owns pointer/key interaction with the
// timeline. It only mutates the model through TimelineEditController commands;
// every preview state is visual and discarded on commit failure.
// ---------------------------------------------------------------------------
class TimelineView : public QGraphicsView {
    Q_OBJECT
public:
    explicit TimelineView(TimelinePanel* host, QWidget* parent = nullptr);

    TimelinePanel* panel() const { return host_; }

    // Discards any in-progress drag visuals (called on model rebuilds).
    void cancelActiveGesture();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    friend class TimelinePanel;

    // In-progress external drop session. One candidate per dragged item: a
    // media-bin drag always has exactly one (it carries a single id), while a
    // file-manager drag may carry many. For an external file `path` is set and
    // `mediaId` stays empty until the drop is committed, because the id is
    // only assigned when the file is imported into the media bin.
    struct DropCandidate {
        std::string mediaId;
        QString path;
        std::optional<bl::StreamInfo> probe;
    };
    struct DropSession {
        std::vector<DropCandidate> items;
    };
    struct DropPlacement {
        int flat{-1};
        bl::Time start{0, {1'000'000, 1}};
        bl::Clip clip;
    };
    struct DropTarget {
        std::vector<DropPlacement> placements;
        bool valid{false};
        // Why the plan was rejected (empty while valid). Surfaced in the
        // status bar so a refused drop is never silent.
        QString reason;
    };

    struct PendingMove {
        ClipId id;
        int fromTrack{0};
        int toTrack{0};
        Time oldStart{0, {1'000'000, 1}};
        Time newStart{0, {1'000'000, 1}};
    };

    ClipItem* clipItemAt(const QPointF& scenePos) const;
    int64_t snapToleranceFrames() const;

    void updateMovePreview(const QPointF& scenePos);
    void updateTrimPreview(const QPointF& scenePos, bool leftEdge);
    void commitMove();
    void commitTrim(bool leftEdge);
    void finishMarquee(const QPointF& scenePos, Qt::KeyboardModifiers mods);
    void updateHoverCursor(const QPointF& scenePos);
    void clearSnapIndicator();

    // --- Phase 4 edit-mode gesture handling ---------------------------------
    // Nearest shared seam of two adjacent clips on `track`, within a few
    // pixels of `sceneX`. Returns false when the track has no such seam.
    bool seamNear(int track, qreal sceneX, ClipId& leftId, ClipId& rightId,
                  bl::Time& boundary) const;
    void updateRangePreview(const QPointF& scenePos);
    void updateRipplePreview(const QPointF& scenePos);
    void updateSlipPreview(const QPointF& scenePos);
    void updateRollPreview(const QPointF& scenePos);
    void updateSpacerPreview(const QPointF& scenePos);
    void commitRipple();
    void commitSlip();
    void commitRoll();
    void commitSpacer();

    // External drop (media bin row, or OS file-manager payload → timeline)
    // session handling.
    bool startDropSession(const std::string& mediaId);
    bool startDropSessionFromFiles(const QStringList& paths);
    void updateDropSession(const QPointF& scenePos);
    void finishDropSession(const QPointF& scenePos);
    void clearDropSession();
    void dropTargetAt(const QPointF& scenePos, DropTarget& target) const;
    void hideDropGhosts();
    // Reports a drag/drop refusal in the status bar, deduplicated so the
    // per-pixel drag-move stream cannot flood it with the same line.
    void reportDropProblem(const QString& reason);

    void performDelete(bool ripple);
    void performSplit();
    void performSelectAll();

    TimelinePanel* host_{nullptr};
    enum class Mode {
        None,
        Scrub,
        Move,
        TrimLeft,
        TrimRight,
        Marquee,
        Range,
        Ripple,
        Slip,
        Roll,
        Spacer,
        ZoneIn,
        ZoneOut
    };
    Mode mode_{Mode::None};
    QPointF pressScenePos_;
    QPoint pressViewportPos_;
    Time pressTime_{0, {1'000'000, 1}};
    int pressLane_{-1};
    ClipId pressClip_;
    QGraphicsRectItem* marqueeItem_{nullptr};
    std::vector<PendingMove> pendingMoves_;
    bool dragging_{false};

    // External drop session state.
    DropSession dropSession_;
    DropTarget dropTarget_;
    // One placement ghost per planned clip; all share the same object name so
    // the existing single-item ghost lookup keeps working.
    std::vector<QGraphicsRectItem*> dropGhosts_;
    // Last refusal already shown during the current drag (see
    // reportDropProblem); reset whenever a new session starts or the target
    // becomes valid again.
    QString lastDropReason_;

    // Trim scratch state.
    bool trimRipple_{false};
    int trimFlat_{-1};
    Time trimAnchorStart_{0, {1'000'000, 1}};
    Time trimAnchorEnd_{0, {1'000'000, 1}};
    Time trimCurrent_{0, {1'000'000, 1}};

    // Phase 4 gesture scratch state.
    Time rangeAnchor_{0, {1'000'000, 1}};
    int rollFlat_{-1};
    ClipId rollLeftId_;
    ClipId rollRightId_;
    Time rollAnchor_{0, {1'000'000, 1}};
    Time rollCurrent_{0, {1'000'000, 1}};
    Time slipAnchor_{0, {1'000'000, 1}};
    Time slipCurrent_{0, {1'000'000, 1}};
    int spacerFlat_{-1};
    Time spacerAnchor_{0, {1'000'000, 1}};
    Time spacerCurrent_{0, {1'000'000, 1}};
};

// ---------------------------------------------------------------------------
// TimelineState: the Kdenlive-style edit state owned by TimelinePanel. The edit
// mode determines what a lane drag does (select/move, set the zone, ripple,
// roll a seam, slip the source window, insert/remove space); insert/overwrite
// chooses whether a move ripples following clips or replaces/pushes at the
// drop point; the ripple flag makes plain delete and trim ripple too.
// Persisted by MainWindow via QSettings.
// ---------------------------------------------------------------------------
struct TimelineState {
    enum class EditMode { Select, Range, Ripple, Roll, Slip, Spacer };
    EditMode editMode{EditMode::Select};
    bool insertMode{false};  // false = overwrite (default)
    bool ripple{false};
    bool snap{true};
    bool autoSplitAv{true};  // drop an A/V clip as a linked video+audio pair
    timeline_geometry::LaneOrder laneOrder{timeline_geometry::LaneOrder::Split};
};

// ---------------------------------------------------------------------------
// TimelinePanel: ruler + lanes + clips + playhead with snapping, zoom,
// marquee selection and undoable move/trim/split/delete gestures.
// ---------------------------------------------------------------------------
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(ProjectController* controller, QWidget* parent = nullptr);

    ProjectController* controller() const { return controller_; }
    TimelineEditController& editor() { return editor_; }
    TimelineView* view() const { return view_; }
    TrackHeader* header() const { return header_; }

    // Model access.
    const bl::Sequence& sequence() const;
    void rebuildFromModel();

    // Audio waveform display (P1 #13). `resolver` hands each audio clip the
    // analyzed envelope for its source (MainWindow wires the shared
    // AudioMeterEngine's cache lookup here); a null resolver simply draws no
    // waveforms. Background analysis settles asynchronously, so call
    // refreshWaveforms() when new envelopes arrive to repaint without a full
    // rebuild (which would cancel an in-progress gesture).
    void setWaveformResolver(bl::SourceMeterResolver resolver) {
        waveformResolver_ = std::move(resolver);
        refreshWaveforms();
    }
    const bl::SourceMeterResolver& waveformResolver() const {
        return waveformResolver_;
    }
    void refreshWaveforms();

    // Geometry / time mapping (scene coordinates, origin at ruler top-left).
    qreal xForTime(const bl::Time& t) const;
    bl::Time rawTimeAtX(qreal sceneX) const;
    bl::Time timeAtX(qreal sceneX, const bl::ClipId& dragging = bl::ClipId()) const;
    int flatAtY(qreal sceneY) const;
    QRectF laneRect(int flat) const;
    int videoCount() const;
    int trackCount() const;

    // Zoom (pixels per timeline frame).
    qreal zoom() const { return pxPerFrame_; }
    void setZoom(qreal pxPerFrame);
    // Sets zoom so the whole sequence width fits the viewport (clamped).
    void fitZoomToViewport();

    // --- Lane layout (Phase 3) ---------------------------------------------
    // Kdenlive's three track layouts; default Split (video above, audio
    // below). Mixed / MixedReverse interleave audio with its video lane.
    timeline_geometry::LaneOrder laneOrder() const { return state_.laneOrder; }
    void setLaneOrder(timeline_geometry::LaneOrder order);

    // --- Edit state (Phase 4) ----------------------------------------------
    const TimelineState& timelineState() const { return state_; }
    void setTimelineState(const TimelineState& state);

    TimelineState::EditMode editMode() const { return state_.editMode; }
    void setEditMode(TimelineState::EditMode mode);

    // Insert (true) vs overwrite (false) for clip drags.
    bool insertMode() const { return state_.insertMode; }
    void setInsertMode(bool on);

    // Global ripple: plain Delete/trim ripple like their Shift variants.
    bool rippleEnabled() const { return state_.ripple; }
    void setRippleEnabled(bool on);

    // A/V linking (Phase 6): dropping an A/V clip onto a video lane also
    // creates its linked audio half. Toggleable; default on.
    bool autoSplitAv() const { return state_.autoSplitAv; }
    void setAutoSplitAv(bool on);

    // --- Timeline zone (Phase 4/5) -----------------------------------------
    // In/out range set by Range-mode ruler drags and the toolbar buttons; the
    // zone strip and lift/extract operations build on this.
    bool hasZone() const { return hasZone_; }
    bl::Time zoneIn() const { return zoneIn_; }
    bl::Time zoneOut() const { return zoneOut_; }
    bl::Duration zoneDuration() const { return zoneOut_ - zoneIn_; }
    void setZone(const bl::Time& in, const bl::Time& out);
    // Sets one edge to `t`, clamped so in <= out; the first call opens a
    // zero-length zone. Used by the toolbar buttons and ruler handle drags.
    void setZoneIn(const bl::Time& t);
    void setZoneOut(const bl::Time& t);
    void clearZone();

    // Lift removes everything inside the zone (leaving a gap); Extract also
    // closes the gap by rippling later clips left. Both skip locked tracks and
    // are a single undo step. A missing/empty zone or no-op refuses with a
    // status message (via the controller) and returns false.
    bool liftZone();
    bool extractZone();

    // Playhead.
    bl::Time playhead() const { return playhead_; }
    void setPlayhead(const bl::Time& t);

    // Snap.
    bool snapEnabled() const { return state_.snap; }
    void setSnapEnabled(bool on);

    // Selection.
    QSet<bl::ClipId> selection() const { return selection_; }
    void setSelection(const QSet<bl::ClipId>& ids);
    void clearSelection();

    // --- Clip groups (Phase 6 A/V linking) ----------------------------------
    // Other live clips grouped with `id` ({} when ungrouped).
    QSet<bl::ClipId> groupMembersOf(const bl::ClipId& id) const;
    // `ids` expanded with every group it touches; used so selecting one half
    // of a linked A/V pair selects the whole pair.
    QSet<bl::ClipId> expandSelection(const QSet<bl::ClipId>& ids) const;
    // Groups / ungroups the current selection (Ctrl+G / Ctrl+Shift+G). Each is
    // a single undo step for the common single-group case; refusals are
    // reported through the controller's statusMessage.
    bool groupSelection();
    bool ungroupSelection();

    // Test/UX helpers.
    int clipItemCount() const;
    ClipItem* clipItemFor(const bl::ClipId& id) const;
    int markerItemCount() const;

    // Adds a media-bin item to the timeline without a drag: the clip is
    // appended at the end of the first track whose kind matches the media
    // (video → first video lane, audio-only → first audio lane). Returns
    // false and leaves the document untouched when the media cannot be
    // probed or no lane can take it; every refusal is reported through the
    // controller's statusMessage. This is the "Add to Timeline" entry point
    // behind the media-bin context menu and double-click.
    //
    // `durationOverride` forces the clip's length instead of the probed media
    // duration; used for stills/titles whose stream has no meaningful length
    // (the decoder holds the last frame for the whole span).
    bool addMediaToTimeline(
        const std::string& mediaId,
        std::optional<bl::Duration> durationOverride = std::nullopt);

    // --- Subtitle import ----------------------------------------------------
    // Turns parsed subtitle cues into soft-subtitle clips on a dedicated
    // "Subtitles" video track (one undo step). Returns the number of cues
    // placed; 0 is reported through statusMessage.
    int importSubtitleCues(const std::vector<bl::SubtitleCue>& cues);

    // Bounding span of the current selection, for per-clip export. Returns
    // false when nothing is selected.
    bool selectedClipRange(bl::Time& in, bl::Time& out) const;

    // --- Clipboard (Phase 7) ------------------------------------------------
    // Copy/cut gather the current selection into the controller clipboard; cut
    // additionally deletes the selection as one undo step. Paste drops the
    // clipboard at `at` (default: the playhead) on the active track, or the
    // first lane with room. Each returns the number of clips touched; a zero
    // result is reported through statusMessage. Wired to Ctrl+C / Ctrl+X /
    // Ctrl+V.
    int copySelection();
    int cutSelection();
    int pasteAtPlayhead();
    int pasteAt(const bl::Time& at);

    // --- Markers (Phase 7) --------------------------------------------------
    // Add / rename / remove a sequence marker at `at`; each is one undo step
    // and reports refusals through statusMessage. `addMarkerAt` picks a default
    // label when none is given.
    bool addMarkerAt(const bl::Time& at, const QString& label = QString());
    bool renameMarkerAt(const bl::Time& at, const QString& label);
    bool removeMarkerAt(const bl::Time& at);
    // Position of the marker within `radiusFrames` of `at`, if any (used for
    // hit-testing marker clicks). Returns false when none is close enough.
    bool markerNear(const bl::Time& at, int radiusFrames,
                    bl::Time& out) const;

    // --- Track header operations (Phase 2) ---------------------------------
    // All mutate the model through the existing controllers, so each is a
    // single undo step and failures are reported through statusMessage. The
    // header rows, the context menu and the toolbar buttons all funnel here.
    MixerController& mixer() { return mixer_; }

    int activeTrack() const { return activeTrack_; }
    void setActiveTrack(int flat);

    bool setTrackLocked(int flat, bool locked);
    bool setTrackMuted(int flat, bool muted);
    bool setTrackHidden(int flat, bool hidden);
    bool setTrackSoloed(int flat, bool soloed);
    bool renameTrack(int flat, const QString& name);

    // Adds an empty video/audio track at the end of its kind.
    bool addTrack(bool video);
    // Inserts an empty track above (`above == true`) or below `flat`.
    bool insertTrackAt(int flat, bool above);
    bool removeTrackAt(int flat);
    bool moveTrackUp(int flat);
    bool moveTrackDown(int flat);
    // Moves `flat` to `targetFlat` (must be the same kind).
    bool moveTrackTo(int flat, int targetFlat);

    // Maps a screen-global y to a flat track (used by header drag-reorder).
    int flatAtGlobalY(int globalY) const;

signals:
    void playheadChanged(const bl::Time& current);
    void selectionChanged();
    void timelineChanged();
    void activeTrackChanged(int flat);
    void laneOrderChanged(timeline_geometry::LaneOrder order);
    void timelineStateChanged();
    void zoneChanged();
    // "Edit Title…" on a title clip's context menu. Emitted with the clip's
    // media id; the receiver (MainWindow) re-opens the Title Editor.
    void editTitleRequested(const std::string& mediaId);

private:
    friend class TimelineView;
    friend class TrackHeader;
    friend class TrackHeaderRow;
    void onDocumentChanged();
    void updatePlayheadItem();
    void updateZoneItem();
    void updateZoomLabel();
    // Emits editor_.lastError() (when non-empty) through statusMessage.
    void reportEditorError();
    // Shared body of liftZone()/extractZone(); refuses a missing/empty zone.
    bool applyZoneEdit(bool extract);
    // First audio lane that can hold `clip` at `start` (preferring the lane
    // aligned with `videoFlat`), or -1 when none fits. Used by A/V auto-split.
    int audioLaneFor(int videoFlat, const bl::Clip& clip,
                     const bl::Time& start) const;

    ProjectController* controller_{nullptr};
    TimelineEditController editor_;
    MixerController mixer_;
    TimelineView* view_{nullptr};
    TrackHeader* header_{nullptr};
    QCheckBox* snapCheck_{nullptr};
    QToolButton* zoomIn_{nullptr};
    QToolButton* zoomOut_{nullptr};
    QToolButton* addVideoBtn_{nullptr};
    QToolButton* addAudioBtn_{nullptr};
    QToolButton* fitBtn_{nullptr};
    QComboBox* laneOrderCombo_{nullptr};
    QComboBox* editModeCombo_{nullptr};
    QToolButton* insertModeBtn_{nullptr};
    QToolButton* rippleBtn_{nullptr};
    QToolButton* avSplitBtn_{nullptr};
    QToolButton* zoneInBtn_{nullptr};
    QToolButton* zoneOutBtn_{nullptr};
    QToolButton* clearZoneBtn_{nullptr};
    QSlider* zoomSlider_{nullptr};
    QLabel* zoomLabel_{nullptr};
    QGraphicsScene* scene_{nullptr};
    PlayheadItem* playheadItem_{nullptr};
    SnapIndicatorItem* snapIndicator_{nullptr};
    ZoneItem* zoneItem_{nullptr};

    bl::Time playhead_{0, {1'000'000, 1}};
    qreal pxPerFrame_{6.0};
    TimelineState state_;
    int activeTrack_{0};
    bl::Time zoneIn_{0, {1'000'000, 1}};
    bl::Time zoneOut_{0, {1'000'000, 1}};
    bool hasZone_{false};
    QSet<bl::ClipId> selection_;
    std::map<bl::ClipId, ClipItem*> clipItems_;
    bl::SourceMeterResolver waveformResolver_;
};

} // namespace bl::ui