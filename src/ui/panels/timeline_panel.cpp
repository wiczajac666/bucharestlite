#include "panels/timeline_panel.hpp"

#include "app/project_controller.hpp"
#include "panels/media_bin_panel.hpp"
#include "panels/timeline_edit_controller.hpp"
#include "panels/timeline_items.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
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
// How close (px) the pointer must be to a zone edge on the ruler to grab it.
constexpr qreal kZoneHandleGrabPx = 6.0;
const bl::Rational kTimelineRate{1'000'000, 1};

// The zoom slider spans [kMinZoom, kMaxZoom] logarithmically so equal slider
// steps give a constant zoom factor.
constexpr int kZoomSliderSteps = 1000;

int zoomToSlider(qreal pxPerFrame) {
    const qreal lo = std::log(kMinZoom);
    const qreal hi = std::log(kMaxZoom);
    const qreal z = std::clamp(pxPerFrame, kMinZoom, kMaxZoom);
    const qreal t = (std::log(z) - lo) / (hi - lo);
    return static_cast<int>(std::lround(t * kZoomSliderSteps));
}

qreal sliderToZoom(int value) {
    const qreal lo = std::log(kMinZoom);
    const qreal hi = std::log(kMaxZoom);
    const qreal t = std::clamp(value, 0, kZoomSliderSteps) /
                    static_cast<qreal>(kZoomSliderSteps);
    return std::exp(lo + t * (hi - lo));
}

const QColor kBackground{0x20, 0x22, 0x25};
const QColor kRulerFill{0x2a, 0x2d, 0x30};
const QColor kHeaderFill{0x26, 0x28, 0x2b};
const QColor kHeaderRowVideo{0x2e, 0x3a, 0x46};
const QColor kHeaderRowVideoActive{0x3c, 0x4e, 0x60};
const QColor kHeaderRowAudio{0x2c, 0x40, 0x35};
const QColor kHeaderRowAudioActive{0x38, 0x54, 0x46};
const QColor kBorder{0x3a, 0x3d, 0x40};
const QColor kText{0xc8, 0xcc, 0xd1};
const QColor kDropGhostValid{0x3f, 0xa9, 0x5a};
const QColor kDropGhostInvalid{0xc0, 0x4a, 0x4a};
const char kDropGhostObjectName[] = "timelineDropGhost";

// Width of the fixed left-hand track header column.
constexpr int kHeaderWidth = 176;

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
// TrackHeaderRow
// ---------------------------------------------------------------------------
TrackHeaderRow::TrackHeaderRow(TimelinePanel* host, int flat, QWidget* parent)
    : QWidget(parent), host_(host), flat_(flat) {
    setObjectName(QStringLiteral("trackHeaderRow"));
    setProperty("flatTrack", flat);

    const auto makeToggle = [this](const QString& text, const QString& name,
                                   const QString& tip) {
        auto* b = new QToolButton(this);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(22, 20);
        return b;
    };

    nameLabel_ = new QLabel(this);
    nameLabel_->setObjectName(QStringLiteral("trackNameLabel"));
    nameLabel_->setToolTip(tr("Double-click to rename"));
    nameLabel_->setTextInteractionFlags(Qt::NoTextInteraction);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("trackNameEdit"));
    nameEdit_->setVisible(false);
    nameEdit_->installEventFilter(this);

    lockBtn_ = makeToggle(QStringLiteral("L"), QStringLiteral("trackLockButton"),
                          tr("Lock track"));
    avBtn_ = makeToggle(QStringLiteral("H"), QStringLiteral("trackAvButton"),
                        tr("Hide track"));
    soloBtn_ = makeToggle(QStringLiteral("S"), QStringLiteral("trackSoloButton"),
                          tr("Solo track"));
    menuBtn_ = makeToggle(QString::fromUtf8("\u2261"),
                          QStringLiteral("trackMenuButton"), tr("Track menu"));
    menuBtn_->setCheckable(false);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 0, 4, 0);
    lay->setSpacing(2);
    lay->addWidget(nameLabel_, 1);
    lay->addWidget(nameEdit_, 1);
    lay->addWidget(lockBtn_);
    lay->addWidget(avBtn_);
    lay->addWidget(soloBtn_);
    lay->addWidget(menuBtn_);

    connect(lockBtn_, &QToolButton::toggled, this, [this](bool on) {
        if (!host_->setTrackLocked(flat_, on)) refresh();
    });
    connect(avBtn_, &QToolButton::toggled, this, [this](bool on) {
        const bool ok = video_ ? host_->setTrackHidden(flat_, on)
                               : host_->setTrackMuted(flat_, on);
        if (!ok) refresh();
    });
    connect(soloBtn_, &QToolButton::toggled, this, [this](bool on) {
        if (!host_->setTrackSoloed(flat_, on)) refresh();
    });
    connect(menuBtn_, &QToolButton::clicked, this, [this] {
        openMenu(menuBtn_->mapToGlobal(QPoint(0, menuBtn_->height())));
    });

    refresh();
}

void TrackHeaderRow::refresh() {
    if (!host_) return;
    const bl::Sequence& seq = host_->sequence();
    video_ = host_->editor().isVideoTrack(seq, flat_);

    QString name;
    bool locked = false;
    bool toggled = false;  // hidden (video) / muted (audio)
    bool soloed = false;
    QString fallback;
    if (video_) {
        const auto& t = seq.videoTracks[static_cast<size_t>(flat_)];
        name = QString::fromStdString(t.name());
        locked = t.locked();
        toggled = t.hidden();
        soloed = t.soloed();
        fallback = QStringLiteral("V%1").arg(flat_ + 1);
    } else {
        const int ai = flat_ - static_cast<int>(seq.videoTracks.size());
        const auto& t = seq.audioTracks[static_cast<size_t>(ai)];
        name = QString::fromStdString(t.name());
        locked = t.locked();
        toggled = t.muted();
        soloed = t.soloed();
        fallback = QStringLiteral("A%1").arg(ai + 1);
    }

    nameLabel_->setText(name.isEmpty() ? fallback : name);
    {
        const QSignalBlocker b1(lockBtn_);
        lockBtn_->setChecked(locked);
        const QSignalBlocker b2(avBtn_);
        avBtn_->setChecked(toggled);
        const QSignalBlocker b3(soloBtn_);
        soloBtn_->setChecked(soloed);
    }
    soloBtn_->setVisible(!video_);
    avBtn_->setText(video_ ? QStringLiteral("H") : QStringLiteral("M"));
    avBtn_->setToolTip(video_ ? tr("Hide track") : tr("Mute track"));
    active_ = host_->activeTrack() == flat_;
    update();
}

void TrackHeaderRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QColor fill = video_ ? kHeaderRowVideo : kHeaderRowAudio;
    if (active_) fill = video_ ? kHeaderRowVideoActive : kHeaderRowAudioActive;
    p.fillRect(rect(), fill);
    p.setPen(kBorder);
    p.drawLine(0, height() - 1, width(), height() - 1);
    if (active_) {
        p.setPen(QColor(0x4d, 0xa3, 0xf0));
        p.drawLine(0, 0, 0, height() - 1);
    }
}

void TrackHeaderRow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && host_) {
        host_->setActiveTrack(flat_);
        pressGlobal_ = event->globalPosition().toPoint();
        dragArmed_ = !renameActive_;
        dragActive_ = false;
    }
    QWidget::mousePressEvent(event);
}

