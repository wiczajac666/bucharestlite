#include "panels/timeline_panel.hpp"

#include "app/project_controller.hpp"
#include "panels/timeline_edit_controller.hpp"
#include "panels/timeline_items.hpp"

#include <QCheckBox>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace bl::ui {

namespace {

constexpr qreal kMinZoom = 0.25;
constexpr qreal kMaxZoom = 64.0;
constexpr qreal kDefaultZoom = 6.0;
constexpr qreal kSnapTolerancePx = 6.0;
constexpr qreal kDragStartTolerancePx = 4.0;
const bl::Rational kTimelineRate{1'000'000, 1};

const QColor kBackground{0x20, 0x22, 0x25};
const QColor kRulerFill{0x2a, 0x2d, 0x30};
const QColor kHeaderFill{0x26, 0x28, 0x2b};
const QColor kBorder{0x3a, 0x3d, 0x40};
const QColor kText{0xc8, 0xcc, 0xd1};

int64_t toFrame(const bl::Time& t, const bl::Rational& fps) {
    return static_cast<int64_t>(
        std::llround(t.toSeconds() * static_cast<double>(fps.num) /
                     static_cast<double>(fps.den)));
}

bl::Time fromFrames(int64_t frame, const bl::Rational& fps) {
    return bl::Time::fromFrameAt(std::max<int64_t>(0, frame), fps, kTimelineRate);
}

int64_t snapToleranceFrames(qreal pxPerFrame) {
    const qreal frames = kSnapTolerancePx / std::max(pxPerFrame, 0.001);
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(frames)));
}

} // namespace

// ---------------------------------------------------------------------------
// TrackHeader
// ---------------------------------------------------------------------------
TrackHeader::TrackHeader(TimelinePanel* host, QWidget* parent)
    : QWidget(parent), host_(host) {
    setFixedWidth(144);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void TrackHeader::setScrollOffset(int offset) {
    if (offset_ == offset) return;
    offset_ = offset;
    update();
}

QSize TrackHeader::sizeHint() const { return {144, 320}; }

void TrackHeader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kHeaderFill);

    const bl::Sequence& seq = host_->sequence();
    const int vc = static_cast<int>(seq.videoTracks.size());
    const bl::Rational& fps = seq.settings.fps;
    static_cast<void>(fps);

    // Ruler band label.
    p.setPen(kText);
    p.fillRect(QRect(0, 0, width(), static_cast<int>(timeline_geometry::kRulerHeight)),
               kRulerFill);
    p.drawText(QRect(6, 0, width() - 8,
                     static_cast<int>(timeline_geometry::kRulerHeight)),
               QStringLiteral("Timeline"));

    int flat = 0;
    const auto drawRow = [&](const std::string& name, bool video, int index) {
        const qreal top =
            timeline_geometry::laneTop(seq, flat, vc) - static_cast<qreal>(offset_);
        const qreal h = timeline_geometry::laneHeight(seq, flat, vc);
        ++flat;
        if (top + h < 0 || top > rect().bottom()) return;

        const QRectF row(0, top, width(), h);
        p.fillRect(row, video ? QColor(0x2e, 0x3a, 0x46) : QColor(0x2c, 0x40, 0x35));
        p.setPen(kBorder);
        p.drawLine(QPointF(0, top), QPointF(width(), top));

        if (std::fabs(top) < 1e-6) { // visible top edge at ruler boundary
            p.drawLine(QPointF(0, top), QPointF(width(), top));
        }

        p.setPen(kText);
        const QRectF textRect = row.adjusted(6, 0, -4, 0);
        const QString label = name.empty()
                                  ? QString(video ? QStringLiteral("V%1")
                                                  : QStringLiteral("A%1")).arg(index + 1)
                                  : QString::fromStdString(name);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
    };

    for (size_t i = 0; i < seq.videoTracks.size(); ++i) {
        drawRow(seq.videoTracks[i].name(), true, static_cast<int>(i));
    }
    for (size_t i = 0; i < seq.audioTracks.size(); ++i) {
        drawRow(seq.audioTracks[i].name(), false, static_cast<int>(i));
    }
}

// ---------------------------------------------------------------------------
// TimelineView
// ---------------------------------------------------------------------------
TimelineView::TimelineView(TimelinePanel* host, QWidget* parent)
    : QGraphicsView(parent), host_(host) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::NoDrag);
    viewport()->setCursor(Qt::ArrowCursor);
}

