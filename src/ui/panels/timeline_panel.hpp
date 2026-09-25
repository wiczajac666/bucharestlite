#pragma once

#include <bl_core/media_source.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/sequence.hpp>

#include "panels/timeline_edit_controller.hpp"
#include "panels/timeline_items.hpp"

#include <QGraphicsView>
#include <QSet>
#include <QWidget>

#include <map>
#include <optional>
#include <string>

class QCheckBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QGraphicsLineItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QToolButton;
class QWheelEvent;

namespace bl::ui {

class ClipItem;
class ProjectController;
class TimelineEditController;
class TimelinePanel;
class TrackHeader;
class TimelineView;

// ---------------------------------------------------------------------------
// TrackHeader: the fixed left-hand column of lane titles, kept in vertical
// sync with the view's scrollbar.
// ---------------------------------------------------------------------------
class TrackHeader : public QWidget {
    Q_OBJECT
public:
    explicit TrackHeader(TimelinePanel* host, QWidget* parent = nullptr);

    void setScrollOffset(int offset);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TimelinePanel* host_{nullptr};
    int offset_{0};
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
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    friend class TimelinePanel;

    // In-progress external drop (media bin → timeline) session.
    struct DropSession {
        std::string mediaId;
        std::optional<bl::StreamInfo> probe;
    };
    struct DropTarget {
        int flat{-1};
        bl::Time start{0, {1'000'000, 1}};
        bl::Clip clip;
        bool valid{false};
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

    // External drop (media bin → timeline) session handling.
    bool startDropSession(const std::string& mediaId);
    void updateDropSession(const QPointF& scenePos);
    void finishDropSession(const QPointF& scenePos);
    void clearDropSession();
    void dropTargetAt(const QPointF& scenePos, DropTarget& target) const;

    void performDelete(bool ripple);
    void performSplit();
    void performSelectAll();

    TimelinePanel* host_{nullptr};
    enum class Mode { None, Scrub, Move, TrimLeft, TrimRight, Marquee };
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
    QGraphicsRectItem* dropGhost_{nullptr};

    // Trim scratch state.
    bool trimRipple_{false};
    int trimFlat_{-1};
    Time trimAnchorStart_{0, {1'000'000, 1}};
    Time trimAnchorEnd_{0, {1'000'000, 1}};
    Time trimCurrent_{0, {1'000'000, 1}};
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

    // Playhead.
    bl::Time playhead() const { return playhead_; }
    void setPlayhead(const bl::Time& t);

    // Snap.
    bool snapEnabled() const { return snap_; }
    void setSnapEnabled(bool on);

    // Selection.
    QSet<bl::ClipId> selection() const { return selection_; }
    void setSelection(const QSet<bl::ClipId>& ids);
    void clearSelection();

    // Test/UX helpers.
    int clipItemCount() const;
    ClipItem* clipItemFor(const bl::ClipId& id) const;
    int markerItemCount() const;

signals:
    void playheadChanged(const bl::Time& current);
    void selectionChanged();
    void timelineChanged();

private:
    friend class TimelineView;
    friend class TrackHeader;
    void onDocumentChanged();
    void updatePlayheadItem();
    void updateZoomLabel();

    ProjectController* controller_{nullptr};
    TimelineEditController editor_;
    TimelineView* view_{nullptr};
    TrackHeader* header_{nullptr};
    QCheckBox* snapCheck_{nullptr};
    QToolButton* zoomIn_{nullptr};
    QToolButton* zoomOut_{nullptr};
    QLabel* zoomLabel_{nullptr};
    QGraphicsScene* scene_{nullptr};
    PlayheadItem* playheadItem_{nullptr};
    SnapIndicatorItem* snapIndicator_{nullptr};

    bl::Time playhead_{0, {1'000'000, 1}};
    qreal pxPerFrame_{6.0};
    bool snap_{true};
    QSet<bl::ClipId> selection_;
    std::map<bl::ClipId, ClipItem*> clipItems_;
};

} // namespace bl::ui