void TrackHeaderRow::mouseMoveEvent(QMouseEvent* event) {
    if (!dragArmed_ || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (!dragActive_) {
        const QPoint delta = event->globalPosition().toPoint() - pressGlobal_;
        if (delta.manhattanLength() < 6) return;
        dragActive_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    event->accept();
}

void TrackHeaderRow::mouseReleaseEvent(QMouseEvent* event) {
    if (dragActive_ && event->button() == Qt::LeftButton && host_) {
        setCursor(Qt::ArrowCursor);
        const int target = host_->flatAtGlobalY(event->globalPosition().toPoint().y());
        if (target >= 0 && target != flat_ &&
            host_->editor().isVideoTrack(host_->sequence(), target) == video_) {
            host_->moveTrackTo(flat_, target);
        }
    }
    dragArmed_ = false;
    dragActive_ = false;
    QWidget::mouseReleaseEvent(event);
}

void TrackHeaderRow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        beginRename();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TrackHeaderRow::contextMenuEvent(QContextMenuEvent* event) {
    openMenu(event->globalPos());
    event->accept();
}

bool TrackHeaderRow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == nameEdit_) {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                cancelRename();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                commitRename();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            if (renameActive_) commitRename();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TrackHeaderRow::beginRename() {
    if (!host_ || renameActive_) return;
    const bl::Sequence& seq = host_->sequence();
    const std::string raw =
        host_->editor().isVideoTrack(seq, flat_)
            ? seq.videoTracks[static_cast<size_t>(flat_)].name()
            : seq.audioTracks[static_cast<size_t>(
                  flat_ - static_cast<int>(seq.videoTracks.size()))]
                  .name();
    renameActive_ = true;
    nameEdit_->setText(QString::fromStdString(raw));
    nameEdit_->setVisible(true);
    nameLabel_->setVisible(false);
    nameEdit_->selectAll();
    nameEdit_->setFocus(Qt::OtherFocusReason);
}

void TrackHeaderRow::commitRename() {
    if (!renameActive_) return;
    renameActive_ = false;
    const QString text = nameEdit_->text();
    nameEdit_->setVisible(false);
    nameLabel_->setVisible(true);
    if (host_) host_->renameTrack(flat_, text);
    refresh();  // no-op rename path emits no undoChanged
}

void TrackHeaderRow::cancelRename() {
    renameActive_ = false;
    nameEdit_->setVisible(false);
    nameLabel_->setVisible(true);
}

void TrackHeaderRow::openMenu(const QPoint& globalPos) {
    if (!host_) return;
    const bl::Sequence& seq = host_->sequence();
    const bool video = host_->editor().isVideoTrack(seq, flat_);
    const int ki = host_->editor().kindIndex(seq, flat_);
    const int kindCount = video ? static_cast<int>(seq.videoTracks.size())
                                : static_cast<int>(seq.audioTracks.size());
    const bool locked = video
                            ? seq.videoTracks[static_cast<size_t>(flat_)].locked()
                            : seq.audioTracks[static_cast<size_t>(ki)].locked();
    const bool toggled = video
                             ? seq.videoTracks[static_cast<size_t>(flat_)].hidden()
                             : seq.audioTracks[static_cast<size_t>(ki)].muted();
    const bool soloed = !video && seq.audioTracks[static_cast<size_t>(ki)].soloed();

    QMenu menu(this);
    QAction* rename = menu.addAction(tr("Rename Track"));
    menu.addSeparator();
    QAction* insertAbove = menu.addAction(
        video ? tr("Insert Video Track Above") : tr("Insert Audio Track Above"));
    QAction* insertBelow = menu.addAction(
        video ? tr("Insert Video Track Below") : tr("Insert Audio Track Below"));
    menu.addSeparator();
    QAction* moveUp = menu.addAction(tr("Move Track Up"));
    moveUp->setEnabled(ki > 0);
    QAction* moveDown = menu.addAction(tr("Move Track Down"));
    moveDown->setEnabled(ki + 1 < kindCount);
    menu.addSeparator();
    QAction* toggleLock =
        menu.addAction(locked ? tr("Unlock Track") : tr("Lock Track"));
    QAction* toggleAv = menu.addAction(
        video ? (toggled ? tr("Show Track") : tr("Hide Track"))
              : (toggled ? tr("Unmute Track") : tr("Mute Track")));
    QAction* toggleSolo = nullptr;
    if (!video) {
        toggleSolo =
            menu.addAction(soloed ? tr("Unsolo Track") : tr("Solo Track"));
    }
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete Track"));
    remove->setEnabled(video ? kindCount > 1 : true);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == rename) {
        beginRename();
    } else if (chosen == insertAbove) {
        host_->insertTrackAt(flat_, true);
    } else if (chosen == insertBelow) {
        host_->insertTrackAt(flat_, false);
    } else if (chosen == moveUp) {
        host_->moveTrackUp(flat_);
    } else if (chosen == moveDown) {
        host_->moveTrackDown(flat_);
    } else if (chosen == toggleLock) {
        host_->setTrackLocked(flat_, !locked);
    } else if (chosen == toggleAv) {
        if (video) {
            host_->setTrackHidden(flat_, !toggled);
        } else {
            host_->setTrackMuted(flat_, !toggled);
        }
    } else if (toggleSolo && chosen == toggleSolo) {
        host_->setTrackSoloed(flat_, !soloed);
    } else if (chosen == remove) {
        host_->removeTrackAt(flat_);
    }
}

// ---------------------------------------------------------------------------
// TrackHeader
// ---------------------------------------------------------------------------
TrackHeader::TrackHeader(TimelinePanel* host, QWidget* parent)
    : QWidget(parent), host_(host) {
    setFixedWidth(kHeaderWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void TrackHeader::setScrollOffset(int offset) {
    if (offset_ == offset) return;
    offset_ = offset;
    layoutRows();
    update();
}

QSize TrackHeader::sizeHint() const { return {kHeaderWidth, 320}; }

TrackHeaderRow* TrackHeader::rowFor(int flat) const {
    for (TrackHeaderRow* row : rows_) {
        if (row && row->flatTrack() == flat) return row;
    }
    return nullptr;
}

void TrackHeader::rebuild() {
    if (!host_) return;
    const bl::Sequence& seq = host_->sequence();
    const int count =
        static_cast<int>(seq.videoTracks.size() + seq.audioTracks.size());
    if (static_cast<int>(rows_.size()) != count) {
        for (TrackHeaderRow* row : rows_) {
            if (!row) continue;
            row->hide();
            row->setParent(nullptr);
            row->deleteLater();
        }
        rows_.clear();
        for (int flat = 0; flat < count; ++flat) {
            auto* row = new TrackHeaderRow(host_, flat, this);
            row->show();
            rows_.push_back(row);
        }
    }
    for (TrackHeaderRow* row : rows_) {
        if (row) row->refresh();
    }
    layoutRows();
    update();
}

void TrackHeader::layoutRows() {
    if (!host_) return;
    const bl::Sequence& seq = host_->sequence();
    const int vc = static_cast<int>(seq.videoTracks.size());
    for (TrackHeaderRow* row : rows_) {
        if (!row) continue;
        const int flat = row->flatTrack();
        const qreal top = timeline_geometry::laneTop(seq, flat, vc,
                                                     host_->laneOrder()) -
                          static_cast<qreal>(offset_);
        const qreal h = timeline_geometry::laneHeight(seq, flat, vc);
        row->setGeometry(0, static_cast<int>(std::lround(top)), width(),
                         static_cast<int>(std::lround(h)));
    }
}

void TrackHeader::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutRows();
}

void TrackHeader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kHeaderFill);

    // Ruler band label.
    p.fillRect(QRect(0, 0, width(), static_cast<int>(timeline_geometry::kRulerHeight)),
               kRulerFill);
    p.setPen(kText);
    p.drawText(QRect(6, 0, width() - 8,
                     static_cast<int>(timeline_geometry::kRulerHeight)),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Timeline"));
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
    setAcceptDrops(true);
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
    clearDropSession();
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
    const TimelineState::EditMode edit = host_->editMode();

    // Ruler: grab a zone edge if near one, otherwise Range mode sets the in/out
    // zone and every other mode scrubs.
    if (pressScenePos_.y() <= timeline_geometry::kRulerHeight) {
        if (host_->hasZone()) {
            const qreal x0 = host_->xForTime(host_->zoneIn());
            const qreal x1 = host_->xForTime(host_->zoneOut());
            const qreal px = pressScenePos_.x();
            if (std::abs(px - x0) <= kZoneHandleGrabPx) {
                mode_ = Mode::ZoneIn;
                return;
            }
            if (std::abs(px - x1) <= kZoneHandleGrabPx) {
                mode_ = Mode::ZoneOut;
                return;
            }
        }
        if (edit == TimelineState::EditMode::Range) {
            mode_ = Mode::Range;
            rangeAnchor_ = host_->timeAtX(pressScenePos_.x());
            host_->setZone(rangeAnchor_, rangeAnchor_);
            return;
        }
        mode_ = Mode::Scrub;
        host_->setPlayhead(host_->timeAtX(pressScenePos_.x()));
        return;
    }

    ClipItem* item = clipItemAt(pressScenePos_);
    if (item) {
        pressClip_ = item->clip().id;
        pressLane_ = item->flatTrack();
        host_->setActiveTrack(pressLane_);

        // Ctrl/Shift toggles selection in the two "select-like" modes.
        if (edit == TimelineState::EditMode::Select &&
            (mods & (Qt::ControlModifier | Qt::ShiftModifier))) {
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

        const auto buildSingleMove = [&] {
            PendingMove pm;
            pm.id = pressClip_;
            pm.fromTrack = item->flatTrack();
            pm.toTrack = pm.fromTrack;
            pm.oldStart = item->clip().timelineStart;
            pm.newStart = pm.oldStart;
            pendingMoves_.push_back(pm);
        };

        switch (edit) {
        case TimelineState::EditMode::Select: {
            const ClipItem::Edge edge = item->edgeAt(pressScenePos_);
            if (edge == ClipItem::Edge::Left || edge == ClipItem::Edge::Right) {
                mode_ = edge == ClipItem::Edge::Left ? Mode::TrimLeft
                                                     : Mode::TrimRight;
                trimRipple_ =
                    (mods & Qt::ShiftModifier) || host_->rippleEnabled();
                trimFlat_ = item->flatTrack();
                trimAnchorStart_ = item->clip().timelineStart;
                trimAnchorEnd_ =
                    item->clip().timelineStart + item->clip().timelineDuration;
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
            break;
        }
        case TimelineState::EditMode::Ripple:
            buildSingleMove();
            mode_ = Mode::Ripple;
            break;
        case TimelineState::EditMode::Slip:
            slipAnchor_ = host_->timeAtX(pressScenePos_.x());
            slipCurrent_ = slipAnchor_;
            mode_ = Mode::Slip;
            break;
        case TimelineState::EditMode::Roll: {
            ClipId left;
            ClipId right;
            bl::Time boundary{0, {1'000'000, 1}};
            if (seamNear(pressLane_, pressScenePos_.x(), left, right,
                         boundary)) {
                rollFlat_ = pressLane_;
                rollLeftId_ = left;
                rollRightId_ = right;
                rollAnchor_ = boundary;
                rollCurrent_ = boundary;
                mode_ = Mode::Roll;
            } else {
                mode_ = Mode::None;
            }
            break;
        }
        case TimelineState::EditMode::Spacer:
            spacerFlat_ = item->flatTrack();
            spacerAnchor_ = host_->timeAtX(pressScenePos_.x());
            spacerCurrent_ = spacerAnchor_;
            mode_ = Mode::Spacer;
            break;
        case TimelineState::EditMode::Range:
            // Range only acts on the ruler.
            mode_ = Mode::None;
            break;
        }
        return;
    }

    // Empty lane area.
    if (pressLane_ >= 0) host_->setActiveTrack(pressLane_);

    if (edit == TimelineState::EditMode::Spacer) {
        if (!mods) host_->clearSelection();
        spacerFlat_ = pressLane_;
        spacerAnchor_ = host_->timeAtX(pressScenePos_.x());
        spacerCurrent_ = spacerAnchor_;
        mode_ = Mode::Spacer;
        return;
    }

    // Click = clear, drag = marquee (Ctrl/Shift preserves).
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
    case Mode::Range:
        updateRangePreview(scenePos);
        break;
    case Mode::Ripple:
        updateRipplePreview(scenePos);
        break;
    case Mode::Slip:
        updateSlipPreview(scenePos);
        break;
    case Mode::Roll:
        updateRollPreview(scenePos);
        break;
    case Mode::Spacer:
        updateSpacerPreview(scenePos);
        break;
    case Mode::ZoneIn:
        host_->setZoneIn(host_->timeAtX(scenePos.x()));
        break;
    case Mode::ZoneOut:
        host_->setZoneOut(host_->timeAtX(scenePos.x()));
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

bool TimelineView::seamNear(int track, qreal sceneX, ClipId& leftId,
                            ClipId& rightId, bl::Time& boundary) const {
    if (!host_ || track < 0 || track >= host_->trackCount()) return false;
    const bl::Sequence& seq = host_->sequence();
    const std::vector<Clip>& clips =
        host_->editor().isVideoTrack(seq, track)
            ? seq.videoTracks[static_cast<size_t>(track)].clips()
            : seq.audioTracks[static_cast<size_t>(
                  track - static_cast<int>(seq.videoTracks.size()))]
                  .clips();

    constexpr qreal kSeamTolerancePx = 8.0;
    bool found = false;
    qreal best = 0.0;
    for (size_t i = 0; i + 1 < clips.size(); ++i) {
        const bl::Time leftEnd =
            clips[i].timelineStart + clips[i].timelineDuration;
        if (leftEnd != clips[i + 1].timelineStart) continue;
        const qreal x = host_->xForTime(leftEnd);
        const qreal d = std::abs(x - sceneX);
        if (d <= kSeamTolerancePx && (!found || d < best)) {
            found = true;
            best = d;
            leftId = clips[i].id;
            rightId = clips[i + 1].id;
            boundary = leftEnd;
        }
    }
    return found;
}

void TimelineView::updateRangePreview(const QPointF& scenePos) {
    const bl::Time t = host_->timeAtX(scenePos.x());
    host_->setZone(rangeAnchor_, t);
}

void TimelineView::updateRipplePreview(const QPointF& scenePos) {
    updateMovePreview(scenePos);
}

void TimelineView::updateSlipPreview(const QPointF& scenePos) {
    const bl::Time raw = host_->rawTimeAtX(scenePos.x());
    slipCurrent_ = host_->timeAtX(scenePos.x());
    if (host_->snapIndicator_ && slipCurrent_ != raw && host_->snapEnabled()) {
        host_->snapIndicator_->setPos(host_->xForTime(slipCurrent_), 0.0);
        host_->snapIndicator_->setActive(true);
    } else {
        clearSnapIndicator();
    }
}

void TimelineView::updateRollPreview(const QPointF& scenePos) {
    if (rollFlat_ < 0) return;
    const bl::Sequence& seq = host_->sequence();
    const bl::Time raw = host_->rawTimeAtX(scenePos.x());
    rollCurrent_ = host_->snapEnabled()
                       ? host_->editor().snapToNearest(seq, raw,
                                                       snapToleranceFrames())
                       : raw;

    ClipItem* left = host_->clipItemFor(rollLeftId_);
    ClipItem* right = host_->clipItemFor(rollRightId_);
    if (left && right) {
        const QRectF lr = host_->laneRect(rollFlat_);
        const qreal x = host_->xForTime(rollCurrent_);
        const qreal lx = host_->xForTime(left->clip().timelineStart);
        const qreal rx = host_->xForTime(right->clip().timelineStart +
                                         right->clip().timelineDuration);
        left->setPreviewRect(QRectF(lx, lr.top(), x - lx, lr.height()));
        right->setPreviewRect(QRectF(x, lr.top(), rx - x, lr.height()));
    }

    if (host_->snapIndicator_ && rollCurrent_ != raw && host_->snapEnabled()) {
        host_->snapIndicator_->setPos(host_->xForTime(rollCurrent_), 0.0);
        host_->snapIndicator_->setActive(true);
    } else {
        clearSnapIndicator();
    }
}

void TimelineView::updateSpacerPreview(const QPointF& scenePos) {
    const bl::Time raw = host_->rawTimeAtX(scenePos.x());
    spacerCurrent_ = host_->timeAtX(scenePos.x());
    if (host_->snapIndicator_ && spacerCurrent_ != raw && host_->snapEnabled()) {
        host_->snapIndicator_->setPos(host_->xForTime(spacerCurrent_), 0.0);
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

    if (host_->insertMode()) {
        if (!host_->editor().insertMove(entries)) {
            host_->reportEditorError();
            host_->rebuildFromModel();
        }
    } else if (!host_->editor().groupMove(entries)) {
        host_->rebuildFromModel();
    }
    pendingMoves_.clear();
}

void TimelineView::commitRipple() {
    if (pendingMoves_.empty()) return;
    const PendingMove pm = pendingMoves_.front();
    if (!host_->editor().rippleMove(pm.fromTrack, pm.id, pm.newStart)) {
        host_->reportEditorError();
        host_->rebuildFromModel();
    }
    pendingMoves_.clear();
}

void TimelineView::commitSlip() {
    if (pressClip_.empty()) return;
    const bl::Duration delta = slipCurrent_ - slipAnchor_;
    if (delta == bl::Duration{}) return;
    if (!host_->editor().slipClip(pressLane_, pressClip_, delta)) {
        host_->reportEditorError();
        host_->rebuildFromModel();
    }
}

void TimelineView::commitRoll() {
    if (rollFlat_ < 0) return;
    if (!host_->editor().rollTrim(rollFlat_, rollLeftId_, rollCurrent_)) {
        host_->reportEditorError();
        host_->rebuildFromModel();
    }
    rollFlat_ = -1;
}

void TimelineView::commitSpacer() {
    if (spacerFlat_ < 0) return;
    const bl::Duration delta = spacerCurrent_ - spacerAnchor_;
    if (delta == bl::Duration{}) return;
    const bool ok =
        delta > bl::Duration{}
            ? host_->editor().addSpacer(spacerFlat_, spacerAnchor_, delta)
            : host_->editor().removeSpacer(spacerFlat_, spacerAnchor_, -delta);
    if (!ok) {
        host_->reportEditorError();
        host_->rebuildFromModel();
    }
    spacerFlat_ = -1;
}

void TimelineView::commitTrim(bool leftEdge) {
    const bool ok =
        host_->editor().trimLinked(trimFlat_, pressClip_, trimCurrent_, leftEdge,
                                   trimRipple_);
    if (!ok) {
        host_->reportEditorError();
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
    const TimelineState::EditMode edit = host_->editMode();
    ClipItem* item = clipItemAt(scenePos);
    Qt::CursorShape shape = Qt::ArrowCursor;

    // A zone edge grabs in any mode while the pointer is over the ruler.
    if (scenePos.y() <= timeline_geometry::kRulerHeight && host_->hasZone()) {
        const qreal x0 = host_->xForTime(host_->zoneIn());
        const qreal x1 = host_->xForTime(host_->zoneOut());
        if (std::abs(scenePos.x() - x0) <= kZoneHandleGrabPx ||
            std::abs(scenePos.x() - x1) <= kZoneHandleGrabPx) {
            viewport()->setCursor(Qt::SizeHorCursor);
            return;
        }
    }

    switch (edit) {
    case TimelineState::EditMode::Slip:
        if (item) shape = Qt::SizeHorCursor;
        break;
    case TimelineState::EditMode::Roll: {
        ClipId left;
        ClipId right;
        bl::Time boundary{0, {1'000'000, 1}};
        const int lane = host_->flatAtY(scenePos.y());
        if (lane >= 0 && seamNear(lane, scenePos.x(), left, right, boundary)) {
            shape = Qt::SplitHCursor;
        }
        break;
    }
    case TimelineState::EditMode::Spacer:
        if (scenePos.y() > timeline_geometry::kRulerHeight) {
            shape = Qt::SplitHCursor;
        }
        break;
    case TimelineState::EditMode::Range:
        if (scenePos.y() <= timeline_geometry::kRulerHeight) {
            shape = Qt::SizeHorCursor;
        }
        break;
    default:
        if (item && item->edgeAt(scenePos) != ClipItem::Edge::None) {
            shape = Qt::SizeHorCursor;
        }
        break;
    }
    viewport()->setCursor(shape);
}

void TimelineView::clearSnapIndicator() {
    if (host_->snapIndicator_) {
        host_->snapIndicator_->setActive(false);
    }
}

// ---------------------------------------------------------------------------
// External drop (media bin row, or OS file-manager payload → timeline).
// ---------------------------------------------------------------------------

bool TimelineView::startDropSession(const std::string& mediaId) {
    clearDropSession();
    ProjectController* controller = host_ ? host_->controller() : nullptr;
    if (!controller || mediaId.empty()) return false;

    const MediaBinItem* entry = nullptr;
    for (const auto& item : controller->mediaBin()) {
        if (item.id == mediaId) {
            entry = &item;
            break;
        }
    }
    if (!entry) {
        reportDropProblem(tr("Media is no longer in the project"));
        return false;
    }

    auto probe = MediaSource::probe(MediaLocator{entry->path});
    if (!probe) {
        reportDropProblem(
            tr("Cannot read media: %1")
                .arg(QString::fromStdString(entry->name)));
        return false;
    }

    DropCandidate candidate;
    candidate.mediaId = mediaId;
    candidate.probe = std::move(probe.value());
    dropSession_.items.push_back(std::move(candidate));
    return true;
}

bool TimelineView::startDropSessionFromFiles(const QStringList& paths) {
    clearDropSession();
    if (!host_ || !host_->controller() || paths.isEmpty()) return false;

    // Probe now, on drag-enter, so the placement ghost can show real durations
    // before the user releases the button. Nothing is imported yet: an
    // abandoned drag must leave the project untouched.
    int skipped = 0;
    for (const QString& path : paths) {
        if (path.isEmpty()) continue;
        auto probe = MediaSource::probe(MediaLocator{path.toStdString()});
        if (!probe) {
            ++skipped;  // unreadable / not media
            continue;
        }
        DropCandidate candidate;
        candidate.path = path;
        candidate.probe = std::move(probe.value());
        dropSession_.items.push_back(std::move(candidate));
    }
    if (dropSession_.items.empty()) {
        dropSession_ = DropSession{};
        reportDropProblem(
            tr("None of the dropped files could be read as media"));
        return false;
    }
    if (skipped > 0) {
        // A partly readable batch still drags, but the user should know some
        // of the files will not come along before they release the drop.
        reportDropProblem(tr("%n dropped file(s) could not be read", nullptr,
                             skipped));
    }
    return true;
}

void TimelineView::dropTargetAt(const QPointF& scenePos, DropTarget& target) const {
    target = DropTarget{};
    if (!host_ || dropSession_.items.empty()) return;

    int lane = host_->flatAtY(scenePos.y());
    const int trackCount = host_->trackCount();
    if (lane < 0 || lane >= trackCount) {
        target.reason = tr("Drop onto a track, not the ruler");
        return;
    }

    const bl::Sequence& seq = host_->sequence();
    // Planned clips are validated against a running copy of the sequence so a
    // multi-file drop does not stack every item on top of the first.
    bl::Sequence working = seq;

    bl::Time cursor = host_->timeAtX(scenePos.x());
    for (size_t i = 0; i < dropSession_.items.size(); ++i) {
        const DropCandidate& candidate = dropSession_.items[i];
        if (!candidate.probe) {
            target.reason = tr("Cannot read this media");
            return;
        }

        const bool hasVideo = !candidate.probe->videoStreams.empty();
        const bool hasAudio = !candidate.probe->audioStreams.empty();
        if (!hasVideo && !hasAudio) {
            target.reason = tr("File has no video or audio streams");
            return;
        }

        const bl::Duration mediaDuration = candidate.probe->duration;
        if (mediaDuration.ticks <= 0) {
            target.reason = tr("Unknown media duration");
            return;
        }

        // The media id is unresolved for an external file (it is assigned on
        // import). A placeholder is fine for planning: only the duration and
        // stream kinds are consulted, and the committed clip is rebuilt with
        // the real id.
        auto clipOpt = makeThreePointClip(
            SourceRef{candidate.mediaId.empty() ? std::string("pending")
                                                : candidate.mediaId,
                      bl::Time{},
                      bl::Time{mediaDuration.ticks, mediaDuration.rate}},
            SpeedRemap{}, std::string());
        if (!clipOpt) {
            target.reason = tr("Cannot build a clip from this media");
            return;
        }
        bl::Clip clip = *clipOpt;
        clip.timelineStart = cursor;

        // The first item must fit exactly where it was dropped, matching the
        // single-item behaviour (an occupied slot rejects the drop rather than
        // silently relocating it). Later items of the same drag spill onto the
        // next compatible lane, which is what makes a multi-file drop usable.
        // `lane` only ever moves downwards across items: a multi-file drop
        // stacks into the lanes below the cursor instead of jumping back up.
        while (lane < trackCount) {
            const bool videoLane = host_->editor().isVideoTrack(working, lane);
            const bool compatible =
                (videoLane && hasVideo) || (!videoLane && hasAudio);
            if (compatible && host_->editor().canPlaceAt(working, lane, clip,
                                                         cursor)) {
                break;
            }
            if (i == 0) {
                // never relocate the item under the cursor
                target.reason =
                    compatible ? tr("That spot is occupied")
                               : tr("No compatible track at the cursor");
                return;
            }
            ++lane;
        }
        if (lane >= trackCount) {
            target.reason = tr("Not enough room for all dropped files");
            return;
        }

        DropPlacement placement;
        placement.flat = lane;
        placement.start = cursor;
        placement.clip = clip;
        target.placements.push_back(std::move(placement));

        // Record the reservation on the working copy so the next item sees it.
        // A failed reservation would leave the next item free to plan into the
        // same space, so treat it as an invalid plan rather than carry on.
        const bool videoLane = host_->editor().isVideoTrack(working, lane);
        const bool reserved =
            videoLane
                ? working.videoTracks[static_cast<size_t>(lane)].addClip(clip)
                : working.audioTracks[static_cast<size_t>(
                      lane - static_cast<int>(working.videoTracks.size()))]
                      .addClip(clip);
        if (!reserved) {
            target.placements.pop_back();
            target.reason = tr("Not enough room for all dropped files");
            return;
        }
        cursor = cursor + clip.timelineDuration;
    }

    target.valid = !target.placements.empty();
}

void TimelineView::hideDropGhosts() {
    for (QGraphicsRectItem* ghost : dropGhosts_) {
        if (ghost) ghost->setVisible(false);
    }
}

void TimelineView::updateDropSession(const QPointF& scenePos) {
    if (dropSession_.items.empty()) return;

    DropTarget target;
    dropTargetAt(scenePos, target);
    dropTarget_ = target;

    if (!target.valid) {
        hideDropGhosts();
        reportDropProblem(target.reason);
        return;
    }
    lastDropReason_.clear();  // a valid plan supersedes any shown refusal

    while (dropGhosts_.size() < target.placements.size()) {
        auto* ghost = new QGraphicsRectItem;
        ghost->setData(0, QString::fromLatin1(kDropGhostObjectName));
        ghost->setPen(QPen(kDropGhostValid, 2));
        ghost->setBrush(QBrush(kDropGhostValid));
        ghost->setOpacity(0.35);
        ghost->setZValue(2000);
        scene()->addItem(ghost);
        dropGhosts_.push_back(ghost);
    }
    for (size_t i = 0; i < dropGhosts_.size(); ++i) {
        QGraphicsRectItem* ghost = dropGhosts_[i];
        if (!ghost) continue;
        if (i >= target.placements.size()) {
            ghost->setVisible(false);  // stale ghost from a longer previous plan
            continue;
        }
        const DropPlacement& placement = target.placements[i];
        const QRectF lane = host_->laneRect(placement.flat);
        const qreal x0 = host_->xForTime(placement.start);
        const qreal x1 = host_->xForTime(placement.start +
                                        placement.clip.timelineDuration);
        ghost->setRect(QRectF(x0, lane.top(), std::max<qreal>(1.0, x1 - x0),
                              lane.height()));
        ghost->setVisible(true);
    }
}

void TimelineView::finishDropSession(const QPointF& scenePos) {
    if (dropSession_.items.empty()) {
        clearDropSession();
        return;
    }
    ProjectController* controller = host_ ? host_->controller() : nullptr;
    if (!controller) {
        clearDropSession();
        return;
    }

    DropTarget target;
    dropTargetAt(scenePos, target);
    if (!target.valid) {
        emit controller->statusMessage(target.reason.isEmpty()
                                           ? QStringLiteral("Cannot place clip here")
                                           : target.reason);
        clearDropSession();
        return;
    }

    // Snapshot the session before touching the model. Importing or adding a
    // clip emits projectChanged/timelineChanged, which rebuilds the view and
    // calls cancelActiveGesture() — that tears the drop session down, so
    // anything still needed after the commit must be copied out first.
    const std::vector<DropCandidate> items = dropSession_.items;
    clearDropSession();

    // Import first: an external file only gets a media-bin id here, and the
    // committed clips must reference the real id. addToMediaBin() is a no-op
    // for a path already in the bin, so re-dropping a known file just reuses
    // the existing entry.
    const bool importing =
        std::any_of(items.begin(), items.end(),
                    [](const DropCandidate& c) { return !c.path.isEmpty(); });

    UndoStack& undoStack = controller->undoStack();
    // A file-manager drop is one user action, so it collapses to one undo step
    // however many files it carried: the imports and the clips go together and
    // revert with a single Ctrl+Z. A media-bin row drag pushes no import, so
    // it stays the plain single "Add clip" command it has always been.
    const bool macro = importing;
    if (macro) undoStack.beginMacro("Import media and add clips");

    std::vector<std::string> ids(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const DropCandidate& candidate = items[i];
        if (candidate.path.isEmpty()) {
            ids[i] = candidate.mediaId;
            continue;
        }
        const QString path = candidate.path;
        controller->addToMediaBin(path);
        for (const MediaBinItem& item : controller->mediaBin()) {
            if (item.path == path.toStdString()) {
                ids[i] = item.id;
                break;
            }
        }
    }

    int placed = 0;
    for (size_t i = 0; i < target.placements.size() && i < ids.size(); ++i) {
        DropPlacement placement = target.placements[i];
        const std::string& id = ids[i];
        if (id.empty()) continue;

        // Rebuild with the resolved id so the clip points at real media.
        // makeThreePointClip() leaves Clip::id empty, which is exactly what
        // TimelineEditController::addClip() expects (it generates "ui:N").
        auto clipOpt = makeThreePointClip(
            SourceRef{id, placement.clip.source.sourceIn,
                      placement.clip.source.sourceOut},
            placement.clip.speed, placement.clip.name);
        if (!clipOpt) continue;
        clipOpt->timelineStart = placement.start;
        if (host_->editor().addClip(placement.flat, *clipOpt)) ++placed;
    }

    if (macro) undoStack.endMacro();

    if (placed > 0) {
        if (macro) {
            emit controller->statusMessage(
                tr("Imported %n clip(s)", nullptr, placed));
        } else {
            const DropCandidate& candidate = items.front();
            const QString label = candidate.path.isEmpty()
                                      ? QString::fromStdString(candidate.mediaId)
                                      : QFileInfo(candidate.path).fileName();
            emit controller->statusMessage(
                QStringLiteral("Added %1").arg(label));
        }
    } else {
        emit controller->statusMessage(QStringLiteral("Cannot place clip here"));
    }
    if (placed != static_cast<int>(target.placements.size())) {
        // A partial placement is still consistent with the planned layout, but
        // repaint so the view matches the model that actually landed.
        host_->rebuildFromModel();
    }
}

void TimelineView::clearDropSession() {
    for (QGraphicsRectItem* ghost : dropGhosts_) {
        if (!ghost) continue;
        if (scene()) scene()->removeItem(ghost);
        delete ghost;
    }
    dropGhosts_.clear();
    dropSession_ = DropSession{};
    dropTarget_ = DropTarget{};
    lastDropReason_.clear();
}

void TimelineView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(QString::fromLatin1(kMediaBinMime))) {
        const std::string mediaId =
            QString::fromLatin1(event->mimeData()->data(
                                    QString::fromLatin1(kMediaBinMime)))
                .toStdString();
        // On failure startDropSession() has already reported why (missing
        // media, unreadable file) — a refused drag is never silent.
        if (startDropSession(mediaId)) {
            event->acceptProposedAction();
            return;
        }
    } else {
        const QStringList paths =
            media_bin_detail::localFilePaths(event->mimeData());
        // Foreign payloads (plain text etc.) produce an empty path list and
        // are ignored without a status message; a real file drag that cannot
        // be read reports why inside startDropSessionFromFiles().
        if (!paths.isEmpty() && startDropSessionFromFiles(paths)) {
            // Media files dragged straight from the file manager: imported
            // into the bin and placed on the timeline by the drop.
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void TimelineView::reportDropProblem(const QString& reason) {
    if (reason.isEmpty() || reason == lastDropReason_) return;
    lastDropReason_ = reason;
    if (host_ && host_->controller()) {
        emit host_->controller()->statusMessage(reason);
    }
}

void TimelineView::dragMoveEvent(QDragMoveEvent* event) {
    if (!dropSession_.items.empty()) {
        updateDropSession(mapToScene(event->position().toPoint()));
        if (dropTarget_.valid) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
        return;
    }
    event->ignore();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent* event) {
    clearDropSession();
    QGraphicsView::dragLeaveEvent(event);
}

void TimelineView::dropEvent(QDropEvent* event) {
    if (!dropSession_.items.empty()) {
        finishDropSession(mapToScene(event->position().toPoint()));
        event->acceptProposedAction();
        return;
    }
    event->ignore();
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
    case Mode::Range:
        if (!dragging_) host_->clearZone();
        break;
    case Mode::Ripple:
        if (dragging_) {
            commitRipple();
        }
        break;
    case Mode::Slip:
        if (dragging_) commitSlip();
        break;
    case Mode::Roll:
        if (dragging_) commitRoll();
        break;
    case Mode::Spacer:
        if (dragging_) commitSpacer();
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
        performDelete(host_->rippleEnabled());
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
    case Qt::Key_C:
        if (event->modifiers() & Qt::ControlModifier) {
            host_->copySelection();
            event->accept();
            return;
        }
        break;
    case Qt::Key_X:
        if (event->modifiers() & Qt::ControlModifier) {
            host_->cutSelection();
            event->accept();
            return;
        }
        break;
    case Qt::Key_V:
        if (event->modifiers() & Qt::ControlModifier) {
            host_->pasteAtPlayhead();
            event->accept();
            return;
        }
        break;
    case Qt::Key_M:
        // Add a sequence marker at the playhead (with or without Ctrl).
        if (!(event->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))) {
            host_->addMarkerAt(host_->playhead());
            event->accept();
            return;
        }
        break;
    case Qt::Key_G:
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->modifiers() & Qt::ShiftModifier) {
                // Ungroup: find any selected clip that's in a group and ungroup it.
                for (const auto& entry : host_->clipItems_) {
                    if (host_->selection().contains(entry.first)) {
                        host_->editor().ungroupClips(entry.second->flatTrack(), entry.first);
                        break;
                    }
                }
            } else {
                // Group: collect selected clips and pass to groupClips.
                std::vector<std::pair<int, bl::ClipId>> members;
                for (const auto& entry : host_->clipItems_) {
                    if (host_->selection().contains(entry.first)) {
                        members.push_back({entry.second->flatTrack(), entry.first});
                    }
                }
                if (members.size() >= 2) {
                    host_->editor().groupClips(members);
                } else if (host_->controller()) {
                    host_->controller()->statusMessage(
                        tr("Select at least two clips to group"));
                }
            }
            event->accept();
            return;
        }
        break;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

// Right-click menus: on a marker (rename/delete), on the ruler (marker + zone
// commands at the clicked time), on a clip (clipboard / edit / enable), or on
// empty lane space (paste / select all). Everything funnels through the
// panel's undoable operations so the menu and the shortcuts stay in sync.
void TimelineView::contextMenuEvent(QContextMenuEvent* event) {
    if (!host_) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }
    const QPointF scenePos = mapToScene(event->pos());
    const bl::Time at = host_->rawTimeAtX(scenePos.x());
    const qreal pxPerFrame = std::max<qreal>(host_->zoom(), 1e-6);
    const int hitRadius =
        std::max(1, static_cast<int>(std::llround(6.0 / pxPerFrame)));

    QMenu menu(this);

    // 1) A marker under the cursor wins: rename/delete it.
    bl::Time markerPos;
    if (host_->markerNear(at, hitRadius, markerPos)) {
        const auto idx = host_->sequence().findMarkerAt(markerPos);
        const QString label =
            idx ? QString::fromStdString(host_->sequence().markers[*idx].label)
                : QString();
        QAction* renameAct = menu.addAction(tr("Rename Marker…"));
        QAction* deleteAct = menu.addAction(tr("Delete Marker"));
        QAction* picked = menu.exec(event->globalPos());
        if (picked == renameAct) {
            bool ok = false;
            const QString text =
                QInputDialog::getText(this, tr("Rename Marker"), tr("Label:"),
                                      QLineEdit::Normal, label, &ok);
            if (ok) host_->renameMarkerAt(markerPos, text);
        } else if (picked == deleteAct) {
            host_->removeMarkerAt(markerPos);
        }
        event->accept();
        return;
    }

    // 2) The ruler band: marker + zone commands anchored at the click.
    if (scenePos.y() <= timeline_geometry::kRulerHeight) {
        QAction* addMarker = menu.addAction(tr("Add Marker Here"));
        menu.addSeparator();
        QAction* setIn = menu.addAction(tr("Set Zone In Here"));
        QAction* setOut = menu.addAction(tr("Set Zone Out Here"));
        QAction* clearZone =
            host_->hasZone() ? menu.addAction(tr("Clear Zone")) : nullptr;
        QAction* picked = menu.exec(event->globalPos());
        if (picked == addMarker) {
            host_->addMarkerAt(at);
        } else if (picked == setIn) {
            host_->setZoneIn(at);
        } else if (picked == setOut) {
            host_->setZoneOut(at);
        } else if (picked != nullptr && picked == clearZone) {
            host_->clearZone();
        }
        event->accept();
        return;
    }

    // 3) A clip: act on the whole selection, ensuring the clip is in it.
    if (ClipItem* item = clipItemAt(scenePos)) {
        if (!host_->selection().contains(item->clip().id)) {
            host_->setSelection(QSet<bl::ClipId>{item->clip().id});
        }
        const bool enabled = item->clip().enabled;
        QAction* cutAct = menu.addAction(tr("Cut"));
        QAction* copyAct = menu.addAction(tr("Copy"));
        QAction* pasteAct = menu.addAction(tr("Paste"));
        pasteAct->setEnabled(host_->editor().hasClipboard());
        menu.addSeparator();
        QAction* splitAct = menu.addAction(tr("Split at Playhead"));
        QAction* deleteAct = menu.addAction(tr("Delete"));
        QAction* rippleDeleteAct = menu.addAction(tr("Ripple Delete"));
        menu.addSeparator();
        QAction* toggleAct =
            menu.addAction(enabled ? tr("Disable") : tr("Enable"));
        menu.addSeparator();
        QAction* zoneInAct = menu.addAction(tr("Set Zone In Here"));
        QAction* zoneOutAct = menu.addAction(tr("Set Zone Out Here"));
        // Titles carry their stored design, so they can be re-opened in the
        // Title Editor straight from the clip they are on.
        QAction* editTitleAct = nullptr;
        if (host_->controller() &&
            !host_->controller()
                 ->titleSpecFor(item->clip().source.mediaItemId)
                 .is_null()) {
            menu.addSeparator();
            editTitleAct = menu.addAction(tr("Edit Title…"));
        }
        QAction* picked = menu.exec(event->globalPos());
        if (picked == cutAct) {
            host_->cutSelection();
        } else if (picked == copyAct) {
            host_->copySelection();
        } else if (picked == pasteAct) {
            host_->pasteAtPlayhead();
        } else if (picked == splitAct) {
            performSplit();
        } else if (picked == deleteAct) {
            performDelete(host_->rippleEnabled());
        } else if (picked == rippleDeleteAct) {
            performDelete(true);
        } else if (picked == toggleAct) {
            // Toggle every selected clip to the opposite of the clicked one.
            std::vector<std::pair<int, bl::ClipId>> clips;
            for (const auto& entry : host_->clipItems_) {
                if (host_->selection().contains(entry.first)) {
                    clips.push_back({entry.second->flatTrack(), entry.first});
                }
            }
            for (const auto& [flat, id] : clips) {
                host_->editor().setClipEnabled(flat, id, !enabled);
            }
        } else if (picked == zoneInAct) {
            host_->setZoneIn(at);
        } else if (picked == zoneOutAct) {
            host_->setZoneOut(at);
        } else if (editTitleAct && picked == editTitleAct) {
            emit host_->editTitleRequested(item->clip().source.mediaItemId);
        }
        event->accept();
        return;
    }

    // 4) Empty lane space.
    QAction* pasteAct = menu.addAction(tr("Paste"));
    pasteAct->setEnabled(host_->editor().hasClipboard());
    QAction* addMarker = menu.addAction(tr("Add Marker Here"));
    menu.addSeparator();
    QAction* selectAllAct = menu.addAction(tr("Select All"));
    QAction* picked = menu.exec(event->globalPos());
    if (picked == pasteAct) {
        host_->pasteAt(at);
    } else if (picked == addMarker) {
        host_->addMarkerAt(at);
    } else if (picked == selectAllAct) {
        performSelectAll();
    }
    event->accept();
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
    // Convert to (flat, id) pairs for the atomic removeClips.
    std::vector<std::pair<int, bl::ClipId>> clips;
    clips.reserve(refs.size());
    for (const auto& ref : refs) clips.push_back({ref.flat, ref.id});
    if (!host_->editor().removeClips(clips, ripple)) {
        host_->reportEditorError();
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

    addVideoBtn_ = new QToolButton(this);
    addVideoBtn_->setObjectName(QStringLiteral("addVideoTrackButton"));
    addVideoBtn_->setText(QStringLiteral("+V"));
    addVideoBtn_->setToolTip(tr("Add video track"));
    addVideoBtn_->setAutoRaise(true);
    addVideoBtn_->setFocusPolicy(Qt::NoFocus);
    addAudioBtn_ = new QToolButton(this);
    addAudioBtn_->setObjectName(QStringLiteral("addAudioTrackButton"));
    addAudioBtn_->setText(QStringLiteral("+A"));
    addAudioBtn_->setToolTip(tr("Add audio track"));
    addAudioBtn_->setAutoRaise(true);
    addAudioBtn_->setFocusPolicy(Qt::NoFocus);

    // Lane layout picker (Kdenlive parity): Normal / Mixed / Mixed reverse.
    laneOrderCombo_ = new QComboBox(this);
    laneOrderCombo_->setObjectName(QStringLiteral("laneOrderCombo"));
    laneOrderCombo_->setToolTip(tr("Track layout"));
    laneOrderCombo_->setFocusPolicy(Qt::NoFocus);
    laneOrderCombo_->addItem(
        tr("Normal"),
        static_cast<int>(timeline_geometry::LaneOrder::Split));
    laneOrderCombo_->addItem(
        tr("Mixed"),
        static_cast<int>(timeline_geometry::LaneOrder::Mixed));
    laneOrderCombo_->addItem(
        tr("Mixed reverse"),
        static_cast<int>(timeline_geometry::LaneOrder::MixedReverse));

    // Edit-mode picker (Kdenlive parity): Select / Range / Ripple / Roll /
    // Slip / Spacer.
    editModeCombo_ = new QComboBox(this);
    editModeCombo_->setObjectName(QStringLiteral("editModeCombo"));
    editModeCombo_->setToolTip(tr("Timeline edit mode"));
    editModeCombo_->setFocusPolicy(Qt::NoFocus);
    const struct {
        TimelineState::EditMode mode;
        const char* label;
    } kModes[] = {
        {TimelineState::EditMode::Select, QT_TR_NOOP("Select")},
        {TimelineState::EditMode::Range, QT_TR_NOOP("Range")},
        {TimelineState::EditMode::Ripple, QT_TR_NOOP("Ripple")},
        {TimelineState::EditMode::Roll, QT_TR_NOOP("Roll")},
        {TimelineState::EditMode::Slip, QT_TR_NOOP("Slip")},
        {TimelineState::EditMode::Spacer, QT_TR_NOOP("Spacer")},
    };
    for (const auto& entry : kModes) {
        editModeCombo_->addItem(tr(entry.label),
                                static_cast<int>(entry.mode));
    }

    insertModeBtn_ = new QToolButton(this);
    insertModeBtn_->setObjectName(QStringLiteral("insertModeButton"));
    insertModeBtn_->setText(tr("Insert"));
    insertModeBtn_->setToolTip(tr("Insert clips (ripple) instead of overwriting"));
    insertModeBtn_->setCheckable(true);
    insertModeBtn_->setChecked(state_.insertMode);
    insertModeBtn_->setAutoRaise(true);
    insertModeBtn_->setFocusPolicy(Qt::NoFocus);

    rippleBtn_ = new QToolButton(this);
    rippleBtn_->setObjectName(QStringLiteral("rippleButton"));
    rippleBtn_->setText(tr("Ripple"));
    rippleBtn_->setToolTip(tr("Ripple delete and trim"));
    rippleBtn_->setCheckable(true);
    rippleBtn_->setChecked(state_.ripple);
    rippleBtn_->setAutoRaise(true);
    rippleBtn_->setFocusPolicy(Qt::NoFocus);

    // A/V auto-split (Phase 6): drop an A/V clip as a linked video+audio pair.
    avSplitBtn_ = new QToolButton(this);
    avSplitBtn_->setObjectName(QStringLiteral("avSplitButton"));
    avSplitBtn_->setText(tr("A/V"));
    avSplitBtn_->setToolTip(
        tr("Drop A/V clips as linked video + audio halves"));
    avSplitBtn_->setCheckable(true);
    avSplitBtn_->setChecked(state_.autoSplitAv);
    avSplitBtn_->setAutoRaise(true);
    avSplitBtn_->setFocusPolicy(Qt::NoFocus);

    // Timeline zone (Phase 5): set the in/out points at the playhead and clear.
    zoneInBtn_ = new QToolButton(this);
    zoneInBtn_->setObjectName(QStringLiteral("zoneInButton"));
    zoneInBtn_->setText(tr("Set In"));
    zoneInBtn_->setToolTip(tr("Set the timeline zone in point at the playhead"));
    zoneInBtn_->setAutoRaise(true);
    zoneInBtn_->setFocusPolicy(Qt::NoFocus);

    zoneOutBtn_ = new QToolButton(this);
    zoneOutBtn_->setObjectName(QStringLiteral("zoneOutButton"));
    zoneOutBtn_->setText(tr("Set Out"));
    zoneOutBtn_->setToolTip(tr("Set the timeline zone out point at the playhead"));
    zoneOutBtn_->setAutoRaise(true);
    zoneOutBtn_->setFocusPolicy(Qt::NoFocus);

    clearZoneBtn_ = new QToolButton(this);
    clearZoneBtn_->setObjectName(QStringLiteral("clearZoneButton"));
    clearZoneBtn_->setText(tr("Clear Zone"));
    clearZoneBtn_->setToolTip(tr("Delete the timeline zone"));
    clearZoneBtn_->setAutoRaise(true);
    clearZoneBtn_->setFocusPolicy(Qt::NoFocus);

    // Logarithmic zoom slider mirroring the +/- buttons and Ctrl+wheel.
    zoomSlider_ = new QSlider(Qt::Horizontal, this);
    zoomSlider_->setObjectName(QStringLiteral("zoomSlider"));
    zoomSlider_->setRange(0, kZoomSliderSteps);
    zoomSlider_->setFixedWidth(120);
    zoomSlider_->setToolTip(tr("Zoom"));
    zoomSlider_->setFocusPolicy(Qt::NoFocus);
    zoomSlider_->setValue(zoomToSlider(pxPerFrame_));

    fitBtn_ = new QToolButton(this);
    fitBtn_->setObjectName(QStringLiteral("fitZoomButton"));
    fitBtn_->setText(QStringLiteral("Fit"));
    fitBtn_->setToolTip(tr("Fit timeline to view"));
    fitBtn_->setAutoRaise(true);
    fitBtn_->setFocusPolicy(Qt::NoFocus);

    connect(zoomIn_, &QToolButton::clicked, this, [this] { setZoom(zoom() * 1.2); });
    connect(zoomOut_, &QToolButton::clicked, this, [this] { setZoom(zoom() / 1.2); });
    connect(addVideoBtn_, &QToolButton::clicked, this, [this] { addTrack(true); });
    connect(addAudioBtn_, &QToolButton::clicked, this, [this] { addTrack(false); });
    connect(zoomSlider_, &QSlider::valueChanged, this,
            [this](int value) { setZoom(sliderToZoom(value)); });
    connect(fitBtn_, &QToolButton::clicked, this,
            &TimelinePanel::fitZoomToViewport);
    connect(laneOrderCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const int orderValue = laneOrderCombo_->itemData(index).toInt();
                setLaneOrder(
                    static_cast<timeline_geometry::LaneOrder>(orderValue));
            });
    connect(editModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const int modeValue = editModeCombo_->itemData(index).toInt();
                setEditMode(static_cast<TimelineState::EditMode>(modeValue));
            });
    connect(insertModeBtn_, &QToolButton::toggled, this,
            &TimelinePanel::setInsertMode);
    connect(rippleBtn_, &QToolButton::toggled, this,
            &TimelinePanel::setRippleEnabled);
    connect(avSplitBtn_, &QToolButton::toggled, this,
            &TimelinePanel::setAutoSplitAv);
    connect(zoneInBtn_, &QToolButton::clicked, this,
            [this] { setZoneIn(playhead_); });
    connect(zoneOutBtn_, &QToolButton::clicked, this,
            [this] { setZoneOut(playhead_); });
    connect(clearZoneBtn_, &QToolButton::clicked, this,
            &TimelinePanel::clearZone);

    topBar->addWidget(snapCheck_);
    topBar->addWidget(addVideoBtn_);
    topBar->addWidget(addAudioBtn_);
    topBar->addSpacing(8);
    topBar->addWidget(editModeCombo_);
    topBar->addWidget(insertModeBtn_);
    topBar->addWidget(rippleBtn_);
    topBar->addWidget(avSplitBtn_);
    topBar->addSpacing(8);
    topBar->addWidget(zoneInBtn_);
    topBar->addWidget(zoneOutBtn_);
    topBar->addWidget(clearZoneBtn_);
    topBar->addSpacing(8);
    topBar->addWidget(laneOrderCombo_);
    topBar->addStretch(1);
    topBar->addWidget(zoomOut_);
    topBar->addWidget(zoomSlider_);
    topBar->addWidget(zoomLabel_);
    topBar->addWidget(zoomIn_);
    topBar->addWidget(fitBtn_);

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
        mixer_.reset(controller_->timeline(), controller_->undoStack());
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
    mixer_.reset(controller_->timeline(), controller_->undoStack());
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
    if (!state_.snap) return raw;
    return editor_.snapToNearest(sequence(), raw,
                                 snapToleranceFrames(pxPerFrame_),
                                 dragging);
}

int TimelinePanel::flatAtY(qreal sceneY) const {
    const bl::Sequence& seq = sequence();
    if (sceneY <= timeline_geometry::kRulerHeight) return -1;
    const int vc = static_cast<int>(seq.videoTracks.size());
    const int lanes = trackCount();
    // Walk in visual (lane) order so Mixed layouts hit the right flat track.
    for (int lane = 0; lane < lanes; ++lane) {
        const int flat = timeline_geometry::flatForLane(seq, lane, state_.laneOrder);
        if (flat < 0) continue;
        const qreal top =
            timeline_geometry::laneTop(seq, flat, vc, state_.laneOrder);
        const qreal h = timeline_geometry::laneHeight(seq, flat, vc);
        if (sceneY >= top && sceneY < top + h) return flat;
    }
    return -1;
}

QRectF TimelinePanel::laneRect(int flat) const {
    const bl::Sequence& seq = sequence();
    const int vc = static_cast<int>(seq.videoTracks.size());
    const qreal top = timeline_geometry::laneTop(seq, flat, vc, state_.laneOrder);
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
    if (zoomSlider_) {
        const QSignalBlocker blocker(zoomSlider_);
        zoomSlider_->setValue(zoomToSlider(pxPerFrame_));
    }
    rebuildFromModel();
}

void TimelinePanel::fitZoomToViewport() {
    if (!view_ || !controller_) return;
    const int viewportWidth = view_->viewport()->width();
    if (viewportWidth <= 0) return;
    const bl::Sequence& seq = sequence();
    // contentWidth is linear in pxPerFrame: width = endFrame*px + margin, so
    // one sample at 1px/frame recovers the sequence's frame length.
    const qreal endFrame = timeline_geometry::contentWidth(seq, 1.0) -
                           timeline_geometry::kContentMargin;
    if (endFrame <= 0.0) return;
    const qreal available = static_cast<qreal>(viewportWidth) -
                            timeline_geometry::kContentMargin;
    if (available <= 1.0) return;
    setZoom(available / endFrame);
}

void TimelinePanel::setLaneOrder(timeline_geometry::LaneOrder order) {
    if (state_.laneOrder == order) return;
    state_.laneOrder = order;
    if (laneOrderCombo_) {
        const int index = laneOrderCombo_->findData(static_cast<int>(order));
        if (index >= 0) {
            const QSignalBlocker blocker(laneOrderCombo_);
            laneOrderCombo_->setCurrentIndex(index);
        }
    }
    rebuildFromModel();
    emit laneOrderChanged(state_.laneOrder);
    emit timelineStateChanged();
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
    if (state_.snap == on) return;
    state_.snap = on;
    if (snapCheck_) snapCheck_->setChecked(on);
    emit timelineStateChanged();
}

void TimelinePanel::setTimelineState(const TimelineState& state) {
    setLaneOrder(state.laneOrder);
    setEditMode(state.editMode);
    setInsertMode(state.insertMode);
    setRippleEnabled(state.ripple);
    setAutoSplitAv(state.autoSplitAv);
    setSnapEnabled(state.snap);
}

void TimelinePanel::setEditMode(TimelineState::EditMode mode) {
    if (state_.editMode == mode) return;
    state_.editMode = mode;
    if (editModeCombo_) {
        const int index = editModeCombo_->findData(static_cast<int>(mode));
        if (index >= 0) {
            const QSignalBlocker blocker(editModeCombo_);
            editModeCombo_->setCurrentIndex(index);
        }
    }
    emit timelineStateChanged();
}

void TimelinePanel::setInsertMode(bool on) {
    if (state_.insertMode == on) return;
    state_.insertMode = on;
    if (insertModeBtn_) {
        const QSignalBlocker blocker(insertModeBtn_);
        insertModeBtn_->setChecked(on);
    }
    emit timelineStateChanged();
}

void TimelinePanel::setRippleEnabled(bool on) {
    if (state_.ripple == on) return;
    state_.ripple = on;
    if (rippleBtn_) {
        const QSignalBlocker blocker(rippleBtn_);
        rippleBtn_->setChecked(on);
    }
    emit timelineStateChanged();
}

void TimelinePanel::setAutoSplitAv(bool on) {
    if (state_.autoSplitAv == on) return;
    state_.autoSplitAv = on;
    if (avSplitBtn_) {
        const QSignalBlocker blocker(avSplitBtn_);
        avSplitBtn_->setChecked(on);
    }
    emit timelineStateChanged();
}

void TimelinePanel::setZone(const bl::Time& in, const bl::Time& out) {
    bl::Time lo = in;
    bl::Time hi = out;
    if (hi < lo) std::swap(lo, hi);
    hasZone_ = true;
    zoneIn_ = lo;
    zoneOut_ = hi;
    updateZoneItem();
    emit zoneChanged();
}

void TimelinePanel::clearZone() {
    if (!hasZone_) return;
    hasZone_ = false;
    updateZoneItem();
    emit zoneChanged();
}

void TimelinePanel::setZoneIn(const bl::Time& t) {
    if (t.ticks < 0) return;
    if (!hasZone_) {
        hasZone_ = true;
        zoneIn_ = t;
        zoneOut_ = t;
    } else {
        zoneIn_ = (t > zoneOut_) ? zoneOut_ : t;
    }
    updateZoneItem();
    emit zoneChanged();
}

void TimelinePanel::setZoneOut(const bl::Time& t) {
    if (t.ticks < 0) return;
    if (!hasZone_) {
        hasZone_ = true;
        zoneIn_ = t;
        zoneOut_ = t;
    } else {
        zoneOut_ = (t < zoneIn_) ? zoneIn_ : t;
    }
    updateZoneItem();
    emit zoneChanged();
}

bool TimelinePanel::liftZone() {
    return applyZoneEdit(/*extract=*/false);
}

bool TimelinePanel::extractZone() {
    return applyZoneEdit(/*extract=*/true);
}

void TimelinePanel::updateZoneItem() {
    if (!zoneItem_) return;
    if (!hasZone_) {
        zoneItem_->setVisible(false);
        return;
    }
    const qreal x0 = xForTime(zoneIn_);
    const qreal x1 = xForTime(zoneOut_);
    const qreal height = timeline_geometry::contentHeight(sequence());
    zoneItem_->setRange(x0, std::max<qreal>(x1 - x0, 1.0), 0.0, height);
    zoneItem_->setVisible(true);
}

void TimelinePanel::setSelection(const QSet<bl::ClipId>& ids) {
    // Selecting one half of a linked A/V pair selects the whole group.
    const QSet<bl::ClipId> expanded = expandSelection(ids);
    if (selection_ == expanded) return;
    selection_ = expanded;
    for (auto& entry : clipItems_) {
        entry.second->setSelected(selection_.contains(entry.first));
    }
    emit selectionChanged();
}

void TimelinePanel::clearSelection() { setSelection({}); }

QSet<bl::ClipId> TimelinePanel::groupMembersOf(const bl::ClipId& id) const {
    QSet<bl::ClipId> out;
    for (const bl::ClipId& m : sequence().groupMembers(id)) out.insert(m);
    return out;
}

QSet<bl::ClipId> TimelinePanel::expandSelection(
    const QSet<bl::ClipId>& ids) const {
    QSet<bl::ClipId> out = ids;
    for (const bl::ClipId& id : ids) {
        for (const bl::ClipId& m : sequence().groupMembers(id)) out.insert(m);
    }
    return out;
}

bool TimelinePanel::groupSelection() {
    std::vector<std::pair<int, bl::ClipId>> members;
    for (const auto& entry : clipItems_) {
        if (selection_.contains(entry.first)) {
            members.push_back({entry.second->flatTrack(), entry.first});
        }
    }
    if (members.size() < 2) {
        if (controller_) {
            emit controller_->statusMessage(
                tr("Select at least two clips to group"));
        }
        return false;
    }
    if (!editor_.groupClips(members)) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::ungroupSelection() {
    // Ungroup every group the selection touches. ungroupClips() drops the
    // whole group containing a clip, so one call per group is enough; iterate
    // because a rebuild refreshes clipItems_ after each command.
    bool any = false;
    for (;;) {
        const ClipItem* item = nullptr;
        for (const auto& entry : clipItems_) {
            if (!selection_.contains(entry.first)) continue;
            if (sequence().groupMembers(entry.first).empty()) continue;
            item = entry.second;
            break;
        }
        if (!item) break;
        const int flat = item->flatTrack();
        const bl::ClipId cid = item->clip().id;
        if (!editor_.ungroupClips(flat, cid)) break;
        any = true;
    }
    if (!any) {
        if (controller_) {
            emit controller_->statusMessage(tr("No grouped clips selected"));
        }
        return false;
    }
    return true;
}

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

// --- Subtitle import -------------------------------------------------------

int TimelinePanel::importSubtitleCues(const std::vector<bl::SubtitleCue>& cues) {
    if (!controller_) return 0;
    const int n = editor_.importSubtitleCues(cues, -1);
    if (n <= 0) {
        reportEditorError();
        return 0;
    }
    emit controller_->statusMessage(tr("Imported %1 subtitle(s)").arg(n));
    return n;
}

bool TimelinePanel::selectedClipRange(bl::Time& in, bl::Time& out) const {
    bool found = false;
    bl::Time lo{};
    bl::Time hi{};
    for (const auto& entry : clipItems_) {
        if (!selection_.contains(entry.first)) continue;
        const bl::Clip& c = entry.second->clip();
        const bl::Time start = c.timelineStart;
        const bl::Time end = c.timelineStart + c.timelineDuration;
        if (!found || start < lo) lo = start;
        if (!found || hi < end) hi = end;
        found = true;
    }
    if (!found || !(lo < hi)) return false;
    in = lo;
    out = hi;
    return true;
}

// --- Clipboard (Phase 7) ---------------------------------------------------

int TimelinePanel::copySelection() {
    if (!controller_) return 0;
    std::vector<TimelineEditController::ClipboardEntry> entries;
    for (const auto& entry : clipItems_) {
        if (!selection_.contains(entry.first)) continue;
        entries.push_back({entry.second->flatTrack(), entry.second->clip()});
    }
    if (entries.empty()) {
        emit controller_->statusMessage(tr("Select a clip to copy"));
        return 0;
    }
    const int count = static_cast<int>(entries.size());
    editor_.setClipboard(std::move(entries));
    emit controller_->statusMessage(tr("Copied %1 clip(s)").arg(count));
    return count;
}

int TimelinePanel::cutSelection() {
    if (!controller_) return 0;
    std::vector<std::pair<int, bl::ClipId>> clips;
    std::vector<TimelineEditController::ClipboardEntry> entries;
    for (const auto& entry : clipItems_) {
        if (!selection_.contains(entry.first)) continue;
        clips.push_back({entry.second->flatTrack(), entry.first});
        entries.push_back({entry.second->flatTrack(), entry.second->clip()});
    }
    if (clips.empty()) {
        emit controller_->statusMessage(tr("Select a clip to cut"));
        return 0;
    }
    const int count = static_cast<int>(clips.size());
    editor_.setClipboard(std::move(entries));
    if (!editor_.removeClips(clips, false)) {
        reportEditorError();
        return 0;
    }
    clearSelection();
    emit controller_->statusMessage(tr("Cut %1 clip(s)").arg(count));
    return count;
}

int TimelinePanel::pasteAtPlayhead() { return pasteAt(playhead_); }

int TimelinePanel::pasteAt(const bl::Time& at) {
    if (!controller_) return 0;
    if (!editor_.hasClipboard()) {
        emit controller_->statusMessage(tr("Clipboard is empty"));
        return 0;
    }
    QSet<bl::ClipId> before;
    for (const auto& entry : clipItems_) before.insert(entry.first);
    const int n = editor_.pasteClips(at, activeTrack_);
    if (n <= 0) {
        reportEditorError();
        return 0;
    }
    // Select the freshly pasted clips (ids not present before the paste).
    QSet<bl::ClipId> pasted;
    for (const auto& entry : clipItems_) {
        if (!before.contains(entry.first)) pasted.insert(entry.first);
    }
    if (!pasted.isEmpty()) setSelection(pasted);
    emit controller_->statusMessage(tr("Pasted %1 clip(s)").arg(n));
    return n;
}

// --- Markers (Phase 7) -----------------------------------------------------

bool TimelinePanel::addMarkerAt(const bl::Time& at, const QString& label) {
    if (!controller_) return false;
    bl::Marker m;
    m.position = at;
    m.label = label.isEmpty() ? tr("Marker").toStdString() : label.toStdString();
    m.color = 0xFFCC00u; // amber
    if (!editor_.addTimelineMarker(m)) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::renameMarkerAt(const bl::Time& at, const QString& label) {
    if (!controller_) return false;
    const auto idx = sequence().findMarkerAt(at);
    if (!idx) {
        emit controller_->statusMessage(tr("No marker there"));
        return false;
    }
    bl::Marker revised = sequence().markers[*idx];
    revised.label = label.toStdString();
    if (!editor_.editTimelineMarker(at, revised)) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::removeMarkerAt(const bl::Time& at) {
    if (!controller_) return false;
    if (!editor_.removeTimelineMarker(at)) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::markerNear(const bl::Time& at, int radiusFrames,
                               bl::Time& out) const {
    const bl::Rational fps = sequence().settings.fps;
    bool found = false;
    int64_t best = 0;
    for (const bl::Marker& m : sequence().markers) {
        bl::Duration d = m.position - at;
        if (d < bl::Duration{}) d = -d;
        const int64_t frames = d.toFramesAt(fps);
        if (frames <= radiusFrames && (!found || frames < best)) {
            out = m.position;
            best = frames;
            found = true;
        }
    }
    return found;
}

bool TimelinePanel::addMediaToTimeline(
    const std::string& mediaId,
    std::optional<bl::Duration> durationOverride) {
    if (!controller_ || mediaId.empty()) return false;

    const MediaBinItem* entry = nullptr;
    for (const MediaBinItem& item : controller_->mediaBin()) {
        if (item.id == mediaId) {
            entry = &item;
            break;
        }
    }
    if (!entry) {
        emit controller_->statusMessage(
            tr("Media is no longer in the project"));
        return false;
    }

    auto probe = MediaSource::probe(MediaLocator{entry->path});
    if (!probe) {
        emit controller_->statusMessage(
            tr("Cannot read media: %1")
                .arg(QString::fromStdString(entry->name)));
        return false;
    }
    const bool hasVideo = !probe->videoStreams.empty();
    const bool hasAudio = !probe->audioStreams.empty();
    if (!hasVideo && !hasAudio) {
        emit controller_->statusMessage(
            tr("File has no video or audio streams"));
        return false;
    }
    const bl::Duration mediaDuration =
        durationOverride ? *durationOverride : probe->duration;
    if (mediaDuration.ticks <= 0) {
        emit controller_->statusMessage(tr("Unknown media duration"));
        return false;
    }

    auto clipOpt = makeThreePointClip(
        SourceRef{mediaId, bl::Time{},
                  bl::Time{mediaDuration.ticks, mediaDuration.rate}},
        SpeedRemap{}, std::string());
    if (!clipOpt) {
        emit controller_->statusMessage(tr("Cannot place clip here"));
        return false;
    }
    bl::Clip clip = *clipOpt;

    // First lane whose kind can carry the media: video material goes to the
    // first video lane (V1), audio-only material to the first audio lane.
    const bl::Sequence& seq = sequence();
    const int laneCount = trackCount();
    int lane = -1;
    for (int i = 0; i < laneCount; ++i) {
        const bool videoLane = editor_.isVideoTrack(seq, i);
        if ((videoLane && hasVideo) || (!videoLane && hasAudio)) {
            lane = i;
            break;
        }
    }
    if (lane < 0) {
        emit controller_->statusMessage(
            tr("No compatible track for this media"));
        return false;
    }

    // Append at the end of that lane so the clip never covers existing
    // material (an empty lane starts at 0).
    bl::Time start{0, kTimelineRate};
    if (editor_.isVideoTrack(seq, lane)) {
        for (const bl::Clip& c :
             seq.videoTracks[static_cast<size_t>(lane)].clips()) {
            const bl::Time end = c.timelineStart + c.timelineDuration;
            if (end > start) start = end;
        }
    } else {
        const size_t audioIndex = static_cast<size_t>(
            lane - static_cast<int>(seq.videoTracks.size()));
        for (const bl::Clip& c : seq.audioTracks[audioIndex].clips()) {
            const bl::Time end = c.timelineStart + c.timelineDuration;
            if (end > start) start = end;
        }
    }

    clip.timelineStart = start;
    if (!editor_.canPlaceAt(seq, lane, clip, start)) {
        emit controller_->statusMessage(tr("That track is occupied at its end"));
        return false;
    }
    if (!editor_.addClip(lane, clip)) {
        emit controller_->statusMessage(tr("Cannot place clip here"));
        return false;
    }
    emit controller_->statusMessage(
        QStringLiteral("Added %1").arg(QFileInfo(QString::fromStdString(
                                            entry->path))
                                           .fileName()));
    return true;
}

void TimelinePanel::rebuildFromModel() {
    if (!scene_) return;
    if (view_) view_->cancelActiveGesture();
    scene_->clear();
    clipItems_.clear();
    playheadItem_ = nullptr;
    snapIndicator_ = nullptr;
    zoneItem_ = nullptr;

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
                if (waveformResolver_ && clip.hasAudio) {
                    clipItem->setWaveform(
                        waveformResolver_(clip.source.mediaItemId));
                }
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

    zoneItem_ = new ZoneItem;
    zoneItem_->setRange(0, 1, 0, height);
    zoneItem_->setVisible(hasZone_);
    scene_->addItem(zoneItem_);
    updateZoneItem();

    playheadItem_ = new PlayheadItem;
    playheadItem_->setHeight(height);
    playheadItem_->setPos(xForTime(playhead_), 0);
    scene_->addItem(playheadItem_);

    snapIndicator_ = new SnapIndicatorItem;
    snapIndicator_->setHeight(height);
    scene_->addItem(snapIndicator_);

    // Keep the active track valid after structural edits, then resync the
    // interactive header rows with the (possibly new) model.
    const int tracks = trackCount();
    if (tracks == 0) {
        activeTrack_ = -1;
    } else if (activeTrack_ < 0 || activeTrack_ >= tracks) {
        activeTrack_ = 0;
    }
    if (header_) header_->rebuild();
}

void TimelinePanel::refreshWaveforms() {
    if (!waveformResolver_) return;
    for (auto& [id, item] : clipItems_) {
        (void)id;
        if (!item) continue;
        // Mirror rebuildFromModel: only audio-bearing clips carry a waveform
        // (the video half of an A/V split shares its media id but is silent).
        item->setWaveform(item->clip().hasAudio
                              ? waveformResolver_(item->clip().source.mediaItemId)
                              : nullptr);
        item->update();
    }
}

void TimelinePanel::setActiveTrack(int flat) {
    if (flat < 0 || flat >= trackCount()) return;
    if (activeTrack_ == flat) return;
    activeTrack_ = flat;
    if (header_) header_->rebuild();
    emit activeTrackChanged(activeTrack_);
}

int TimelinePanel::flatAtGlobalY(int globalY) const {
    if (!view_) return -1;
    const QPoint viewportPos = view_->viewport()->mapFromGlobal(QPoint(0, globalY));
    const QPointF scenePos = view_->mapToScene(viewportPos);
    return flatAtY(scenePos.y());
}

void TimelinePanel::reportEditorError() {
    if (!controller_) return;
    const std::string& err = editor_.lastError();
    if (!err.empty()) {
        emit controller_->statusMessage(QString::fromStdString(err));
    }
}

bool TimelinePanel::applyZoneEdit(bool extract) {
    if (!hasZone_) {
        if (controller_) emit controller_->statusMessage(tr("No timeline zone set"));
        return false;
    }
    const bl::Duration dur = zoneOut_ - zoneIn_;
    if (dur.ticks <= 0) {
        if (controller_) emit controller_->statusMessage(tr("Zone is empty"));
        return false;
    }
    const bool ok = extract ? editor_.extractZone(zoneIn_, dur)
                            : editor_.liftZone(zoneIn_, dur);
    if (!ok) reportEditorError();
    return ok;
}

int TimelinePanel::audioLaneFor(int videoFlat, const bl::Clip& clip,
                                const bl::Time& start) const {
    const bl::Sequence& seq = sequence();
    const int vc = static_cast<int>(seq.videoTracks.size());
    const int ac = static_cast<int>(seq.audioTracks.size());
    if (ac <= 0) return -1;
    const auto fits = [&](int flat) {
        if (flat < vc || flat >= vc + ac) return false;
        return editor_.canPlaceAt(seq, flat, clip, start);
    };
    // Prefer the audio lane aligned with the video lane (V1 → A1).
    const int aligned = vc + videoFlat;
    if (fits(aligned)) return aligned;
    for (int a = 0; a < ac; ++a) {
        const int flat = vc + a;
        if (flat != aligned && fits(flat)) return flat;
    }
    return -1;
}

bool TimelinePanel::setTrackLocked(int flat, bool locked) {
    if (!mixer_.valid()) return false;
    return mixer_.setTrackLocked(flat, locked);
}

bool TimelinePanel::setTrackMuted(int flat, bool muted) {
    if (!mixer_.valid()) return false;
    return mixer_.setTrackMuted(flat, muted);
}

bool TimelinePanel::setTrackHidden(int flat, bool hidden) {
    if (!mixer_.valid()) return false;
    return mixer_.setTrackHidden(flat, hidden);
}

bool TimelinePanel::setTrackSoloed(int flat, bool soloed) {
    if (!mixer_.valid()) return false;
    return mixer_.setTrackSoloed(flat, soloed);
}

bool TimelinePanel::renameTrack(int flat, const QString& name) {
    if (!mixer_.valid()) return false;
    return mixer_.setTrackName(flat, name.toStdString());
}

bool TimelinePanel::addTrack(bool video) {
    if (!editor_.valid() || !controller_) return false;
    const bl::Sequence& seq = sequence();
    const size_t index =
        video ? seq.videoTracks.size() : seq.audioTracks.size();
    if (!editor_.insertTrack(video, index, std::string())) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::insertTrackAt(int flat, bool above) {
    if (!editor_.valid() || !controller_) return false;
    const bl::Sequence& seq = sequence();
    if (flat < 0 || flat >= trackCount()) return false;
    const bool video = editor_.isVideoTrack(seq, flat);
    const size_t kindIndex =
        static_cast<size_t>(editor_.kindIndex(seq, flat));
    const size_t index = above ? kindIndex : kindIndex + 1;
    if (!editor_.insertTrack(video, index, std::string())) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::removeTrackAt(int flat) {
    if (!editor_.valid() || !controller_) return false;
    const bl::Sequence& seq = sequence();
    if (flat < 0 || flat >= trackCount()) return false;
    const bool video = editor_.isVideoTrack(seq, flat);
    const size_t kindIndex =
        static_cast<size_t>(editor_.kindIndex(seq, flat));
    if (!editor_.removeTrack(video, kindIndex, false)) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::moveTrackTo(int flat, int targetFlat) {
    if (!editor_.valid() || !controller_) return false;
    const bl::Sequence& seq = sequence();
    if (flat < 0 || targetFlat < 0 || flat >= trackCount() ||
        targetFlat >= trackCount()) {
        return false;
    }
    const bool video = editor_.isVideoTrack(seq, flat);
    if (editor_.isVideoTrack(seq, targetFlat) != video) return false;
    const size_t from = static_cast<size_t>(editor_.kindIndex(seq, flat));
    const size_t to = static_cast<size_t>(editor_.kindIndex(seq, targetFlat));
    if (from == to) return false;
    if (!editor_.moveTrack(video, from, to)) {
        reportEditorError();
        return false;
    }
    return true;
}

bool TimelinePanel::moveTrackUp(int flat) {
    if (!editor_.valid()) return false;
    const bl::Sequence& seq = sequence();
    if (flat < 0 || flat >= trackCount()) return false;
    if (editor_.kindIndex(seq, flat) <= 0) return false;
    return moveTrackTo(flat, flat - 1);
}

bool TimelinePanel::moveTrackDown(int flat) {
    if (!editor_.valid()) return false;
    const bl::Sequence& seq = sequence();
    if (flat < 0 || flat >= trackCount()) return false;
    const bool video = editor_.isVideoTrack(seq, flat);
    const int kindIndex = editor_.kindIndex(seq, flat);
    const int kindCount = video ? static_cast<int>(seq.videoTracks.size())
                                : static_cast<int>(seq.audioTracks.size());
    if (kindIndex + 1 >= kindCount) return false;
    return moveTrackTo(flat, flat + 1);
}

void TimelinePanel::updatePlayheadItem() {
    if (playheadItem_) {
        playheadItem_->setPos(xForTime(playhead_), 0);
    }
}

} // namespace bl::ui