ClipItem* TimelineView::clipItemAt(const QPointF& scenePos) const {
    const auto items = scene()->items(scenePos, Qt::IntersectsItemShape,
                                      Qt::DescendingOrder);
    for (QGraphicsItem* item : items) {
        auto* clip = dynamic_cast<ClipItem*>(item);
        if (clip && clip->isVisible()) return clip;
    }
    return nullptr;
}

void TimelineView::cancelActiveGesture() {
    clearSnapIndicator();
    if (marqueeItem_ && scene()) {
        scene()->removeItem(marqueeItem_);
        delete marqueeItem_;
        marqueeItem_ = nullptr;
    }
    pendingMoves_.clear();
    mode_ = Mode::None;
    dragging_ = false;
}

int64_t TimelineView::snapToleranceFrames() const {
    if (!host_ || !host_->controller()) return 1;
    return ::bl::ui::snapToleranceFrames(host_->zoom());
}

void TimelineView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    setFocus();
    pressViewportPos_ = event->pos();
    pressScenePos_ = mapToScene(event->pos());
    pressTime_ = host_->rawTimeAtX(pressScenePos_.x());
    pressLane_ = host_->flatAtY(pressScenePos_.y());
    pressClip_.clear();
    pendingMoves_.clear();
    dragging_ = false;
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Ruler scrub.
    if (pressScenePos_.y() <= timeline_geometry::kRulerHeight) {
        mode_ = Mode::Scrub;
        host_->setPlayhead(host_->timeAtX(pressScenePos_.x()));
        return;
    }

    ClipItem* item = clipItemAt(pressScenePos_);
    if (item) {
        pressClip_ = item->clip().id;
        pressLane_ = item->flatTrack();

        if (mods & (Qt::ControlModifier | Qt::ShiftModifier)) {
            QSet<bl::ClipId> sel = host_->selection();
            if (sel.contains(pressClip_)) {
                sel.remove(pressClip_);
            } else {
                sel.insert(pressClip_);
            }
            host_->setSelection(sel);
            mode_ = Mode::None;
            return;
        }

        if (!host_->selection().contains(pressClip_)) {
            host_->setSelection({pressClip_});
        }

        const ClipItem::Edge edge = item->edgeAt(pressScenePos_);
        if (edge == ClipItem::Edge::Left || edge == ClipItem::Edge::Right) {
            mode_ = edge == ClipItem::Edge::Left ? Mode::TrimLeft : Mode::TrimRight;
            trimRipple_ = (mods & Qt::ShiftModifier) != 0;
            trimFlat_ = item->flatTrack();
            trimAnchorStart_ = item->clip().timelineStart;
            trimAnchorEnd_ = item->clip().timelineStart + item->clip().timelineDuration;
            trimCurrent_ = trimAnchorStart_;
        } else {
            // Record the whole selection for a group move; offsets persist.
            for (const auto& entry : host_->clipItems_) {
                if (!host_->selection().contains(entry.first)) continue;
                PendingMove pm;
                pm.id = entry.second->clip().id;
                pm.fromTrack = entry.second->flatTrack();
                pm.toTrack = pm.fromTrack;
                pm.oldStart = entry.second->clip().timelineStart;
                pm.newStart = pm.oldStart;
                pendingMoves_.push_back(pm);
            }
            mode_ = Mode::Move;
        }
        return;
    }

    // Empty lane area: click = clear, drag = marquee (Ctrl/Shift preserves).
    if (!(mods)) {
        host_->clearSelection();
    }
    mode_ = Mode::Marquee;
    marqueeItem_ =
        scene()->addRect(QRectF(pressScenePos_, pressScenePos_),
                         QPen(QColor(0x4d, 0xa3, 0xf0), 1),
                         QBrush(QColor(0x4d, 0xa3, 0xf0, 40)));
    marqueeItem_->setZValue(120);
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
    if (mode_ == Mode::None) {
        updateHoverCursor(mapToScene(event->pos()));
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }

    if (!dragging_) {
        const QPoint delta = event->pos() - pressViewportPos_;
        if (delta.manhattanLength() < kDragStartTolerancePx) return;
        dragging_ = true;
    }

    const QPointF scenePos = mapToScene(event->pos());
    switch (mode_) {
    case Mode::Scrub:
        host_->setPlayhead(host_->timeAtX(scenePos.x()));
        break;
    case Mode::Move:
        updateMovePreview(scenePos);
        break;
    case Mode::TrimLeft:
        updateTrimPreview(scenePos, true);
        break;
    case Mode::TrimRight:
        updateTrimPreview(scenePos, false);
        break;
    case Mode::Marquee:
        if (marqueeItem_) {
            marqueeItem_->setRect(QRectF(pressScenePos_, scenePos).normalized());
        }
        break;
    default:
        break;
    }
}

