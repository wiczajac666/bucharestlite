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

constexpr qreal kSnapBarWidth = timeline_geometry::kContentMargin;

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
// ZoneItem
// ---------------------------------------------------------------------------
ZoneItem::ZoneItem() : QGraphicsRectItem() { setZValue(1); }

void ZoneItem::setRange(qreal x, qreal width, qreal top, qreal height) {
    setRect(x, top, std::max<qreal>(width, 1.0), height);
}

void ZoneItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF r = rect();
    QColor fill(0x4d, 0xa3, 0xf0);
    fill.setAlpha(40);
    painter->fillRect(r, fill);

    // In/out grab handles.
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0x4d, 0xa3, 0xf0, 220));
    painter->drawRect(QRectF(r.left(), r.top(), 4.0, r.height()));
    painter->drawRect(QRectF(r.right() - 4.0, r.top(), 4.0, r.height()));

    // Boundaries.
    painter->setPen(QPen(QColor(0x4d, 0xa3, 0xf0), 1.0, Qt::DashLine));
    painter->drawLine(QPointF(r.left(), r.top()), QPointF(r.left(), r.bottom()));
    painter->drawLine(QPointF(r.right(), r.top()), QPointF(r.right(), r.bottom()));
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

    // Audio waveform (P1 #13): reduce the analyzed envelope to one bar per
    // pixel column over the clip's source range and draw it inside the body,
    // before the label so the clip name stays readable on top.
    if (waveform_ && !waveform_->empty() && clip_.hasAudio) {
        const qreal innerW = r.width() - 2.0;
        if (innerW >= 2.0) {
            const int columns = std::max(
                1, static_cast<int>(
                       std::min<qreal>(std::floor(innerW), 4096.0)));
            const std::vector<float> amps =
                waveformColumns(*waveform_, clip_.source.sourceIn.toSeconds(),
                                clip_.source.sourceOut.toSeconds(), columns);
            const qreal colW = innerW / static_cast<double>(columns);
            const qreal midY = r.center().y();
            const qreal maxAmp = std::max<qreal>(1.0, r.height() * 0.5 - 4.0);
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0xf0, 0xf2, 0xf4, 140));
            for (int c = 0; c < columns; ++c) {
                const qreal amp =
                    std::min<qreal>(amps[static_cast<size_t>(c)], 1.0);
                if (amp <= 0.0) continue;
                const qreal half = amp * maxAmp;
                const qreal x = r.left() + 1.0 + colW * c;
                painter->drawRect(QRectF(x, midY - half,
                                         std::max<qreal>(colW, 1.0),
                                         half * 2.0));
            }
            painter->restore();
        }
    }

    // Video filmstrip (Phase 3): one sampled thumbnail per cell across the
    // clip's source window. Cells whose decode has not settled yet draw a
    // neutral placeholder; the name is painted on top so it stays readable.
    if (thumbnail_ && clip_.hasVideo && !thumbnail_->frames.empty()) {
        const qreal innerLeft = r.left() + 2.0;
        const qreal innerTop = r.top() + 2.0;
        const qreal innerW = r.width() - 4.0;
        const qreal innerH = r.height() - 4.0;
        if (innerW >= 2.0 && innerH >= 2.0) {
            const size_t cells = thumbnail_->frames.size();
            const qreal cellW = innerW / static_cast<qreal>(cells);
            painter->save();
            for (size_t i = 0; i < cells; ++i) {
                const QRectF cell(innerLeft + cellW * static_cast<qreal>(i),
                                  innerTop, cellW, innerH);
                const auto& thumb = thumbnail_->frames[i];
                if (thumb && thumb->hasPixels()) {
                    // MediaThumbnail pixels are BGRA32; on little-endian hosts
                    // QImage::Format_ARGB32 has the same in-memory B,G,R,A
                    // order (the old RGBA8888 mapping swapped red and blue).
                    const QImage image(
                        reinterpret_cast<const uchar*>(thumb->pixels.data()),
                        static_cast<int>(thumb->width),
                        static_cast<int>(thumb->height),
                        static_cast<int>(thumb->linesize),
                        QImage::Format_ARGB32);
                    if (!image.isNull()) {
                        const qreal aspect = static_cast<qreal>(thumb->width) /
                                             static_cast<qreal>(thumb->height);
                        qreal dw = cell.width();
                        qreal dh = dw / aspect;
                        if (dh > cell.height()) {
                            dh = cell.height();
                            dw = dh * aspect;
                        }
                        const QRectF target(
                            cell.left() + (cell.width() - dw) * 0.5,
                            cell.top() + (cell.height() - dh) * 0.5, dw, dh);
                        painter->save();
                        painter->setClipRect(cell);
                        painter->drawImage(target, image);
                        painter->restore();
                        continue;
                    }
                }
                // Graceful fallback while the cell's frame is decoding (or a
                // frame could not be produced): a subtle placeholder keeps the
                // strip grid legible without ever blocking the GUI thread.
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0x2a, 0x2e, 0x32, 170));
                painter->drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
            }
            painter->restore();
        }
    }

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
        // Highlight overlay so the selection is visible at a glance, in
        // addition to the outline below.
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xff, 0xd0, 0x5a, 55));
        painter->drawRect(r);
        painter->restore();
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

    // Proxy status badge.
    // Shows proxy availability/override status at top-right of clip.
    const QString& pstatus = proxyStatus_;
    if (!pstatus.isEmpty() && pstatus != "none") {
        QColor badgeColor;
        QString badgeText;
        if (pstatus == "ready") {
            badgeColor = QColor(0x3f, 0xa9, 0x5a);  // Green
            badgeText = QStringLiteral("✓");
        } else if (pstatus == "generating") {
            badgeColor = QColor(0xd3, 0xa0, 0x51);  // Orange/amber
            badgeText = QStringLiteral("⟳");
        } else if (pstatus == "failed") {
            badgeColor = QColor(0xc0, 0x4a, 0x4a);  // Red
            badgeText = QStringLiteral("✕");
        } else if (pstatus == "force_proxy") {
            badgeColor = QColor(0x3f, 0xa9, 0x5a);  // Green
            badgeText = QStringLiteral("P");
        } else if (pstatus == "force_original") {
            badgeColor = QColor(0x6c, 0x8f, 0xd3);  // Blue
            badgeText = QStringLiteral("O");
        } else {
            badgeColor = QColor(0x88, 0x8d, 0x93);  // Gray
            badgeText = QStringLiteral("?");
        }

        const qreal badgeSize = 14.0;
        const qreal badgeX = r.right() - badgeSize - 2.0;
        const qreal badgeY = r.top() + 2.0;
        const QRectF badgeRect(badgeX, badgeY, badgeSize, badgeSize);

        painter->setBrush(badgeColor);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(badgeRect, 3.0, 3.0);
        painter->setPen(Qt::white);
        QFont badgeFont = painter->font();
        badgeFont.setPixelSize(10);
        badgeFont.setBold(true);
        painter->setFont(badgeFont);
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
    }
}

