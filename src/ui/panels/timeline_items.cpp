#include "panels/timeline_items.hpp"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace bl::ui {

namespace {

constexpr qreal kSnapBarWidth = 56.0; // min content width margin (px)

int64_t toFrame(const bl::Time& t, const bl::Rational& fps) {
    return static_cast<int64_t>(
        std::llround(t.toSeconds() * static_cast<double>(fps.num) /
                     static_cast<double>(fps.den)));
}

int64_t toFrame(const bl::Duration& d, const bl::Rational& fps) {
    return d.toFramesAt(fps);
}

qreal frameToX(int64_t frame, qreal pxPerFrame) {
    return static_cast<qreal>(frame) * pxPerFrame;
}

QColor labelColor(uint32_t label, bool selected) {
    static const QColor palette[6] = {
        QColor(0x8a, 0x9c, 0xb0), QColor(0x6c, 0x8f, 0xd3),
        QColor(0x88, 0xb0, 0x5a), QColor(0xd3, 0xa0, 0x51),
        QColor(0xc1, 0x6e, 0x5e), QColor(0x9a, 0x77, 0xc6)};
    const QColor& base = palette[static_cast<size_t>(label % 6)];
    return selected ? base.lighter(125) : base;
}

QString timecodeLabel(int64_t frame, const bl::Rational& fps) {
    const int fpsInt = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(fps.num) /
                                        static_cast<double>(fps.den))));
    const int totalSeconds = static_cast<int>(frame / static_cast<int64_t>(fpsInt));
    const int hh = totalSeconds / 3600;
    const int mm = (totalSeconds % 3600) / 60;
    const int ss = totalSeconds % 60;
    const int ff = static_cast<int>(frame % static_cast<int64_t>(fpsInt));
    return QString::asprintf("%02d:%02d:%02d:%02d", hh, mm, ss, ff);
}

} // namespace

// ---------------------------------------------------------------------------
// RulerItem
// ---------------------------------------------------------------------------
RulerItem::RulerItem(qreal height) : QGraphicsRectItem(0, 0, height, height) {
    setZValue(100);
}

void RulerItem::configure(const bl::SequenceSettings& settings) { settings_ = settings; }

void RulerItem::setWidth(qreal width) {
    setRect(0, 0, width, timeline_geometry::kRulerHeight);
}

void RulerItem::setZoom(qreal pxPerFrame) { pxPerFrame_ = pxPerFrame; }

void RulerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF r = rect();
    painter->fillRect(r, QColor(0x2a, 0x2d, 0x30));

    const bl::Rational& fps = settings_.fps;
    const qreal px = pxPerFrame_;
    const int64_t endFrame = static_cast<int64_t>(r.width() / px) + 1;

    // Smallest power-of-two tick step that keeps >=8px spacing. Ruler ticks
    // always start at frame 0 (negative times are not representable in UI-2).
    const int64_t rawStep =
        static_cast<int64_t>(std::max(1.0, std::ceil(8.0 / px)));
    int64_t tickStep = 1;
    while (tickStep < rawStep) tickStep *= 2;
    const int64_t labelEvery =
        std::max<int64_t>(1, static_cast<int64_t>(28.0 / (px * tickStep)));

    painter->setPen(QPen(QColor(0x9a, 0x9e, 0xa3), 1.0));
    QFont labelFont = painter->font();
    labelFont.setPixelSize(9);
    painter->setFont(labelFont);

    const qreal bottom = r.bottom();
    for (int64_t f = 0; f <= endFrame; f += tickStep) {
        const qreal x = frameToX(f, px);
        const bool major = (f % (tickStep * labelEvery)) == 0;
        painter->drawLine(QPointF(x, major ? bottom - 12.0 : bottom - 6.0),
                          QPointF(x, bottom));
        if (major) {
            painter->drawText(QPointF(x + 2.0, bottom - 14.0), timecodeLabel(f, fps));
        }
    }
}

// ---------------------------------------------------------------------------
// PlayheadItem
// ---------------------------------------------------------------------------
PlayheadItem::PlayheadItem() {
    setPen(QPen(QColor(0xe0, 0x5a, 0x5a), 1.5));
    setZValue(150);
}