void TimelineView::updateMovePreview(const QPointF& scenePos) {
    if (pendingMoves_.empty() || !host_->controller()) return;
    const bl::Sequence& seq = host_->sequence();

    PendingMove* anchor = nullptr;
    for (auto& pm : pendingMoves_) {
        if (pm.id == pressClip_) {
            anchor = &pm;
            break;
        }
    }
    if (!anchor) anchor = &pendingMoves_.front();

    const bl::Time raw = host_->rawTimeAtX(scenePos.x());
    const bl::Time snapped =
        host_->snapEnabled()
            ? host_->editor().snapToNearest(seq, raw, snapToleranceFrames(), pressClip_)
            : raw;
    const bl::Time anchorSnap =
        host_->snapEnabled()
            ? host_->editor().snapToNearest(seq, anchor->oldStart + (raw - pressTime_),
                                            snapToleranceFrames(), pressClip_)
            : anchor->oldStart + (raw - pressTime_);
    const bl::Time anchorFinal = anchorSnap < bl::Time{} ? bl::Time{} : anchorSnap;
    const bl::Duration groupOffset = anchorFinal - anchor->oldStart;

    for (auto& pm : pendingMoves_) {
        bl::Time ns = pm.oldStart + groupOffset;
        if (ns < bl::Time{}) ns = bl::Time{};
        pm.newStart = ns;
        ClipItem* item = host_->clipItemFor(pm.id);
        if (!item) continue;
        const QRectF lr = host_->laneRect(pm.fromTrack);
        const QRectF prev(host_->xForTime(ns), lr.top(), item->rect().width(),
                          lr.height());
        item->setPreviewRect(prev);
    }

    if (host_->snapIndicator_ && snapped != raw && host_->snapEnabled()) {
        host_->snapIndicator_->setPos(host_->xForTime(snapped), 0.0);
        host_->snapIndicator_->setActive(true);
    } else {
        clearSnapIndicator();
    }
}

void TimelineView::updateTrimPreview(const QPointF& scenePos, bool leftEdge) {
    ClipItem* item = host_->clipItemFor(pressClip_);
    if (!item || !host_->controller()) return;
    const bl::Sequence& seq = host_->sequence();

    const bl::Time raw = host_->rawTimeAtX(scenePos.x());
    const bl::Time snapped =
        host_->snapEnabled()
            ? host_->editor().snapToNearest(seq, raw, snapToleranceFrames(), pressClip_)
            : raw;
    trimCurrent_ = snapped;

    const QRectF lr = host_->laneRect(trimFlat_);
    if (leftEdge) {
        const qreal x = host_->xForTime(snapped);
        item->setPreviewRect(QRectF(x, lr.top(), host_->xForTime(trimAnchorEnd_) - x, lr.height()));
    } else {
        const qreal x = host_->xForTime(trimAnchorStart_);
        item->setPreviewRect(QRectF(x, lr.top(), host_->xForTime(snapped) - x, lr.height()));
    }

    if (host_->snapIndicator_ && snapped != raw && host_->snapEnabled()) {
        host_->snapIndicator_->setPos(host_->xForTime(snapped), 0.0);
        host_->snapIndicator_->setActive(true);
    } else {
        clearSnapIndicator();
    }
}

void TimelineView::commitMove() {
    if (!host_->controller()) return;
    const bool single = pendingMoves_.size() == 1;

    std::vector<TimelineEditController::MoveEntry> entries;
    for (auto& pm : pendingMoves_) {
        if (pm.id == pressClip_ && single) {
            const int target = host_->flatAtY(pressScenePos_.y());
            if (target >= 0 && target < host_->trackCount() &&
                target != pm.fromTrack) {
                pm.toTrack = target;
            }
        }
        TimelineEditController::MoveEntry e;
        e.id = pm.id;
        e.fromTrack = pm.fromTrack;
        e.toTrack = pm.toTrack;
        e.oldStart = pm.oldStart;
        e.newStart = pm.newStart;
        entries.push_back(e);
    }

    if (!host_->editor().groupMove(entries)) {
        host_->rebuildFromModel();
    }
    pendingMoves_.clear();
}

