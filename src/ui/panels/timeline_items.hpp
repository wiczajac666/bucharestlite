#pragma once

#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>

#include <optional>
#include <string>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
QT_END_NAMESPACE

namespace bl::ui {

// Stored on every scene item that wants to talk back to the model.
constexpr int kClipItemIdRole = Qt::UserRole;
constexpr int kClipItemTrackRole = Qt::UserRole + 1;

// ---------------------------------------------------------------------------
// Ruler: frame ticks + (HH:MM:SS:FF) labels along the top of the timeline.
// ---------------------------------------------------------------------------
class RulerItem : public QGraphicsRectItem {
public:
    explicit RulerItem(qreal height);

    void configure(const bl::SequenceSettings& settings);
    void setWidth(qreal width);
    void setZoom(qreal pxPerFrame);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bl::SequenceSettings settings_;
    qreal pxPerFrame_{6.0};
};

// ---------------------------------------------------------------------------
// Playhead: the vertical current-time line spanning ruler + lanes.
// ---------------------------------------------------------------------------
class PlayheadItem : public QGraphicsLineItem {
public:
    PlayheadItem();

    void setHeight(qreal height);
};

// ---------------------------------------------------------------------------
// Snap indicator: transient dashed vertical guide shown while snapping.
// ---------------------------------------------------------------------------
class SnapIndicatorItem : public QGraphicsLineItem {
public:
    SnapIndicatorItem();

    void setHeight(qreal height);
    void setActive(bool active) { setVisible(active); }
};

// ---------------------------------------------------------------------------
// Marker: a flagged frame (small triangle + thin guide).
// ---------------------------------------------------------------------------
class MarkerItem : public QGraphicsRectItem {
public:
    MarkerItem(const bl::Marker& marker, qreal x, qreal topY, qreal height);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bl::Marker marker_;
};

// ---------------------------------------------------------------------------
// Lane: translucent background band for a single track.
// ---------------------------------------------------------------------------
class LaneItem : public QGraphicsRectItem {
public:
    LaneItem(qreal y, qreal height, qreal width, bool videoKind);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bool videoKind_;
};

// ---------------------------------------------------------------------------
// Clip: a rect with name label, trim-handle hit zones, transition hatch and
// subtitle badge. Scene position follows the model; during a drag the view
// sets a preview rectangle via setPreviewRect() without touching the model.
// ---------------------------------------------------------------------------
class ClipItem : public QGraphicsRectItem {
public:
    enum class Edge { None, Left, Right };

    ClipItem(const bl::Clip& clip, int flatTrack, qreal pxPerFrame,
             const bl::Rational& fps);

    const bl::Clip& clip() const { return clip_; }
    int flatTrack() const { return flatTrack_; }
    qreal pxPerFrame() const { return pxPerFrame_; }

    QRectF geometry() const;
    Edge edgeAt(const QPointF& scenePos) const;
    qreal handleWidth() const { return 6.0; }

    void setZoom(qreal pxPerFrame);
    void setLaneRect(const QRectF& laneRect);
    void setPreviewRect(const QRectF& rect);
    void clearPreview();
    bool hasPreview() const { return preview_.has_value(); }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bl::Clip clip_;
    int flatTrack_{0};
    qreal pxPerFrame_{6.0};
    bl::Rational fps_;
    QRectF laneRect_;
    std::optional<QRectF> preview_;
};

namespace timeline_geometry {

constexpr qreal kRulerHeight = 26.0;
constexpr qreal kVideoLaneHeight = 44.0;
constexpr qreal kAudioLaneHeight = 36.0;

qreal laneTop(const bl::Sequence& seq, int flat, int videoCount);
qreal laneHeight(const bl::Sequence& seq, int flat, int videoCount);
qreal contentWidth(const bl::Sequence& seq, qreal pxPerFrame);
qreal contentHeight(const bl::Sequence& seq);

} // namespace timeline_geometry

} // namespace bl::ui