void PlayheadItem::setHeight(qreal height) { setLine(0, 0, 0, height); }

// ---------------------------------------------------------------------------
// SnapIndicatorItem
// ---------------------------------------------------------------------------
SnapIndicatorItem::SnapIndicatorItem() {
    setPen(QPen(QColor(0x4d, 0xa3, 0xf0), 1.0, Qt::DashLine));
    setZValue(140);
    setVisible(false);
}

void SnapIndicatorItem::setHeight(qreal height) {
    setLine(0, 0, 0, height);
}

// ---------------------------------------------------------------------------
// MarkerItem
// ---------------------------------------------------------------------------
MarkerItem::MarkerItem(const bl::Marker& marker, qreal x, qreal topY,
                       qreal height)
    : QGraphicsRectItem(x - 5.0, topY, 10.0, height), marker_(marker) {
    setZValue(90);
}

void MarkerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF r = rect();
    const QColor base = labelColor(marker_.color, false);
    painter->setPen(Qt::NoPen);
    painter->setBrush(base);
    const QPolygonF triangle{QPointF(r.left(), r.top()),
                             QPointF(r.right(), r.top()),
                             QPointF(r.center().x(), r.top() + 9.0)};
    painter->drawPolygon(triangle);
    QColor guide = base;
    guide.setAlpha(90);
    QPen pen(guide, 1.0, Qt::DashLine);
    painter->setPen(pen);
    painter->drawLine(QPointF(r.center().x(), r.top() + 9.0),
                      QPointF(r.center().x(), r.bottom()));
}

// ---------------------------------------------------------------------------
// LaneItem
// ---------------------------------------------------------------------------
LaneItem::LaneItem(qreal y, qreal height, qreal width, bool videoKind)
    : QGraphicsRectItem(0, y, width, height), videoKind_(videoKind) {
    setZValue(0);
}

void LaneItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF r = rect();
    const QColor fill =
        videoKind_ ? QColor(0x2e, 0x3a, 0x46) : QColor(0x2c, 0x40, 0x35);
    painter->fillRect(r, fill);
    painter->setPen(QPen(QColor(0x3a, 0x3d, 0x40), 1.0));
    painter->drawLine(QPointF(r.left(), r.top()), QPointF(r.right(), r.top()));
}

// ---------------------------------------------------------------------------
// ClipItem
// ---------------------------------------------------------------------------
ClipItem::ClipItem(const bl::Clip& clip, int flatTrack, qreal pxPerFrame,
                   const bl::Rational& fps)
    : QGraphicsRectItem(), clip_(clip), flatTrack_(flatTrack),
      pxPerFrame_(pxPerFrame), fps_(fps) {
    setRect(geometry());
    setZValue(10);
}

QRectF ClipItem::geometry() const {
    const int64_t startFrame = toFrame(clip_.timelineStart, fps_);
    const int64_t durFrames = toFrame(clip_.effectiveDuration(), fps_);
    const qreal w = frameToX(std::max<int64_t>(durFrames, 1), pxPerFrame_);
    const qreal x = frameToX(startFrame, pxPerFrame_);
    if (laneRect_.isEmpty()) {
        return QRectF(x, 0, w, 0);
    }
    return QRectF(x, laneRect_.top(), w, laneRect_.height());
}

ClipItem::Edge ClipItem::edgeAt(const QPointF& scenePos) const {
    const QRectF r = hasPreview() ? *preview_ : QGraphicsRectItem::rect();
    const qreal x = scenePos.x();
    if (x - r.left() <= handleWidth()) return Edge::Left;
    if (r.right() - x <= handleWidth()) return Edge::Right;
    return Edge::None;
}

void ClipItem::setZoom(qreal pxPerFrame) {
    pxPerFrame_ = pxPerFrame;
    setRect(geometry());
    update();
}

void ClipItem::setLaneRect(const QRectF& laneRect) {
    laneRect_ = laneRect;
    setRect(geometry());
}