void TimelineView::commitTrim(bool leftEdge) {
    const bool ok =
        leftEdge ? host_->editor().trimLeft(trimFlat_, pressClip_, trimCurrent_, trimRipple_)
                 : host_->editor().trimRight(trimFlat_, pressClip_, trimCurrent_, trimRipple_);
    if (!ok) {
        host_->rebuildFromModel();
    }
}

void TimelineView::finishMarquee(const QPointF& scenePos, Qt::KeyboardModifiers mods) {
    if (marqueeItem_) {
        const QRectF sel = marqueeItem_->rect();
        scene()->removeItem(marqueeItem_);
        delete marqueeItem_;
        marqueeItem_ = nullptr;

        if (sel.width() >= kDragStartTolerancePx && sel.height() >= kDragStartTolerancePx) {
            QSet<bl::ClipId> selection;
            for (const auto& entry : host_->clipItems_) {
                if (entry.second->sceneBoundingRect().intersects(sel)) {
                    selection.insert(entry.first);
                }
            }
            if (mods & (Qt::ControlModifier | Qt::ShiftModifier)) {
                QSet<bl::ClipId> merged = host_->selection();
                for (const auto& id : selection) merged.insert(id);
                host_->setSelection(merged);
            } else {
                host_->setSelection(selection);
            }
        }
    }
    static_cast<void>(scenePos);
}

void TimelineView::updateHoverCursor(const QPointF& scenePos) {
    ClipItem* item = clipItemAt(scenePos);
    viewport()->setCursor(item && item->edgeAt(scenePos) != ClipItem::Edge::None
                              ? Qt::SizeHorCursor
                              : Qt::ArrowCursor);
}