// ---------------------------------------------------------------------------
// timeline_geometry
// ---------------------------------------------------------------------------
namespace timeline_geometry {

std::vector<int> lanePermutation(int videoCount, int audioCount,
                                 LaneOrder order) {
    std::vector<int> perm;
    perm.reserve(static_cast<size_t>(videoCount + audioCount));
    switch (order) {
    case LaneOrder::Split:
        for (int v = 0; v < videoCount; ++v) perm.push_back(v);
        for (int a = 0; a < audioCount; ++a) perm.push_back(videoCount + a);
        break;
    case LaneOrder::Mixed:
        for (int i = 0; i < std::max(videoCount, audioCount); ++i) {
            if (i < videoCount) perm.push_back(i);
            if (i < audioCount) perm.push_back(videoCount + i);
        }
        break;
    case LaneOrder::MixedReverse:
        for (int i = 0; i < std::max(videoCount, audioCount); ++i) {
            if (i < audioCount) perm.push_back(videoCount + i);
            if (i < videoCount) perm.push_back(i);
        }
        break;
    }
    return perm;
}

int laneForFlat(const bl::Sequence& seq, int flat, LaneOrder order) {
    const auto perm = lanePermutation(
        static_cast<int>(seq.videoTracks.size()),
        static_cast<int>(seq.audioTracks.size()), order);
    for (size_t lane = 0; lane < perm.size(); ++lane) {
        if (perm[lane] == flat) return static_cast<int>(lane);
    }
    return -1;
}

int flatForLane(const bl::Sequence& seq, int lane, LaneOrder order) {
    const auto perm = lanePermutation(
        static_cast<int>(seq.videoTracks.size()),
        static_cast<int>(seq.audioTracks.size()), order);
    if (lane < 0 || static_cast<size_t>(lane) >= perm.size()) return -1;
    return perm[lane];
}

qreal laneHeight(const bl::Sequence& seq, int flat, int videoCount) {
    // Use the track's custom height when the user has set one, otherwise fall
    // back to the per-kind defaults (the model's generic 60 is not a UI size).
    qreal h = 0.0;
    if (flat < videoCount) {
        if (static_cast<size_t>(flat) < seq.videoTracks.size()) {
            const auto& track = seq.videoTracks[static_cast<size_t>(flat)];
            h = track.hasCustomHeight() ? static_cast<qreal>(track.height()) : 0.0;
        }
        if (h <= 0.0) h = kDefaultVideoLaneHeight;
    } else {
        const int ai = flat - videoCount;
        if (static_cast<size_t>(ai) < seq.audioTracks.size()) {
            const auto& track = seq.audioTracks[static_cast<size_t>(ai)];
            h = track.hasCustomHeight() ? static_cast<qreal>(track.height()) : 0.0;
        }
        if (h <= 0.0) h = kDefaultAudioLaneHeight;
    }
    // Clamp to valid range.
    return std::clamp(h, kMinLaneHeight, kMaxLaneHeight);
}

qreal laneTop(const bl::Sequence& seq, int flat, int videoCount, LaneOrder order) {
    const auto perm = lanePermutation(
        videoCount, static_cast<int>(seq.audioTracks.size()), order);
    qreal y = kRulerHeight;
    for (size_t lane = 0; lane < perm.size(); ++lane) {
        if (perm[lane] == flat) return y;
        y += laneHeight(seq, perm[lane], videoCount);
    }
    return y;
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
    const int vc = static_cast<int>(seq.videoTracks.size());
    for (size_t i = 0; i < seq.videoTracks.size(); ++i) {
        h += laneHeight(seq, static_cast<int>(i), vc);
    }
    for (size_t i = 0; i < seq.audioTracks.size(); ++i) {
        h += laneHeight(seq, vc + static_cast<int>(i), vc);
    }
    return h;
}

} // namespace timeline_geometry

} // namespace bl::ui