void ClipItem::setPreviewRect(const QRectF& rect) {
    preview_ = rect;
    setRect(rect);
    update();
}

void ClipItem::clearPreview() {
    preview_.reset();
    setRect(geometry());
    update();
}

void ClipItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF r = rect();
    if (r.width() < 1.0 || r.height() < 1.0) return;

    const bool selected = isSelected();
    painter->setPen(QPen(QColor(0x1a, 0x1c, 0x1e), 1.0));
    painter->setBrush(labelColor(clip_.colorLabel, selected));
    painter->drawRect(r);

    QFont f = painter->font();
    f.setPixelSize(10);
    painter->setFont(f);
    const QRectF textRect = r.adjusted(4, 0, -4, 0);
    painter->setClipRect(textRect);
    painter->setPen(QColor(0xf0, 0xf0, 0xf0));
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QString::fromStdString(clip_.name));
    painter->setClipping(false);

    if (selected) {
        painter->setPen(QPen(QColor(0xff, 0xd0, 0x5a), 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(r.adjusted(1.0, 1.0, -1.0, -1.0));
    }

    // Trim-handle zones (visual darkening at each edge).
    const qreal h = handleWidth();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 70));
    painter->drawRect(QRectF(r.left(), r.top(), h, r.height()));
    painter->drawRect(QRectF(r.right() - h, r.top(), h, r.height()));

    // Transition hatch on the out edge.
    if (clip_.transitionOut) {
        const int64_t frames = toFrame(clip_.transitionOut->duration, fps_);
        const qreal w =
            std::min(frameToX(std::max<int64_t>(frames, 1), pxPerFrame_), 28.0);
        painter->setBrush(QColor(0xd0, 0x60, 0x60, 0xa0));
        painter->drawRect(QRectF(r.right() - w, r.top(), w, r.height()));
    }

    // Subtitle badge.
    if (clip_.subtitleText) {
        painter->setBrush(QColor(0xf5, 0xf5, 0xf5));
        painter->setPen(Qt::NoPen);
        const QRectF badge(r.right() - 16.0, r.top() + 2.0, 12.0, 12.0);
        painter->drawRect(badge);
        painter->setPen(QColor(0x22, 0x22, 0x22));
        painter->drawText(badge, Qt::AlignCenter, QStringLiteral("S"));
    }
}

// ---------------------------------------------------------------------------
// timeline_geometry
// ---------------------------------------------------------------------------
namespace timeline_geometry {

qreal laneTop(const bl::Sequence& seq, int flat, int videoCount) {
    static_cast<void>(seq);
    qreal y = kRulerHeight;
    for (int i = 0; i < flat; ++i) {
        y += i < videoCount ? kVideoLaneHeight : kAudioLaneHeight;
    }
    return y;
}

qreal laneHeight(const bl::Sequence& seq, int flat, int videoCount) {
    static_cast<void>(seq);
    return flat < videoCount ? kVideoLaneHeight : kAudioLaneHeight;
}

qreal contentWidth(const bl::Sequence& seq, qreal pxPerFrame) {
    if (pxPerFrame <= 0.0) return 0.0;
    const bl::Rational& fps = seq.settings.fps;
    int64_t endFrame = 720; // ~30s at 24fps minimum footprint
    const auto scan = [&](const auto& tracks) {
        for (const auto& t : tracks) {
            for (const auto& c : t.clips()) {
                const int64_t f =
                    toFrame(c.timelineStart + c.effectiveDuration(), fps);
                endFrame = std::max(endFrame, f);
            }
        }
    };
    scan(seq.videoTracks);
    scan(seq.audioTracks);
    return frameToX(endFrame, pxPerFrame) + kSnapBarWidth;
}

qreal contentHeight(const bl::Sequence& seq) {
    qreal h = kRulerHeight;
    h += kVideoLaneHeight * static_cast<qreal>(seq.videoTracks.size());
    h += kAudioLaneHeight * static_cast<qreal>(seq.audioTracks.size());
    return h;
}

} // namespace timeline_geometry

} // namespace bl::ui