void TimelineView::clearSnapIndicator() {
    if (host_->snapIndicator_) {
        host_->snapIndicator_->setActive(false);
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    const QPointF scenePos = mapToScene(event->pos());
    switch (mode_) {
    case Mode::Scrub:
        if (dragging_) host_->setPlayhead(host_->timeAtX(scenePos.x()));
        break;
    case Mode::Move:
        if (dragging_) {
            pressScenePos_.setY(scenePos.y());
            commitMove();
        }
        break;
    case Mode::TrimLeft:
        if (dragging_) commitTrim(true);
        break;
    case Mode::TrimRight:
        if (dragging_) commitTrim(false);
        break;
    case Mode::Marquee:
        finishMarquee(scenePos, event->modifiers());
        break;
    default:
        break;
    }

    clearSnapIndicator();
    mode_ = Mode::None;
    dragging_ = false;
    updateHoverCursor(scenePos);
}

void TimelineView::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Delete:
        performDelete(false);
        event->accept();
        return;
    case Qt::Key_Backspace:
        performDelete(true);
        event->accept();
        return;
    case Qt::Key_S:
        if (!(event->modifiers() & Qt::ControlModifier)) {
            performSplit();
            event->accept();
            return;
        }
        break;
    case Qt::Key_A:
        if (event->modifiers() & Qt::ControlModifier) {
            performSelectAll();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

void TimelineView::performDelete(bool ripple) {
    struct Ref {
        bl::ClipId id;
        int flat;
    };
    std::vector<Ref> refs;
    for (const auto& entry : host_->clipItems_) {
        if (host_->selection().contains(entry.first)) {
            refs.push_back({entry.first, entry.second->flatTrack()});
        }
    }
    if (refs.empty()) return;
    for (const auto& ref : refs) {
        host_->editor().removeClip(ref.flat, ref.id, ripple);
    }
    host_->clearSelection();
}

void TimelineView::performSplit() {
    const bl::Time at = host_->playhead();
    struct Ref {
        bl::ClipId id;
        int flat;
    };
    std::vector<Ref> refs;
    for (const auto& entry : host_->clipItems_) {
        if (!host_->selection().isEmpty() &&
            !host_->selection().contains(entry.first)) {
            continue;
        }
        const bl::Clip& c = entry.second->clip();
        if (c.timelineStart < at && c.timelineStart + c.effectiveDuration() > at) {
            refs.push_back({entry.first, entry.second->flatTrack()});
        }
    }
    for (const auto& ref : refs) {
        host_->editor().splitClip(ref.flat, ref.id, at);
    }
}

void TimelineView::performSelectAll() {
    QSet<bl::ClipId> all;
    for (const auto& entry : host_->clipItems_) {
        all.insert(entry.first);
    }
    host_->setSelection(all);
}

void TimelineView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const qreal factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
        const qreal target = host_->zoom() * factor;
        const qreal clamped = std::clamp(target, kMinZoom, kMaxZoom);
        if (clamped != host_->zoom()) {
            host_->setZoom(clamped);
        }
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

// ---------------------------------------------------------------------------
// TimelinePanel
// ---------------------------------------------------------------------------
TimelinePanel::TimelinePanel(ProjectController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setObjectName(QStringLiteral("TimelinePanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    scene_ = new QGraphicsScene(this);
    scene_->setBackgroundBrush(kBackground);

    view_ = new TimelineView(this, this);
    view_->setScene(scene_);

    header_ = new TrackHeader(this, this);

    auto* topBar = new QHBoxLayout;
    topBar->setContentsMargins(2, 2, 2, 2);
    topBar->setSpacing(4);

    snapCheck_ = new QCheckBox(tr("Snap"), this);
    snapCheck_->setChecked(true);
    connect(snapCheck_, &QCheckBox::toggled, this, &TimelinePanel::setSnapEnabled);

    zoomOut_ = new QToolButton(this);
    zoomOut_->setText(QStringLiteral("-"));
    zoomOut_->setToolTip(tr("Zoom out"));
    zoomIn_ = new QToolButton(this);
    zoomIn_->setText(QStringLiteral("+"));
    zoomIn_->setToolTip(tr("Zoom in"));
    zoomLabel_ = new QLabel(this);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setFixedWidth(56);

    connect(zoomIn_, &QToolButton::clicked, this, [this] { setZoom(zoom() * 1.2); });
    connect(zoomOut_, &QToolButton::clicked, this, [this] { setZoom(zoom() / 1.2); });

    topBar->addWidget(snapCheck_);
    topBar->addStretch(1);
    topBar->addWidget(zoomOut_);
    topBar->addWidget(zoomLabel_);
    topBar->addWidget(zoomIn_);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(header_);
    body->addWidget(view_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(topBar);
    layout->addLayout(body, 1);

    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged,
            header_, &TrackHeader::setScrollOffset);

    if (controller_) {
        editor_.reset(controller_->timeline(), controller_->undoStack());
        connect(controller_, &ProjectController::projectChanged,
                this, &TimelinePanel::onDocumentChanged);
        connect(controller_, &ProjectController::undoChanged,
                this, &TimelinePanel::onDocumentChanged);
    }

    updateZoomLabel();
    onDocumentChanged();
}

void TimelinePanel::onDocumentChanged() {
    if (!controller_ || !editor_.valid()) return;
    editor_.reset(controller_->timeline(), controller_->undoStack());
    rebuildFromModel();
    emit timelineChanged();
}

const bl::Sequence& TimelinePanel::sequence() const {
    return controller_->timeline().sequence();
}

qreal TimelinePanel::xForTime(const bl::Time& t) const {
    const bl::Rational& fps = sequence().settings.fps;
    return static_cast<qreal>(toFrame(t, fps)) * pxPerFrame_;
}

bl::Time TimelinePanel::rawTimeAtX(qreal sceneX) const {
    if (pxPerFrame_ <= 0.0) return bl::Time{};
    const qreal frameF = sceneX / pxPerFrame_;
    const int64_t frame = static_cast<int64_t>(std::llround(frameF));
    return fromFrames(frame, sequence().settings.fps);
}

bl::Time TimelinePanel::timeAtX(qreal sceneX, const bl::ClipId& dragging) const {
    const bl::Time raw = rawTimeAtX(sceneX);
    if (!snap_) return raw;
    return editor_.snapToNearest(sequence(), raw,
                                 snapToleranceFrames(pxPerFrame_),
                                 dragging);
}

int TimelinePanel::flatAtY(qreal sceneY) const {
    const bl::Sequence& seq = sequence();
    if (sceneY <= timeline_geometry::kRulerHeight) return -1;
    const int vc = static_cast<int>(seq.videoTracks.size());
    const int tc = trackCount();
    for (int flat = 0; flat < tc; ++flat) {
        const qreal top = timeline_geometry::laneTop(seq, flat, vc);
        const qreal h = timeline_geometry::laneHeight(seq, flat, vc);
        if (sceneY >= top && sceneY < top + h) return flat;
    }
    return -1;
}

QRectF TimelinePanel::laneRect(int flat) const {
    const bl::Sequence& seq = sequence();
    const int vc = static_cast<int>(seq.videoTracks.size());
    const qreal top = timeline_geometry::laneTop(seq, flat, vc);
    const qreal h = timeline_geometry::laneHeight(seq, flat, vc);
    qreal w = timeline_geometry::contentWidth(seq, pxPerFrame_);
    if (w <= 0) w = 800;
    return QRectF(0, top, w, h);
}

int TimelinePanel::videoCount() const {
    return static_cast<int>(sequence().videoTracks.size());
}

int TimelinePanel::trackCount() const {
    return videoCount() + static_cast<int>(sequence().audioTracks.size());
}

void TimelinePanel::setZoom(qreal pxPerFrame) {
    const qreal clamped = std::clamp(pxPerFrame, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(clamped, pxPerFrame_)) return;
    pxPerFrame_ = clamped;
    updateZoomLabel();
    rebuildFromModel();
}

void TimelinePanel::updateZoomLabel() {
    if (zoomLabel_) {
        zoomLabel_->setText(QString::fromLatin1("%1px").arg(pxPerFrame_, 0, 'f', 1));
    }
}

void TimelinePanel::setPlayhead(const bl::Time& t) {
    if (t == playhead_) return;
    playhead_ = t;
    updatePlayheadItem();
    emit playheadChanged(playhead_);
}

void TimelinePanel::setSnapEnabled(bool on) {
    if (snap_ == on) return;
    snap_ = on;
    if (snapCheck_) snapCheck_->setChecked(on);
}

void TimelinePanel::setSelection(const QSet<bl::ClipId>& ids) {
    if (selection_ == ids) return;
    selection_ = ids;
    for (auto& entry : clipItems_) {
        entry.second->setSelected(selection_.contains(entry.first));
    }
    emit selectionChanged();
}

void TimelinePanel::clearSelection() { setSelection({}); }

int TimelinePanel::clipItemCount() const { return static_cast<int>(clipItems_.size()); }

ClipItem* TimelinePanel::clipItemFor(const bl::ClipId& id) const {
    const auto it = clipItems_.find(id);
    return it == clipItems_.end() ? nullptr : it->second;
}

int TimelinePanel::markerItemCount() const {
    int count = 0;
    const auto items = scene_->items();
    for (const QGraphicsItem* item : items) {
        if (dynamic_cast<const MarkerItem*>(item) != nullptr) ++count;
    }
    return count;
}

void TimelinePanel::rebuildFromModel() {
    if (!scene_) return;
    if (view_) view_->cancelActiveGesture();
    scene_->clear();
    clipItems_.clear();
    playheadItem_ = nullptr;
    snapIndicator_ = nullptr;

    const bl::Sequence& seq = sequence();
    const qreal width = timeline_geometry::contentWidth(seq, pxPerFrame_);
    const qreal height = timeline_geometry::contentHeight(seq);
    scene_->setSceneRect(0, 0, width, height);

    auto* ruler = new RulerItem(timeline_geometry::kRulerHeight);
    ruler->configure(seq.settings);
    ruler->setWidth(width);
    ruler->setZoom(pxPerFrame_);
    scene_->addItem(ruler);

    int flat = 0;
    const auto buildLane = [&](const auto& tracks, bool video) {
        for (size_t i = 0; i < tracks.size(); ++i, ++flat) {
            const auto& track = tracks[i];
            const QRectF lr = laneRect(flat);
            scene_->addItem(new LaneItem(lr.top(), lr.height(), width, video));
            for (const auto& clip : track.clips()) {
                auto* clipItem = new ClipItem(clip, flat, pxPerFrame_, seq.settings.fps);
                clipItem->setLaneRect(lr);
                clipItem->setData(kClipItemIdRole, QString::fromStdString(clip.id));
                clipItem->setData(kClipItemTrackRole, flat);
                clipItem->setSelected(selection_.contains(clip.id));
                scene_->addItem(clipItem);
                clipItems_[clip.id] = clipItem;
            }
        }
    };
    buildLane(seq.videoTracks, true);
    buildLane(seq.audioTracks, false);

    for (const auto& marker : seq.markers) {
        scene_->addItem(new MarkerItem(marker, xForTime(marker.position), 0, height));
    }

    playheadItem_ = new PlayheadItem;
    playheadItem_->setHeight(height);
    playheadItem_->setPos(xForTime(playhead_), 0);
    scene_->addItem(playheadItem_);

    snapIndicator_ = new SnapIndicatorItem;
    snapIndicator_->setHeight(height);
    scene_->addItem(snapIndicator_);
}

void TimelinePanel::updatePlayheadItem() {
    if (playheadItem_) {
        playheadItem_->setPos(xForTime(playhead_), 0);
    }
}

} // namespace bl::ui