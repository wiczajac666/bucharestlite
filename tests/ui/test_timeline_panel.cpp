#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/title.hpp>

#include <app/project_controller.hpp>
#include <panels/media_bin_panel.hpp>
#include <panels/timeline_panel.hpp>
#include <title/title_renderer.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPoint>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUrl>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

using bl::Clip;
using bl::ClipId;
using bl::Duration;
using bl::Time;
using bl::Sequence;
using bl::Timeline;
using bl::TrackKind;

const bl::Rational kFps{24, 1};
const bl::Rational kRate{1'000'000, 1};

QApplication* ensureApp() {
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char argv0[] = "bl_ui_tests";
        static char* argv[] = {argv0, nullptr};
        return new QApplication(argc, argv);
    }();
    return app;
}

// Fixture takes a QWidget member, so the QApplication must exist before any
// Fixture is constructed — bootstrap it during static initialization.
struct AppBootstrapper {
    AppBootstrapper() { ensureApp(); }
};
const AppBootstrapper kAppBootstrapper{};

Time fr(int64_t frame) { return Time::fromFrameAt(frame, kFps, kRate); }
Duration durF(int64_t frames) { return Duration::fromFrames(frames, kFps); }

Clip makeClip(const ClipId& id, int64_t startFrames, int64_t durFrames) {
    Clip c;
    c.id = id;
    c.name = id;
    c.colorLabel = 0;
    c.source.mediaItemId = "m";
    c.source.sourceIn = fr(0);
    c.source.sourceOut = fr(startFrames + durFrames);
    c.timelineStart = fr(startFrames);
    c.timelineDuration = durF(durFrames);
    return c;
}

struct Fixture {
    bl::ui::ProjectController controller;
    bl::ui::TimelinePanel panel;

    Fixture() : panel(&controller) {
        ensureApp();
        controller.newProject(QStringLiteral("T"));
        panel.resize(8000, 900);
        panel.setZoom(6.0);
        panel.show();
        panel.view()->show();
        QApplication::processEvents();
    }

    ~Fixture() { QApplication::processEvents(); }

    void addClip(int flat, const Clip& clip) {
        if (flat == 0) {
            controller.timeline().sequence().videoTracks[0].addClip(clip);
        } else if (flat == 1) {
            controller.timeline().sequence().audioTracks[0].addClip(clip);
        } else {
            FAIL() << "fixture supports two lanes";
        }
        panel.rebuildFromModel();
    }

    const bl::Sequence& seq() const {
        return controller.timeline().sequence();
    }

    const Clip* find(const ClipId& id) const {
        for (const auto& t : seq().videoTracks) {
            for (const auto& c : t.clips()) {
                if (c.id == id) return &c;
            }
        }
        for (const auto& t : seq().audioTracks) {
            for (const auto& c : t.clips()) {
                if (c.id == id) return &c;
            }
        }
        return nullptr;
    }

    QPoint viewportPos(const QPointF& scenePos) const {
        return panel.view()->mapFromScene(scenePos);
    }

    QPointF clipCenter(int flat, const ClipId& id) const {
        const Clip* clip = find(id);
        EXPECT_NE(clip, nullptr);
        const QRectF lane = panel.laneRect(flat);
        const qreal x = panel.xForTime(clip->timelineStart) +
                        panel.xForTime(clip->timelineStart + clip->timelineDuration) / 2.0 -
                        panel.xForTime(clip->timelineStart) / 2.0 + 3.0;
        return QPointF(x, lane.top() + lane.height() / 2.0);
    }

    qreal sceneX(int64_t frame) const { return panel.xForTime(fr(frame)); }

    void clickAt(int64_t frame, int flat, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        const QRectF lane = panel.laneRect(flat);
        const QPoint p = viewportPos(QPointF(sceneX(frame) + 1.0, lane.top() + lane.height() / 2.0));
        QTest::mouseClick(panel.view()->viewport(), Qt::LeftButton, mods, p);
    }

    void clickOnClip(const ClipId& id, int flat, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        const QPoint p = viewportPos(clipCenter(flat, id));
        QTest::mouseClick(panel.view()->viewport(), Qt::LeftButton, mods, p);
    }
};

int64_t frameOf(Time t) {
    return static_cast<int64_t>(
        std::llround(t.toSeconds() * static_cast<double>(kFps.num) /
                     static_cast<double>(kFps.den)));
}

int64_t frameOf(Duration d) {
    return static_cast<int64_t>(
        std::llround(d.toSeconds() * static_cast<double>(kFps.num) /
                     static_cast<double>(kFps.den)));
}

int64_t clipStart(const Fixture& fx, const ClipId& id) {
    const Clip* c = fx.find(id);
    return c ? frameOf(c->timelineStart) : -1;
}

} // namespace

TEST(TimelinePanel, rendersOneItemPerClipAndMarkers) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 20, 5));
    fx.addClip(1, makeClip("au", 0, 10));

    EXPECT_EQ(fx.panel.clipItemCount(), 3);
    EXPECT_EQ(fx.panel.markerItemCount(), 0);

    fx.controller.timeline().addMarker(bl::Marker(fr(10), "m1"));
    fx.panel.rebuildFromModel();
    EXPECT_EQ(fx.panel.markerItemCount(), 1);
}

TEST(TimelinePanel, clickSelectsAndCtrlClickToggles) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 20, 5));

    fx.clickOnClip("a", 0);
    EXPECT_EQ(fx.panel.selection().size(), 1u);
    EXPECT_TRUE(fx.panel.selection().contains("a"));

    fx.clickOnClip("b", 0, Qt::ControlModifier);
    EXPECT_EQ(fx.panel.selection().size(), 2u);

    fx.clickOnClip("a", 0, Qt::ControlModifier);
    EXPECT_EQ(fx.panel.selection().size(), 1u);
    EXPECT_TRUE(fx.panel.selection().contains("b"));
}

TEST(TimelinePanel, marqueeSelectsIntersectingClips) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 10, 5));
    fx.addClip(0, makeClip("b", 20, 5));
    fx.addClip(0, makeClip("c", 40, 5));
    fx.clickOnClip("c", 0);

    const QRectF lane = fx.panel.laneRect(0);
    const QPoint start = fx.viewportPos(QPointF(2.0, lane.top() + 4.0));
    const QPoint end = fx.viewportPos(QPointF(fx.sceneX(25), lane.bottom() - 4.0));

    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(fx.panel.view()->viewport(), end);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    EXPECT_EQ(fx.panel.selection().size(), 2u);
    EXPECT_TRUE(fx.panel.selection().contains("a"));
    EXPECT_TRUE(fx.panel.selection().contains("b"));
    EXPECT_FALSE(fx.panel.selection().contains("c"));
}

TEST(TimelinePanel, plainClickOnEmptyClearsSelection) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.clickOnClip("a", 0);
    ASSERT_EQ(fx.panel.selection().size(), 1u);

    const QRectF lane = fx.panel.laneRect(0);
    const QPoint empty = fx.viewportPos(QPointF(fx.sceneX(10), lane.top() + lane.height() / 2.0));
    QTest::mouseClick(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, empty);

    EXPECT_TRUE(fx.panel.selection().isEmpty());
}

TEST(TimelinePanel, dragMovesClipForwardAndBack) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 20, 5));

    const QRectF lane = fx.panel.laneRect(0);
    const qreal y = lane.top() + lane.height() / 2.0;
    const QPoint press = fx.viewportPos(QPointF(fx.sceneX(2), y));
    const QPoint mid = fx.viewportPos(QPointF(fx.sceneX(6), y));
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(12), y));

    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), mid);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, release);

    EXPECT_EQ(clipStart(fx, "a"), 10);
    EXPECT_EQ(clipStart(fx, "b"), 20);

    fx.controller.undoStack().undo();
    EXPECT_EQ(clipStart(fx, "a"), 0);
}

TEST(TimelinePanel, dragOntoAudioLaneMovesAndUndoes) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(1, makeClip("au", 60, 30));

    const QRectF srcLane = fx.panel.laneRect(0);
    const QRectF dstLane = fx.panel.laneRect(1);
    const QPoint press = fx.viewportPos(QPointF(fx.sceneX(2), srcLane.top() + srcLane.height() / 2.0));
    // Target lane overlaps nothing (au starts at frame 60).
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(30), dstLane.top() + dstLane.height() / 2.0));

    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, release);

    const bool stillInVideo =
        std::any_of(fx.seq().videoTracks[0].clips().begin(),
                    fx.seq().videoTracks[0].clips().end(),
                    [](const Clip& c) { return c.id == "a"; });
    EXPECT_FALSE(stillInVideo);
    const Clip* moved = fx.find("a");
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(frameOf(moved->timelineStart), 28);
    const bool inAudio =
        std::any_of(fx.seq().audioTracks[0].clips().begin(),
                    fx.seq().audioTracks[0].clips().end(),
                    [](const Clip& c) { return c.id == "a"; });
    EXPECT_TRUE(inAudio);

    fx.controller.undoStack().undo();
    const Clip* restored = fx.find("a");
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(frameOf(restored->timelineStart), 0);
}

TEST(TimelinePanel, trimRightEdgeUndoable) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 20, 5));

    const QRectF lane = fx.panel.laneRect(0);
    const qreal y = lane.top() + lane.height() / 2.0;
    const qreal xRight = fx.sceneX(0) + fx.sceneX(5) - fx.sceneX(0);
    const QPoint press = fx.viewportPos(QPointF(xRight - 2.0, y));
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(3) + 2.0, y));

    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, release);

    const Clip* a = fx.find("a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(frameOf(a->timelineStart + a->timelineDuration), 3);
    EXPECT_EQ(clipStart(fx, "b"), 20);

    fx.controller.undoStack().undo();
    const Clip* back = fx.find("a");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(frameOf(back->timelineStart + back->timelineDuration), 5);
}

TEST(TimelinePanel, deleteLeavesGapBackspaceRipples) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 5, 5));
    fx.addClip(0, makeClip("c", 20, 5));

    fx.clickOnClip("a", 0);
    fx.panel.view()->setFocus();
    QTest::keyClick(fx.panel.view()->viewport(), Qt::Key_Delete);
    EXPECT_EQ(clipStart(fx, "b"), 5);
    EXPECT_EQ(clipStart(fx, "c"), 20);

    fx.controller.undoStack().undo();
    EXPECT_EQ(clipStart(fx, "a"), 0);

    fx.clickOnClip("a", 0);
    QTest::keyClick(fx.panel.view()->viewport(), Qt::Key_Backspace);
    EXPECT_EQ(clipStart(fx, "b"), 0);
    EXPECT_EQ(clipStart(fx, "c"), 15);

    fx.controller.undoStack().undo();
    EXPECT_EQ(clipStart(fx, "a"), 0);
    EXPECT_EQ(clipStart(fx, "b"), 5);
}

TEST(TimelinePanel, splitKeyAtPlayheadSplitsSelectedClip) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 10));
    fx.clickOnClip("a", 0);
    fx.panel.setPlayhead(fr(4));

    fx.panel.view()->setFocus();
    QTest::keyClick(fx.panel.view(), Qt::Key_S);

    const auto& track = fx.seq().videoTracks[0].clips();
    ASSERT_EQ(track.size(), 2u);
    const auto left = std::min_element(track.begin(), track.end(),
                                       [](const Clip& x, const Clip& y) {
                                           return x.timelineStart < y.timelineStart;
                                       });
    const auto right = std::max_element(track.begin(), track.end(),
                                        [](const Clip& x, const Clip& y) {
                                            return x.timelineStart < y.timelineStart;
                                        });
    ASSERT_NE(left, track.end());
    ASSERT_NE(right, track.end());
    EXPECT_EQ(frameOf(left->timelineStart), 0);
    EXPECT_EQ(frameOf(left->timelineDuration), 4);
    EXPECT_EQ(frameOf(right->timelineStart), 4);
    EXPECT_EQ(frameOf(right->timelineDuration), 6);
}

TEST(TimelinePanel, rulerDragScrubsPlayhead) {
    Fixture fx;
    const QPoint start = fx.viewportPos(QPointF(fx.sceneX(10), 5.0));
    const QPoint end = fx.viewportPos(QPointF(fx.sceneX(15), 5.0));

    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(fx.panel.view()->viewport(), end);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    EXPECT_EQ(frameOf(fx.panel.playhead()), 15);
}

TEST(TimelinePanel, zoomClampsToRangeAndRebuilds) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));

    fx.panel.setZoom(100.0);
    EXPECT_EQ(fx.panel.zoom(), 64.0);
    EXPECT_EQ(fx.panel.clipItemCount(), 1);

    fx.panel.setZoom(0.001);
    EXPECT_EQ(fx.panel.zoom(), 0.25);
    EXPECT_EQ(fx.panel.clipItemCount(), 1);
}

TEST(TimelinePanel, undoRedoCommandsRefreshScene) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.clickOnClip("a", 0);
    fx.panel.view()->setFocus();
    QTest::keyClick(fx.panel.view()->viewport(), Qt::Key_Delete);

    ASSERT_EQ(fx.panel.clipItemCount(), 0);
    fx.controller.undoStack().undo();
    EXPECT_EQ(fx.panel.clipItemCount(), 1);
    EXPECT_TRUE(fx.panel.selection().isEmpty());
}

// ---------------------------------------------------------------------------
// Media bin → timeline drag-and-drop.
// ---------------------------------------------------------------------------

namespace {

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

std::string addMediaToBin(Fixture& f, const char* name) {
    f.controller.addToMediaBin(
        QString::fromLatin1(BL_TEST_MEDIA_DIR "/") + QString::fromLatin1(name));
    QApplication::processEvents();
    return f.controller.mediaBin().back().id;
}

void fillMediaMime(QMimeData& mime, const std::string& mediaId) {
    mime.setData(QString::fromLatin1(bl::ui::kMediaBinMime),
                 QString::fromStdString(mediaId).toUtf8());
}

QPoint lanePoint(const Fixture& f, int flat, int64_t frame) {
    const QRectF lane = f.panel.laneRect(flat);
    const QPointF scene(f.panel.xForTime(fr(frame)), lane.center().y());
    return f.panel.view()->mapFromScene(scene);
}

QGraphicsRectItem* findDropGhost(QGraphicsView* view) {
    for (QGraphicsItem* item : view->scene()->items()) {
        if (item->data(0).toString() ==
            QStringLiteral("timelineDropGhost")) {
            return qgraphicsitem_cast<QGraphicsRectItem*>(item);
        }
    }
    return nullptr;
}

} // namespace

TEST(TimelinePanel, DropMediaAddsClipUndoably) {
    Fixture f;
    const std::string mediaId = addMediaToBin(f, "test_av.mp4");
    QWidget* target = f.panel.view()->viewport();

    const QPoint pos = lanePoint(f, 0, 10);
    QMimeData mime;
    fillMediaMime(mime, mediaId);

    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());

    QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_TRUE(move.isAccepted());
    EXPECT_NE(findDropGhost(f.panel.view()), nullptr);

    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton,
                    Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);
    EXPECT_TRUE(drop.isAccepted());

    const auto& track = f.seq().videoTracks[0];
    ASSERT_EQ(track.clips().size(), 1u);
    const bl::Clip& clip = track.clips().front();
    EXPECT_EQ(clip.source.mediaItemId, mediaId);
    EXPECT_EQ(frameOf(clip.timelineStart), 10);
    // Whole-source drop: 2s of test_av at 24fps = 48 frames.
    EXPECT_EQ(frameOf(clip.timelineDuration), 48);
    EXPECT_EQ(frameOf(clip.source.sourceOut), 48);

    f.controller.undoStack().undo();
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 0u);
    f.controller.undoStack().redo();
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 1u);
}

TEST(TimelinePanel, DropOnOverlappingSlotIsRejected) {
    Fixture f;
    f.addClip(0, makeClip("existing", 0, 10));
    const std::string mediaId = addMediaToBin(f, "test_av.mp4");
    QWidget* target = f.panel.view()->viewport();

    QStringList messages;
    QObject context;
    QObject::connect(&f.controller, &bl::ui::ProjectController::statusMessage,
                     &context, [&messages](const QString& m) {
                         messages.push_back(m);
                     });

    const QPoint pos = lanePoint(f, 0, 5);
    QMimeData mime;
    fillMediaMime(mime, mediaId);
    const size_t commandCountBefore = f.controller.undoStack().count();

    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());

    QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_FALSE(move.isAccepted());

    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton,
                    Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);

    ASSERT_EQ(f.seq().videoTracks[0].clips().size(), 1u);
    EXPECT_EQ(f.seq().videoTracks[0].clips().front().id,
              QStringLiteral("existing").toStdString());
    EXPECT_EQ(f.controller.undoStack().count(), commandCountBefore);
    EXPECT_FALSE(messages.isEmpty());
}

TEST(TimelinePanel, DropAudioMediaRoutesToAudioLane) {
    Fixture f;
    const std::string mediaId = addMediaToBin(f, "test_audio.wav");
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    fillMediaMime(mime, mediaId);

    // Video lane: audio-only source has no video stream → rejected.
    const QPoint videoPos = lanePoint(f, 0, 5);
    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDragMoveEvent rejectMove(videoPos, Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier,
                              QEvent::DragMove);
    QApplication::sendEvent(target, &rejectMove);
    EXPECT_FALSE(rejectMove.isAccepted());
    QDropEvent videoDrop(QPointF(videoPos), Qt::CopyAction, &mime,
                         Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &videoDrop);
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 0u);

    // A fresh drag session begins for the audio-lane landing.
    QDragEnterEvent enterAgain(QPoint(), Qt::CopyAction, &mime,
                               Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enterAgain);
    EXPECT_TRUE(enterAgain.isAccepted());

    // Audio lane: accepted, 1s of WAV = 24 frames at 24fps.
    const QPoint audioPos = lanePoint(f, 1, 3);
    QDragMoveEvent acceptMove(audioPos, Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &acceptMove);
    EXPECT_TRUE(acceptMove.isAccepted());
    QDropEvent audioDrop(QPointF(audioPos), Qt::CopyAction, &mime,
                         Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &audioDrop);

    const auto& track = f.seq().audioTracks[0];
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips().front().source.mediaItemId, mediaId);
    EXPECT_EQ(frameOf(track.clips().front().timelineStart), 3);
    EXPECT_EQ(frameOf(track.clips().front().timelineDuration), 24);

    f.controller.undoStack().undo();
    EXPECT_EQ(f.seq().audioTracks[0].clips().size(), 0u);
}

TEST(TimelinePanel, DropGhostTracksCursorAndClearsOnLeave) {
    Fixture f;
    const std::string mediaId = addMediaToBin(f, "test_av.mp4");
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    fillMediaMime(mime, mediaId);

    const QPoint pos = lanePoint(f, 0, 10);
    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_TRUE(move.isAccepted());

    QGraphicsRectItem* ghost = findDropGhost(f.panel.view());
    ASSERT_NE(ghost, nullptr);
    EXPECT_TRUE(ghost->isVisible());
    EXPECT_NEAR(ghost->rect().x(), f.panel.xForTime(fr(10)), 0.5);
    EXPECT_NEAR(ghost->rect().top(), f.panel.laneRect(0).top(), 0.5);

    QDragLeaveEvent leave;
    QApplication::sendEvent(target, &leave);
    EXPECT_EQ(findDropGhost(f.panel.view()), nullptr);
}

TEST(TimelinePanel, DragEnterRejectsForeignMime) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    mime.setText(QStringLiteral("hello"));

    QDragEnterEvent enter(f.panel.view()->mapFromScene(QPointF(0, 0)),
                          Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_FALSE(enter.isAccepted());
}

// ---------------------------------------------------------------------------
// External media import: files dropped onto the timeline from the file
// manager. These import into the media bin AND place a clip.
// ---------------------------------------------------------------------------

void fillUrlMime(QMimeData& mime, const QStringList& paths) {
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) {
        urls.push_back(QUrl::fromLocalFile(QString::fromLatin1(
            BL_TEST_MEDIA_DIR "/") + path));
    }
    mime.setUrls(urls);
}

TEST(TimelinePanel, DropExternalFileImportsAndAddsClip) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    // Nothing is in the bin yet: the drop has to import as well as place.
    ASSERT_EQ(f.controller.mediaBin().size(), 0u);

    QMimeData mime;
    fillUrlMime(mime, {QStringLiteral("test_av.mp4")});

    const QPoint pos = lanePoint(f, 0, 10);
    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());

    QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_TRUE(move.isAccepted());
    EXPECT_NE(findDropGhost(f.panel.view()), nullptr);

    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton,
                    Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);
    QApplication::processEvents();

    // Imported into the bin...
    ASSERT_EQ(f.controller.mediaBin().size(), 1u);
    const std::string importedId = f.controller.mediaBin().front().id;

    // ...and placed on the timeline pointing at that import.
    const auto& track = f.seq().videoTracks[0];
    ASSERT_EQ(track.clips().size(), 1u);
    const bl::Clip& clip = track.clips().front();
    EXPECT_EQ(clip.source.mediaItemId, importedId);
    EXPECT_EQ(frameOf(clip.timelineStart), 10);
    EXPECT_EQ(frameOf(clip.timelineDuration), 48);

    // One Ctrl+Z reverts the import and the clip together.
    f.controller.undoStack().undo();
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 0u);
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
}

TEST(TimelinePanel, DropExternalAudioFileRoutesToAudioLane) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    fillUrlMime(mime, {QStringLiteral("test_audio.wav")});

    // Video lane: audio-only source has no video stream → rejected.
    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDragMoveEvent rejectMove(lanePoint(f, 0, 5), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &rejectMove);
    EXPECT_FALSE(rejectMove.isAccepted());

    QDragEnterEvent enterAgain(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                               Qt::NoModifier);
    QApplication::sendEvent(target, &enterAgain);
    QDragMoveEvent acceptMove(lanePoint(f, 1, 3), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &acceptMove);
    EXPECT_TRUE(acceptMove.isAccepted());
    QDropEvent drop(QPointF(lanePoint(f, 1, 3)), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);
    QApplication::processEvents();

    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 0u);
    const auto& audio = f.seq().audioTracks[0];
    ASSERT_EQ(audio.clips().size(), 1u);
    EXPECT_EQ(frameOf(audio.clips().front().timelineStart), 3);
    ASSERT_EQ(f.controller.mediaBin().size(), 1u);
    EXPECT_EQ(audio.clips().front().source.mediaItemId,
              f.controller.mediaBin().front().id);
}

TEST(TimelinePanel, DropMultipleExternalFilesPlacesThemInSequence) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    fillUrlMime(mime, {QStringLiteral("test_av.mp4"),
                       QStringLiteral("test_audio.wav")});

    const QPoint pos = lanePoint(f, 0, 10);
    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_TRUE(move.isAccepted());

    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton,
                    Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);
    QApplication::processEvents();

    // Both files imported, each with its own media-bin id.
    ASSERT_EQ(f.controller.mediaBin().size(), 2u);
    // test_av.mp4 (48 frames) lands on the video lane under the cursor, and
    // test_audio.wav (24 frames) is placed after it, routed to the audio lane
    // because an audio-only source cannot sit on a video track.
    ASSERT_EQ(f.seq().videoTracks[0].clips().size(), 1u);
    ASSERT_EQ(f.seq().audioTracks[0].clips().size(), 1u);
    EXPECT_EQ(frameOf(f.seq().videoTracks[0].clips().front().timelineStart), 10);
    EXPECT_EQ(frameOf(f.seq().audioTracks[0].clips().front().timelineStart), 58);

    // A single undo reverts the whole multi-file drop.
    f.controller.undoStack().undo();
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 0u);
    EXPECT_EQ(f.seq().audioTracks[0].clips().size(), 0u);
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
}

TEST(TimelinePanel, DropExternalFileReusingKnownMediaAddsNoSecondBinEntry) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    const std::string mediaId = addMediaToBin(f, "test_av.mp4");
    ASSERT_EQ(f.controller.mediaBin().size(), 1u);

    QMimeData mime;
    fillUrlMime(mime, {QStringLiteral("test_av.mp4")});
    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDragMoveEvent move(lanePoint(f, 0, 0), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_TRUE(move.isAccepted());
    QDropEvent drop(QPointF(lanePoint(f, 0, 0)), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);
    QApplication::processEvents();

    // The file was already in the bin, so it is reused rather than duplicated.
    EXPECT_EQ(f.controller.mediaBin().size(), 1u);
    ASSERT_EQ(f.seq().videoTracks[0].clips().size(), 1u);
    EXPECT_EQ(f.seq().videoTracks[0].clips().front().source.mediaItemId,
              mediaId);
}

TEST(TimelinePanel, DropExternalFileOutsideALaneIsRejected) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    fillUrlMime(mime, {QStringLiteral("test_av.mp4")});

    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());

    // Far above the first lane: no valid placement, so nothing is imported.
    const QPoint outside = f.panel.view()->mapFromScene(
        QPointF(f.panel.xForTime(fr(0)), -500.0));
    QDragMoveEvent move(outside, Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_FALSE(move.isAccepted());

    QDropEvent drop(QPointF(outside), Qt::CopyAction, &mime, Qt::LeftButton,
                    Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(target, &drop);
    QApplication::processEvents();
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 0u);
}

TEST(TimelinePanel, DragLeaveCancelsExternalFileDropWithoutImporting) {
    Fixture f;
    QWidget* target = f.panel.view()->viewport();
    QMimeData mime;
    fillUrlMime(mime, {QStringLiteral("test_av.mp4")});

    QDragEnterEvent enter(QPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDragMoveEvent move(lanePoint(f, 0, 10), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier, QEvent::DragMove);
    QApplication::sendEvent(target, &move);
    EXPECT_NE(findDropGhost(f.panel.view()), nullptr);

    // Abandoning the drag must not leave a stray media-bin entry behind.
    QDragLeaveEvent leave;
    QApplication::sendEvent(target, &leave);
    EXPECT_EQ(findDropGhost(f.panel.view()), nullptr);
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
}
// ---------------------------------------------------------------------------
// Add to Timeline without a drag (context menu / double-click entry point).
// ---------------------------------------------------------------------------

static const bl::Clip* findMediaClip(const bl::Sequence& seq,
                                     const std::string& mediaId) {
    for (const auto& t : seq.videoTracks) {
        for (const auto& c : t.clips()) {
            if (c.source.mediaItemId == mediaId) return &c;
        }
    }
    for (const auto& t : seq.audioTracks) {
        for (const auto& c : t.clips()) {
            if (c.source.mediaItemId == mediaId) return &c;
        }
    }
    return nullptr;
}

TEST(TimelinePanel, AddMediaToTimelineAppendsAtLaneEndUndoably) {
    Fixture f;
    const std::string mediaId = addMediaToBin(f, "test_av.mp4");
    // Occupy the start of V1 so "append" has somewhere to go.
    f.addClip(0, makeClip("occupied", 0, 48));

    EXPECT_TRUE(f.panel.addMediaToTimeline(mediaId));

    const bl::Sequence& seq = f.seq();
    ASSERT_EQ(seq.videoTracks[0].clips().size(), 2u);
    const bl::Clip* added = findMediaClip(seq, mediaId);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(frameOf(added->timelineStart), 48);  // right after the occupant
    EXPECT_EQ(f.panel.clipItemCount(), 2);

    // One undoable command reverts the append.
    f.controller.undoStack().undo();
    QApplication::processEvents();
    EXPECT_EQ(f.seq().videoTracks[0].clips().size(), 1u);
    EXPECT_EQ(f.panel.clipItemCount(), 1);
}

TEST(TimelinePanel, AddMediaToTimelineEmptyLaneStartsAtZero) {
    Fixture f;
    const std::string mediaId = addMediaToBin(f, "test_av.mp4");

    EXPECT_TRUE(f.panel.addMediaToTimeline(mediaId));

    const bl::Clip* added = findMediaClip(f.seq(), mediaId);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(frameOf(added->timelineStart), 0);
}

TEST(TimelinePanel, AddMediaToTimelineRoutesAudioOnlyToAudioLane) {
    Fixture f;
    const std::string mediaId = addMediaToBin(f, "test_audio.wav");

    EXPECT_TRUE(f.panel.addMediaToTimeline(mediaId));

    const bl::Sequence& seq = f.seq();
    EXPECT_EQ(seq.videoTracks[0].clips().size(), 0u);
    ASSERT_EQ(seq.audioTracks[0].clips().size(), 1u);
    EXPECT_EQ(seq.audioTracks[0].clips().front().source.mediaItemId, mediaId);
}

TEST(TimelinePanel, AddMediaToTimelineUnknownIdReturnsFalse) {
    Fixture f;
    EXPECT_FALSE(f.panel.addMediaToTimeline("does-not-exist"));
    EXPECT_EQ(f.panel.clipItemCount(), 0);
}

TEST(TimelinePanel, AddMediaToTimelineUnreadableMediaReturnsFalse) {
    Fixture f;
    f.controller.addToMediaBin(QStringLiteral("/media/not_a_real_file.mp4"));
    QApplication::processEvents();
    ASSERT_EQ(f.controller.mediaBin().size(), 1u);

    EXPECT_FALSE(
        f.panel.addMediaToTimeline(f.controller.mediaBin().back().id));
    EXPECT_EQ(f.panel.clipItemCount(), 0);
}

TEST(TimelinePanel, AddTitleUsesDurationOverrideForStillImage) {
    Fixture f;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString png = QDir(dir.path()).filePath(QStringLiteral("title.png"));

    bl::TitleSpec spec;
    spec.text = "Hello";
    ASSERT_TRUE(bl::ui::saveTitlePng(spec, 320, 180, png));

    ASSERT_TRUE(f.controller.addTitleToMediaBin(
        png, QStringLiteral("Hello"), nlohmann::json(spec)));
    QApplication::processEvents();
    ASSERT_EQ(f.controller.mediaBin().size(), 1u);
    const std::string id = f.controller.mediaBin().back().id;

    // A still has no meaningful stream duration; the override sets the length.
    const bl::Duration threeSeconds = bl::Duration::fromSeconds(3.0, kRate);
    EXPECT_TRUE(f.panel.addMediaToTimeline(id, threeSeconds));

    const bl::Clip* added = findMediaClip(f.seq(), id);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(frameOf(added->timelineStart), 0);
    EXPECT_EQ(frameOf(added->timelineDuration), 72);  // 3s at 24 fps
}

// ---------------------------------------------------------------------------
// P1 #13: waveform display — the panel resolves each audio clip's envelope
// through its resolver and hands it to the ClipItem for painting.
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<const bl::AudioMeterEnvelope> makeEnvelope(size_t windows,
                                                           float peak) {
    bl::AudioMeterEnvelope env;
    env.sampleRate = 48000;
    env.channels = 1;
    env.windowHz = 60.0;
    env.peak.assign(windows, std::vector<float>(1, peak));
    env.rms.assign(windows, std::vector<float>(1, peak * 0.7071f));
    return std::make_shared<const bl::AudioMeterEnvelope>(std::move(env));
}

} // namespace

TEST(TimelinePanel, WaveformResolverAttachesEnvelopeToAudioClips) {
    Fixture f;
    Clip audio = makeClip("au", 0, 10);
    audio.hasAudio = true;
    f.addClip(1, audio);

    const auto env = makeEnvelope(10, 0.5f);
    f.panel.setWaveformResolver(
        [env](const std::string& id)
            -> std::shared_ptr<const bl::AudioMeterEnvelope> {
            return id == "m" ? env : nullptr;
        });

    bl::ui::ClipItem* item = f.panel.clipItemFor("au");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->waveform() != nullptr);
    EXPECT_EQ(item->waveform()->windowCount(), 10u);
}

TEST(TimelinePanel, WaveformNotAttachedToVideoOnlyClip) {
    Fixture f;
    // The video half of an A/V split shares its media id but is inaudible.
    Clip videoOnly = makeClip("v", 0, 10);
    videoOnly.hasAudio = false;
    f.addClip(1, videoOnly);

    const auto env = makeEnvelope(10, 0.5f);
    f.panel.setWaveformResolver(
        [env](const std::string&)
            -> std::shared_ptr<const bl::AudioMeterEnvelope> {
            return env;  // media has audio; the clip flag must still veto it
        });

    bl::ui::ClipItem* item = f.panel.clipItemFor("v");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->waveform(), nullptr);
}

TEST(TimelinePanel, RefreshWaveformsPicksUpLateAnalysis) {
    Fixture f;
    Clip audio = makeClip("au", 0, 10);
    audio.hasAudio = true;
    f.addClip(1, audio);

    const auto env = makeEnvelope(5, 0.4f);
    bool ready = false;
    f.panel.setWaveformResolver(
        [env, &ready](const std::string& id)
            -> std::shared_ptr<const bl::AudioMeterEnvelope> {
            return (ready && id == "m") ? env : nullptr;
        });

    bl::ui::ClipItem* item = f.panel.clipItemFor("au");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->waveform(), nullptr);  // analysis has not settled yet

    ready = true;
    f.panel.refreshWaveforms();
    ASSERT_TRUE(item->waveform() != nullptr);
    EXPECT_EQ(item->waveform()->windowCount(), 5u);
}

// ---------------------------------------------------------------------------
// P1: Kdenlive-parity lane ordering — every order round-trips between flat
// indices and lane numbers, and Split is identical to the legacy layout.
// ---------------------------------------------------------------------------

namespace tg = bl::ui::timeline_geometry;

bl::Sequence laneOrderSequence(int videoCount, int audioCount) {
    bl::Sequence seq;
    for (int i = 0; i < videoCount; ++i)
        seq.addVideoTrack("V" + std::to_string(i + 1));
    for (int i = 0; i < audioCount; ++i)
        seq.addAudioTrack("A" + std::to_string(i + 1));
    return seq;
}

TEST(TimelineLaneOrder, SplitMatchesLegacyLayout) {
    const Sequence seq = laneOrderSequence(2, 2);
    for (int flat = 0; flat < 4; ++flat) {
        EXPECT_EQ(tg::laneForFlat(seq, flat, tg::LaneOrder::Split),
                  tg::laneForFlat(seq, flat));
        EXPECT_EQ(tg::laneTop(seq, flat, 2, tg::LaneOrder::Split),
                  tg::laneTop(seq, flat, 2));
    }
    EXPECT_EQ(tg::lanePermutation(2, 2, tg::LaneOrder::Split),
              std::vector<int>({0, 1, 2, 3}));
}

TEST(TimelineLaneOrder, LanePermutationForEveryOrder) {
    for (const auto order :
         {tg::LaneOrder::Split, tg::LaneOrder::Mixed, tg::LaneOrder::MixedReverse}) {
        const auto perm = tg::lanePermutation(3, 3, order);
        ASSERT_EQ(perm.size(), 6u);
        std::vector<bool> seen(6, false);
        for (const int v : perm) {
            ASSERT_GE(v, 0);
            ASSERT_LT(v, 6);
            EXPECT_FALSE(seen[static_cast<size_t>(v)])
                << "duplicate lane in permutation";
            seen[static_cast<size_t>(v)] = true;
        }
    }
}

TEST(TimelineLaneOrder, FlatAndLaneRoundTripForEveryOrder) {
    const Sequence seq = laneOrderSequence(2, 3); // 5 flat tracks
    for (const auto order :
         {tg::LaneOrder::Split, tg::LaneOrder::Mixed, tg::LaneOrder::MixedReverse}) {
        const auto perm = tg::lanePermutation(2, 3, order);
        ASSERT_EQ(perm.size(), 5u);
        for (size_t lane = 0; lane < perm.size(); ++lane) {
            const int flat = perm[lane];
            EXPECT_EQ(tg::laneForFlat(seq, flat, order),
                      static_cast<int>(lane));
            EXPECT_EQ(tg::flatForLane(seq, static_cast<int>(lane), order),
                      flat);
        }
        // Out-of-range inputs map to -1.
        EXPECT_EQ(tg::laneForFlat(seq, 5, order), -1);
        EXPECT_EQ(tg::flatForLane(seq, 5, order), -1);
    }
}

TEST(TimelineLaneOrder, LaneTopFollowsPermutationOrder) {
    const Sequence seq = laneOrderSequence(2, 2);
    for (const auto order :
         {tg::LaneOrder::Split, tg::LaneOrder::Mixed, tg::LaneOrder::MixedReverse}) {
        const auto perm = tg::lanePermutation(2, 2, order);
        double prev = -1.0;
        for (const int flat : perm) {
            const double top = tg::laneTop(seq, flat, 2, order);
            EXPECT_GT(top, prev) << "lane tops must descend the widget";
            prev = top;
        }
        // Heights are per-kind, independent of the order.
        EXPECT_EQ(tg::laneHeight(seq, 0, 2), tg::kVideoLaneHeight);
        EXPECT_EQ(tg::laneHeight(seq, 2, 2), tg::kAudioLaneHeight);
    }
}

// ---------------------------------------------------------------------------
// Phase 2: interactive track headers.
// ---------------------------------------------------------------------------

namespace {

bl::ui::TrackHeaderRow* headerRow(Fixture& fx, int flat) {
    return fx.panel.header() ? fx.panel.header()->rowFor(flat) : nullptr;
}

QToolButton* rowButton(bl::ui::TrackHeaderRow* row, const char* name) {
    return row ? row->findChild<QToolButton*>(QString::fromLatin1(name)) : nullptr;
}

QLabel* rowLabel(bl::ui::TrackHeaderRow* row) {
    return row ? row->findChild<QLabel*>(QStringLiteral("trackNameLabel")) : nullptr;
}

} // namespace

TEST(TimelineTrackHeader, HasOneRowPerLaneAlignedToLanes) {
    Fixture fx;
    auto* header = fx.panel.header();
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(fx.panel.trackCount(), 2); // default project: V1 + A1

    auto* v = headerRow(fx, 0);
    auto* a = headerRow(fx, 1);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(a, nullptr);

    const int vc = fx.panel.videoCount();
    EXPECT_EQ(v->y(), static_cast<int>(std::lround(
                          tg::laneTop(fx.seq(), 0, vc))));
    EXPECT_EQ(a->y(), static_cast<int>(std::lround(
                          tg::laneTop(fx.seq(), 1, vc))));

    // Scroll offset shifts every row by the same amount.
    header->setScrollOffset(10);
    EXPECT_EQ(v->y(), static_cast<int>(std::lround(
                          tg::laneTop(fx.seq(), 0, vc))) - 10);
    header->setScrollOffset(0);

    // Zoom rebuilds the rows and must not drift them off the lanes.
    fx.panel.setZoom(12.0);
    EXPECT_EQ(headerRow(fx, 0)->y(), static_cast<int>(std::lround(
                                        tg::laneTop(fx.seq(), 0, vc))));
    EXPECT_EQ(headerRow(fx, 1)->y(), static_cast<int>(std::lround(
                                        tg::laneTop(fx.seq(), 1, vc))));
}

TEST(TimelineTrackHeader, LockButtonIsUndoable) {
    Fixture fx;
    auto* lock = rowButton(headerRow(fx, 0), "trackLockButton");
    ASSERT_NE(lock, nullptr);

    lock->click();
    EXPECT_TRUE(fx.seq().videoTracks[0].locked());

    fx.controller.undoStack().undo();
    EXPECT_FALSE(fx.seq().videoTracks[0].locked());
}

TEST(TimelineTrackHeader, VideoHideButtonIsUndoable) {
    Fixture fx;
    auto* hide = rowButton(headerRow(fx, 0), "trackAvButton");
    ASSERT_NE(hide, nullptr);
    EXPECT_EQ(hide->text(), QStringLiteral("H"));

    hide->click();
    EXPECT_TRUE(fx.seq().videoTracks[0].hidden());

    fx.controller.undoStack().undo();
    EXPECT_FALSE(fx.seq().videoTracks[0].hidden());
}

TEST(TimelineTrackHeader, AudioMuteAndSoloAreUndoable) {
    Fixture fx;
    ASSERT_NE(headerRow(fx, 1), nullptr);

    rowButton(headerRow(fx, 1), "trackAvButton")->click();
    EXPECT_TRUE(fx.seq().audioTracks[0].muted());

    rowButton(headerRow(fx, 1), "trackSoloButton")->click();
    EXPECT_TRUE(fx.seq().audioTracks[0].soloed());

    fx.controller.undoStack().undo();
    EXPECT_FALSE(fx.seq().audioTracks[0].soloed());
    fx.controller.undoStack().undo();
    EXPECT_FALSE(fx.seq().audioTracks[0].muted());
}

TEST(TimelineTrackHeader, RenameUpdatesModelAndLabelUndoably) {
    Fixture fx;
    EXPECT_TRUE(fx.panel.renameTrack(0, QStringLiteral("Cam A")));
    EXPECT_EQ(fx.seq().videoTracks[0].name(), "Cam A");
    EXPECT_EQ(rowLabel(headerRow(fx, 0))->text(), QStringLiteral("Cam A"));

    fx.controller.undoStack().undo();
    EXPECT_EQ(fx.seq().videoTracks[0].name(), "V1");
    EXPECT_EQ(rowLabel(headerRow(fx, 0))->text(), QStringLiteral("V1"));
}

TEST(TimelineTrackHeader, AddTrackButtonsAppendAndUndo) {
    Fixture fx;
    auto* addVideo = fx.panel.findChild<QToolButton*>(
        QStringLiteral("addVideoTrackButton"));
    auto* addAudio = fx.panel.findChild<QToolButton*>(
        QStringLiteral("addAudioTrackButton"));
    ASSERT_NE(addVideo, nullptr);
    ASSERT_NE(addAudio, nullptr);

    addVideo->click();
    EXPECT_EQ(fx.seq().videoTracks.size(), 2u);
    EXPECT_EQ(fx.panel.trackCount(), 3);

    addAudio->click();
    EXPECT_EQ(fx.seq().audioTracks.size(), 2u);

    fx.controller.undoStack().undo();
    EXPECT_EQ(fx.seq().audioTracks.size(), 1u);
    fx.controller.undoStack().undo();
    EXPECT_EQ(fx.seq().videoTracks.size(), 1u);
}

TEST(TimelineTrackHeader, RemoveRefusesLastVideoTrackButRemovesAudio) {
    Fixture fx;
    EXPECT_FALSE(fx.panel.removeTrackAt(0)); // V1 is the only video track
    EXPECT_EQ(fx.seq().videoTracks.size(), 1u);

    EXPECT_TRUE(fx.panel.removeTrackAt(1)); // audio is fair game
    EXPECT_EQ(fx.seq().audioTracks.size(), 0u);
    EXPECT_EQ(fx.panel.trackCount(), 1);
}

TEST(TimelineTrackHeader, MoveTrackToReordersWithinKind) {
    Fixture fx;
    ASSERT_TRUE(fx.panel.addTrack(true)); // V2
    ASSERT_TRUE(fx.panel.renameTrack(0, QStringLiteral("first")));
    ASSERT_TRUE(fx.panel.renameTrack(1, QStringLiteral("second")));

    EXPECT_TRUE(fx.panel.moveTrackTo(1, 0));
    EXPECT_EQ(fx.seq().videoTracks[0].name(), "second");
    EXPECT_EQ(fx.seq().videoTracks[1].name(), "first");

    // Cross-kind reordering is refused.
    fx.panel.addTrack(false); // A2
    const int audioFlat = fx.panel.trackCount() - 1;
    EXPECT_FALSE(fx.panel.moveTrackTo(0, audioFlat));

    fx.controller.undoStack().undo(); // undo A2 insert
    EXPECT_EQ(fx.seq().audioTracks.size(), 1u);
}

TEST(TimelineTrackHeader, ClickingClipSetsActiveTrack) {
    Fixture fx;
    fx.addClip(0, makeClip("v", 0, 5));
    fx.addClip(1, makeClip("a", 0, 5));

    fx.panel.setActiveTrack(0);
    int signalled = -1;
    QObject::connect(&fx.panel, &bl::ui::TimelinePanel::activeTrackChanged,
                     [&signalled](int flat) { signalled = flat; });

    fx.clickOnClip("a", 1);
    EXPECT_EQ(signalled, 1);
    EXPECT_EQ(fx.panel.activeTrack(), 1);
}

// ---------------------------------------------------------------------------
// Phase 3: selectable lane layout (Normal / Mixed / Mixed reverse), a
// fit-to-view zoom action and a logarithmic zoom slider.
// ---------------------------------------------------------------------------

TEST(TimelineLaneOrderPanel, FlatAtYInvertsLaneTopForEveryOrder) {
    Fixture fx;
    ASSERT_TRUE(fx.panel.addTrack(true));  // V2
    ASSERT_TRUE(fx.panel.addTrack(false)); // A2
    ASSERT_EQ(fx.panel.trackCount(), 4);

    const int vc = fx.panel.videoCount();
    for (const auto order : {tg::LaneOrder::Split, tg::LaneOrder::Mixed,
                             tg::LaneOrder::MixedReverse}) {
        fx.panel.setLaneOrder(order);
        EXPECT_EQ(fx.panel.laneOrder(), order);
        for (int lane = 0; lane < 4; ++lane) {
            const int flat = tg::flatForLane(fx.seq(), lane, order);
            ASSERT_GE(flat, 0);
            const qreal top = tg::laneTop(fx.seq(), flat, vc, order);
            const qreal h = tg::laneHeight(fx.seq(), flat, vc);
            EXPECT_EQ(fx.panel.flatAtY(top + h / 2.0), flat)
                << "order=" << static_cast<int>(order) << " lane=" << lane;
        }
    }

    // Nothing above the ruler or past the last lane.
    EXPECT_EQ(fx.panel.flatAtY(1.0), -1);
    EXPECT_EQ(fx.panel.flatAtY(100000.0), -1);
}

TEST(TimelineLaneOrderPanel, SwitchingOrderKeepsModelAndRelaysOutLanes) {
    Fixture fx;
    ASSERT_TRUE(fx.panel.addTrack(true));  // V2
    ASSERT_TRUE(fx.panel.addTrack(false)); // A2
    fx.addClip(0, makeClip("v", 0, 5));    // V1
    fx.controller.timeline().sequence().audioTracks[0].addClip(
        makeClip("a", 0, 5)); // A1 -> flat 2
    fx.panel.rebuildFromModel();

    const size_t videos = fx.seq().videoTracks.size();
    const size_t audios = fx.seq().audioTracks.size();
    const int64_t vStart = clipStart(fx, "v");
    const int64_t aStart = clipStart(fx, "a");

    int emitted = -1;
    QObject::connect(&fx.panel, &bl::ui::TimelinePanel::laneOrderChanged,
                     [&emitted](tg::LaneOrder o) {
                         emitted = static_cast<int>(o);
                     });

    fx.panel.setLaneOrder(tg::LaneOrder::Mixed);
    EXPECT_EQ(emitted, static_cast<int>(tg::LaneOrder::Mixed));

    // A view-only flip must not touch the document.
    EXPECT_EQ(fx.seq().videoTracks.size(), videos);
    EXPECT_EQ(fx.seq().audioTracks.size(), audios);
    EXPECT_EQ(clipStart(fx, "v"), vStart);
    EXPECT_EQ(clipStart(fx, "a"), aStart);

    // The lane geometry now reflects the new order.
    const qreal splitTop = tg::laneTop(fx.seq(), 1, 2, tg::LaneOrder::Split);
    const qreal mixedTop = tg::laneTop(fx.seq(), 1, 2, tg::LaneOrder::Mixed);
    EXPECT_NE(splitTop, mixedTop);
    EXPECT_EQ(fx.panel.laneRect(1).top(), mixedTop);

    // Re-selecting the active order is a no-op (no signal).
    emitted = -1;
    fx.panel.setLaneOrder(tg::LaneOrder::Mixed);
    EXPECT_EQ(emitted, -1);
}

TEST(TimelineLaneOrderPanel, ComboDrivesAndReflectsOrder) {
    Fixture fx;
    auto* combo = fx.panel.findChild<QComboBox*>(QStringLiteral("laneOrderCombo"));
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 3);
    EXPECT_EQ(combo->currentData().toInt(),
              static_cast<int>(tg::LaneOrder::Split));

    combo->setCurrentIndex(1);
    EXPECT_EQ(fx.panel.laneOrder(), tg::LaneOrder::Mixed);

    fx.panel.setLaneOrder(tg::LaneOrder::MixedReverse);
    EXPECT_EQ(combo->currentIndex(), 2);
    EXPECT_EQ(combo->currentData().toInt(),
              static_cast<int>(tg::LaneOrder::MixedReverse));
}

TEST(TimelineZoomControls, FitClampsToRange) {
    Fixture fx;
    // Long enough to hit the min zoom on any platform: the toolbar's minimum
    // width (font-metric dependent) can keep the viewport far wider than the
    // 320px we ask for, so the content must dwarf even that.
    fx.addClip(0, makeClip("long", 0, 100000));

    fx.panel.resize(320, 900);
    QApplication::processEvents();
    fx.panel.fitZoomToViewport();
    // Viewport is far narrower than the content, so the fit clamps to min.
    EXPECT_DOUBLE_EQ(fx.panel.zoom(), 0.25);

    // Tiny content on a wide panel stays inside the upper clamp too.
    Fixture wide;
    wide.addClip(0, makeClip("tiny", 0, 1));
    wide.panel.resize(12000, 900);
    QApplication::processEvents();
    wide.panel.fitZoomToViewport();
    EXPECT_GE(wide.panel.zoom(), 0.25);
    EXPECT_LE(wide.panel.zoom(), 64.0);
}

TEST(TimelineZoomControls, SliderTracksAndDrivesZoom) {
    Fixture fx;
    auto* slider = fx.panel.findChild<QSlider*>(QStringLiteral("zoomSlider"));
    ASSERT_NE(slider, nullptr);

    fx.panel.setZoom(8.0);
    EXPECT_GT(slider->value(), 0);
    const int before = slider->value();

    slider->setValue(std::min(before + 200, slider->maximum()));
    EXPECT_GT(fx.panel.zoom(), 8.0);

    // The Fit button routes to fitZoomToViewport (zoom ends clamped).
    auto* fit = fx.panel.findChild<QToolButton*>(QStringLiteral("fitZoomButton"));
    ASSERT_NE(fit, nullptr);
    fit->click();
    EXPECT_GE(fx.panel.zoom(), 0.25);
    EXPECT_LE(fx.panel.zoom(), 64.0);
}

// ---------------------------------------------------------------------------
// Phase 4: edit-state toolbar (mode + insert/overwrite + ripple) and the
// Select/Range/Ripple/Roll/Slip/Spacer editing modes.
// ---------------------------------------------------------------------------

using bl::ui::TimelineState;
using EditMode = TimelineState::EditMode;

TEST(TimelineEditState, DefaultsAndToolbarDriveState) {
    Fixture fx;
    EXPECT_EQ(fx.panel.editMode(), EditMode::Select);
    EXPECT_FALSE(fx.panel.insertMode());
    EXPECT_FALSE(fx.panel.rippleEnabled());
    EXPECT_TRUE(fx.panel.snapEnabled());

    auto* mode = fx.panel.findChild<QComboBox*>(QStringLiteral("editModeCombo"));
    ASSERT_NE(mode, nullptr);
    ASSERT_EQ(mode->count(), 6);
    EXPECT_EQ(mode->currentData().toInt(),
              static_cast<int>(EditMode::Select));

    mode->setCurrentIndex(3);  // Roll
    EXPECT_EQ(fx.panel.editMode(), EditMode::Roll);
    fx.panel.setEditMode(EditMode::Slip);
    EXPECT_EQ(mode->currentIndex(), 4);
    EXPECT_EQ(mode->currentData().toInt(), static_cast<int>(EditMode::Slip));

    auto* insert = fx.panel.findChild<QToolButton*>(QStringLiteral("insertModeButton"));
    auto* ripple = fx.panel.findChild<QToolButton*>(QStringLiteral("rippleButton"));
    ASSERT_NE(insert, nullptr);
    ASSERT_NE(ripple, nullptr);
    insert->setChecked(true);
    EXPECT_TRUE(fx.panel.insertMode());
    ripple->setChecked(true);
    EXPECT_TRUE(fx.panel.rippleEnabled());
    fx.panel.setInsertMode(false);
    EXPECT_FALSE(insert->isChecked());
    fx.panel.setRippleEnabled(false);
    EXPECT_FALSE(ripple->isChecked());
}

TEST(TimelineEditState, SetTimelineStateAppliesEveryField) {
    Fixture fx;
    TimelineState s;
    s.editMode = EditMode::Spacer;
    s.insertMode = true;
    s.ripple = true;
    s.snap = false;
    s.laneOrder = tg::LaneOrder::Mixed;
    fx.panel.setTimelineState(s);
    EXPECT_EQ(fx.panel.editMode(), EditMode::Spacer);
    EXPECT_TRUE(fx.panel.insertMode());
    EXPECT_TRUE(fx.panel.rippleEnabled());
    EXPECT_FALSE(fx.panel.snapEnabled());
    EXPECT_EQ(fx.panel.laneOrder(), tg::LaneOrder::Mixed);
}

TEST(TimelineEditState, RangeModeDragSetsZoneAndClickClears) {
    Fixture fx;
    fx.panel.setEditMode(EditMode::Range);
    EXPECT_FALSE(fx.panel.hasZone());

    const qreal y = 10.0;  // inside the ruler band
    const QPoint press = fx.viewportPos(QPointF(fx.sceneX(5), y));
    const QPoint mid = fx.viewportPos(QPointF(fx.sceneX(10), y));
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(15), y));
    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                      Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), mid);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                        Qt::NoModifier, release);

    ASSERT_TRUE(fx.panel.hasZone());
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 5);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 15);

    // A plain ruler click in Range mode clears the zone.
    QTest::mouseClick(fx.panel.view()->viewport(), Qt::LeftButton,
                      Qt::NoModifier, fx.viewportPos(QPointF(fx.sceneX(8), y)));
    EXPECT_FALSE(fx.panel.hasZone());
}

TEST(TimelineEditState, RippleModeDragShiftsFollowingClips) {
    Fixture fx;
    const Clip a = makeClip("a", 0, 5);
    Clip b = makeClip("b", 0, 10);
    b.timelineStart = a.timelineStart + a.timelineDuration;  // 5
    b.source.sourceIn = fr(0);
    b.source.sourceOut = fr(10);
    fx.addClip(0, a);
    fx.addClip(0, b);

    fx.panel.setEditMode(EditMode::Ripple);
    const qreal y = fx.panel.laneRect(0).top() + 5.0;
    const QPoint press = fx.viewportPos(QPointF(fx.sceneX(2), y));
    const QPoint mid = fx.viewportPos(QPointF(fx.sceneX(4), y));
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(7), y));
    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                      Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), mid);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                        Qt::NoModifier, release);

    EXPECT_EQ(clipStart(fx, "a"), 5);
    EXPECT_EQ(clipStart(fx, "b"), 10);  // rode along by a's +5 move

    fx.controller.undoStack().undo();
    EXPECT_EQ(clipStart(fx, "a"), 0);
    EXPECT_EQ(clipStart(fx, "b"), 5);
}

TEST(TimelineEditState, SlipModeDragSlidesSourceWindow) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 10));  // source window [0, 10)
    fx.panel.setEditMode(EditMode::Slip);

    const qreal y = fx.panel.laneRect(0).top() + 5.0;
    const QPoint press = fx.viewportPos(QPointF(fx.sceneX(2), y));
    const QPoint mid = fx.viewportPos(QPointF(fx.sceneX(4), y));
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(5), y));
    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                      Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), mid);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                        Qt::NoModifier, release);

    const Clip* c = fx.find("a");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(frameOf(c->timelineStart), 0);      // timeline range untouched
    EXPECT_EQ(frameOf(c->timelineDuration), 10);
    EXPECT_EQ(frameOf(c->source.sourceIn), 3);    // slid by +3 frames
    EXPECT_EQ(frameOf(c->source.sourceOut), 13);

    fx.controller.undoStack().undo();
    EXPECT_EQ(frameOf(fx.find("a")->source.sourceIn), 0);
}

TEST(TimelineEditState, RollModeDragMovesSeam) {
    Fixture fx;
    const Clip a = makeClip("a", 0, 10);
    Clip b = makeClip("b", 0, 10);
    b.timelineStart = a.timelineStart + a.timelineDuration;  // adjacent at 10
    b.source.sourceIn = fr(0);
    b.source.sourceOut = fr(10);
    fx.addClip(0, a);
    fx.addClip(0, b);

    fx.panel.setEditMode(EditMode::Roll);
    const qreal y = fx.panel.laneRect(0).top() + 5.0;
    const QPoint press = fx.viewportPos(QPointF(fx.sceneX(10), y));
    const QPoint mid = fx.viewportPos(QPointF(fx.sceneX(12), y));
    const QPoint release = fx.viewportPos(QPointF(fx.sceneX(13), y));
    QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                      Qt::NoModifier, press);
    QTest::mouseMove(fx.panel.view()->viewport(), mid);
    QTest::mouseMove(fx.panel.view()->viewport(), release);
    QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                        Qt::NoModifier, release);

    const Clip* ca = fx.find("a");
    const Clip* cb = fx.find("b");
    ASSERT_NE(ca, nullptr);
    ASSERT_NE(cb, nullptr);
    EXPECT_EQ(frameOf(ca->timelineStart), 0);
    EXPECT_EQ(frameOf(ca->timelineStart + ca->timelineDuration), 13);
    EXPECT_EQ(frameOf(cb->timelineStart), 13);
    EXPECT_EQ(frameOf(cb->timelineStart + cb->timelineDuration), 20);

    fx.controller.undoStack().undo();
    EXPECT_EQ(frameOf(fx.find("a")->timelineStart +
                      fx.find("a")->timelineDuration),
              10);
}

TEST(TimelineEditState, SpacerModeInsertsAndRemovesSpace) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 10));
    fx.addClip(0, makeClip("b", 20, 10));
    fx.panel.setEditMode(EditMode::Spacer);

    const qreal y = fx.panel.laneRect(0).top() + 5.0;
    const auto drag = [&](int64_t fromFrame, int64_t toFrame) {
        const QPoint p = fx.viewportPos(QPointF(fx.sceneX(fromFrame), y));
        const QPoint r = fx.viewportPos(QPointF(fx.sceneX(toFrame), y));
        QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);
        QTest::mouseMove(fx.panel.view()->viewport(), r);
        QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                            Qt::NoModifier, r);
    };

    // Press in the gap at frame 15, drag right to 20: b shifts +5.
    drag(15, 20);
    EXPECT_EQ(clipStart(fx, "a"), 0);
    EXPECT_EQ(clipStart(fx, "b"), 25);

    // Drag left from 15 to 12 removes 3 frames: b shifts back to 22.
    drag(15, 12);
    EXPECT_EQ(clipStart(fx, "b"), 22);
}

TEST(TimelineEditState, InsertModeRipplesWhileOverwriteRefuses) {
    Fixture fx;
    const Clip a = makeClip("a", 0, 5);
    Clip b = makeClip("b", 0, 10);
    b.timelineStart = a.timelineStart + a.timelineDuration;  // 5
    b.source.sourceIn = fr(0);
    b.source.sourceOut = fr(10);
    fx.addClip(0, a);
    fx.addClip(0, b);

    const qreal y = fx.panel.laneRect(0).top() + 5.0;
    const auto drag = [&](int64_t fromFrame, int64_t toFrame) {
        const QPoint p = fx.viewportPos(QPointF(fx.sceneX(fromFrame), y));
        const QPoint r = fx.viewportPos(QPointF(fx.sceneX(toFrame), y));
        QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);
        QTest::mouseMove(fx.panel.view()->viewport(), r);
        QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                            Qt::NoModifier, r);
    };

    // Overwrite (default): moving a to frame 3 overlaps b -> refused.
    drag(2, 5);
    EXPECT_EQ(clipStart(fx, "a"), 0);
    EXPECT_EQ(clipStart(fx, "b"), 5);

    // Insert: the same drag ripples b out of the way by a's duration (5).
    fx.panel.setInsertMode(true);
    drag(2, 5);
    EXPECT_EQ(clipStart(fx, "a"), 3);
    EXPECT_EQ(clipStart(fx, "b"), 10);
}

TEST(TimelineEditState, RippleToggleMakesDeleteCloseTheGap) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 10, 5));

    fx.panel.setRippleEnabled(true);
    fx.clickOnClip("a", 0);
    QTest::keyClick(fx.panel.view(), Qt::Key_Delete);

    EXPECT_EQ(fx.find("a"), nullptr);
    EXPECT_EQ(clipStart(fx, "b"), 5);  // gap closed by ripple delete
}

// ---------------------------------------------------------------------------
// Phase 5: timeline zone — set/clear, ruler handles, and lift/extract.
// ---------------------------------------------------------------------------

TEST(TimelineZone, SetZoneInOutClampAndClear) {
    Fixture fx;
    EXPECT_FALSE(fx.panel.hasZone());

    fx.panel.setZoneIn(fr(10));
    EXPECT_TRUE(fx.panel.hasZone());
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 10);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 10);  // first edge opens a point

    fx.panel.setZoneOut(fr(5));  // clamps to the in point
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 10);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 10);

    fx.panel.setZoneOut(fr(20));
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 20);

    fx.panel.setZoneIn(fr(30));  // clamps to the out point
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 20);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 20);
    EXPECT_EQ(frameOf(fx.panel.zoneDuration()), 0);

    fx.panel.clearZone();
    EXPECT_FALSE(fx.panel.hasZone());
}

TEST(TimelineZone, ToolbarButtonsSetZoneAtPlayhead) {
    Fixture fx;
    auto* in = fx.panel.findChild<QToolButton*>(QStringLiteral("zoneInButton"));
    auto* out = fx.panel.findChild<QToolButton*>(QStringLiteral("zoneOutButton"));
    auto* clear = fx.panel.findChild<QToolButton*>(QStringLiteral("clearZoneButton"));
    ASSERT_NE(in, nullptr);
    ASSERT_NE(out, nullptr);
    ASSERT_NE(clear, nullptr);

    fx.panel.setPlayhead(fr(5));
    in->click();
    EXPECT_TRUE(fx.panel.hasZone());
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 5);

    fx.panel.setPlayhead(fr(15));
    out->click();
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 5);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 15);

    clear->click();
    EXPECT_FALSE(fx.panel.hasZone());
}

TEST(TimelineZone, RulerHandlesDragZoneEdges) {
    Fixture fx;
    fx.panel.setZone(fr(5), fr(15));

    const qreal y = 10.0;  // inside the ruler band
    const auto dragTo = [&](qreal fromX, qreal toX) {
        const QPoint p = fx.viewportPos(QPointF(fromX, y));
        const QPoint r = fx.viewportPos(QPointF(toX, y));
        QTest::mousePress(fx.panel.view()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);
        QTest::mouseMove(fx.panel.view()->viewport(), r);
        QTest::mouseRelease(fx.panel.view()->viewport(), Qt::LeftButton,
                            Qt::NoModifier, r);
    };

    dragTo(fx.sceneX(5), fx.sceneX(8));  // grab the in handle
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 8);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 15);

    dragTo(fx.sceneX(15), fx.sceneX(20));  // grab the out handle
    EXPECT_EQ(frameOf(fx.panel.zoneIn()), 8);
    EXPECT_EQ(frameOf(fx.panel.zoneOut()), 20);

    // A ruler click away from the handles scrubs; it must not clear the zone.
    QTest::mouseClick(fx.panel.view()->viewport(), Qt::LeftButton,
                      Qt::NoModifier, fx.viewportPos(QPointF(fx.sceneX(12), y)));
    EXPECT_TRUE(fx.panel.hasZone());
    EXPECT_EQ(frameOf(fx.panel.playhead()), 12);
}

TEST(TimelineZone, LiftRemovesContentInsideZone) {
    Fixture fx;
    const Clip a = makeClip("a", 0, 10);
    Clip b = makeClip("b", 0, 10);
    b.timelineStart = a.timelineStart + a.timelineDuration;
    b.source.sourceIn = fr(10);
    b.source.sourceOut = fr(20);
    fx.addClip(0, a);
    fx.addClip(0, b);
    fx.addClip(1, makeClip("aud", 0, 20));

    fx.panel.setZone(fr(5), fr(15));
    ASSERT_TRUE(fx.panel.liftZone());

    const auto& vids = fx.seq().videoTracks[0].clips();
    ASSERT_EQ(vids.size(), 2u);
    EXPECT_EQ(frameOf(vids[0].timelineStart), 0);
    EXPECT_EQ(frameOf(vids[0].timelineDuration), 5);  // a head
    EXPECT_EQ(frameOf(vids[1].timelineStart), 15);
    EXPECT_EQ(frameOf(vids[1].timelineDuration), 5);  // b tail
    EXPECT_EQ(frameOf(vids[1].source.sourceIn), 15);

    const auto& auds = fx.seq().audioTracks[0].clips();
    ASSERT_EQ(auds.size(), 2u);
    EXPECT_EQ(frameOf(auds[0].timelineDuration), 5);
    EXPECT_EQ(frameOf(auds[1].timelineStart), 15);
    EXPECT_EQ(frameOf(auds[1].timelineDuration), 5);

    fx.controller.undoStack().undo();
    const auto& restored = fx.seq().videoTracks[0].clips();
    ASSERT_EQ(restored.size(), 2u);
    EXPECT_EQ(frameOf(restored[0].timelineDuration), 10);
    EXPECT_EQ(frameOf(restored[1].timelineStart), 10);
    EXPECT_EQ(frameOf(fx.seq().audioTracks[0].clips()[0].timelineDuration), 20);
}

TEST(TimelineZone, ExtractClosesGap) {
    Fixture fx;
    const Clip a = makeClip("a", 0, 10);
    Clip b = makeClip("b", 0, 10);
    b.timelineStart = a.timelineStart + a.timelineDuration;
    b.source.sourceIn = fr(10);
    b.source.sourceOut = fr(20);
    Clip c = makeClip("c", 0, 10);
    c.timelineStart = b.timelineStart + b.timelineDuration;
    c.source.sourceIn = fr(20);
    c.source.sourceOut = fr(30);
    fx.addClip(0, a);
    fx.addClip(0, b);
    fx.addClip(0, c);

    fx.panel.setZone(fr(5), fr(15));
    ASSERT_TRUE(fx.panel.extractZone());

    const auto& v = fx.seq().videoTracks[0].clips();
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(frameOf(v[0].timelineStart), 0);
    EXPECT_EQ(frameOf(v[0].timelineDuration), 5);   // a head
    EXPECT_EQ(frameOf(v[1].timelineStart), 5);      // b tail rippled left
    EXPECT_EQ(frameOf(v[1].timelineDuration), 5);
    EXPECT_EQ(frameOf(v[2].timelineStart), 10);     // c shifted left by 10
    EXPECT_EQ(frameOf(v[2].timelineDuration), 10);
}

TEST(TimelineZone, LiftSkipsLockedTracks) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 10));
    fx.addClip(1, makeClip("aud", 0, 20));
    fx.controller.timeline().sequence().audioTracks[0].setLocked(true);

    fx.panel.setZone(fr(5), fr(15));
    ASSERT_TRUE(fx.panel.liftZone());

    // Video was cut; the locked audio track is byte-for-byte intact.
    EXPECT_EQ(frameOf(fx.seq().videoTracks[0].clips()[0].timelineDuration), 5);
    const auto& auds = fx.seq().audioTracks[0].clips();
    ASSERT_EQ(auds.size(), 1u);
    EXPECT_EQ(frameOf(auds[0].timelineStart), 0);
    EXPECT_EQ(frameOf(auds[0].timelineDuration), 20);
}

TEST(TimelineZone, LiftAndExtractRefuseEmptyOrMissingZone) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 10));

    // No zone set.
    EXPECT_FALSE(fx.panel.liftZone());
    EXPECT_FALSE(fx.panel.extractZone());

    // Zero-length zone.
    fx.panel.setZone(fr(5), fr(5));
    EXPECT_FALSE(fx.panel.liftZone());
    EXPECT_FALSE(fx.panel.extractZone());

    // The model is untouched and nothing was pushed on the undo stack.
    ASSERT_EQ(fx.seq().videoTracks[0].clips().size(), 1u);
    EXPECT_EQ(frameOf(fx.seq().videoTracks[0].clips()[0].timelineDuration), 10);
    EXPECT_FALSE(fx.controller.undoStack().canUndo());
}

// --- Clipboard + markers (Phase 7) -----------------------------------------

TEST(TimelinePanel, copyPasteSelectionDuplicatesClips) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.clickOnClip("a", 0);
    ASSERT_EQ(fx.panel.copySelection(), 1);

    fx.panel.setPlayhead(fr(100));
    EXPECT_EQ(fx.panel.pasteAtPlayhead(), 1);
    EXPECT_EQ(fx.panel.clipItemCount(), 2);
    // The pasted copy is selected; the source is not.
    EXPECT_EQ(fx.panel.selection().size(), 1u);
    EXPECT_FALSE(fx.panel.selection().contains("a"));
}

TEST(TimelinePanel, cutRemovesSelectionAndPastesBack) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 20, 5));
    fx.clickOnClip("a", 0);

    ASSERT_EQ(fx.panel.cutSelection(), 1);
    EXPECT_EQ(fx.panel.clipItemCount(), 1);
    EXPECT_TRUE(fx.panel.selection().isEmpty());

    // The cut clip is still on the clipboard; pasting restores a copy.
    EXPECT_EQ(fx.panel.pasteAt(fr(60)), 1);
    EXPECT_EQ(fx.panel.clipItemCount(), 2);
}

TEST(TimelinePanel, pasteWithEmptyClipboardIsRefused) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    EXPECT_EQ(fx.panel.pasteAt(fr(30)), 0);
    EXPECT_EQ(fx.panel.clipItemCount(), 1);
}

TEST(TimelinePanel, addRenameRemoveMarkerUndoably) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));

    ASSERT_TRUE(fx.panel.addMarkerAt(fr(10), QStringLiteral("intro")));
    EXPECT_EQ(fx.panel.markerItemCount(), 1);
    ASSERT_EQ(fx.seq().markers.size(), 1u);
    EXPECT_EQ(fx.seq().markers.front().label, "intro");

    ASSERT_TRUE(fx.panel.renameMarkerAt(fr(10), QStringLiteral("hook")));
    EXPECT_EQ(fx.seq().markers.front().label, "hook");

    ASSERT_TRUE(fx.panel.removeMarkerAt(fr(10)));
    EXPECT_EQ(fx.panel.markerItemCount(), 0);
    EXPECT_TRUE(fx.seq().markers.empty());
}

TEST(TimelinePanel, markerNearFindsClosestWithinRadius) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    ASSERT_TRUE(fx.panel.addMarkerAt(fr(10)));

    bl::Time out;
    EXPECT_TRUE(fx.panel.markerNear(fr(11), 2, out));
    EXPECT_EQ(frameOf(out), 10);
    EXPECT_FALSE(fx.panel.markerNear(fr(20), 2, out));
}

TEST(TimelinePanel, clipboardShortcutsCopyCutPaste) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.clickOnClip("a", 0);

    fx.panel.view()->setFocus();
    QTest::keyClick(fx.panel.view(), Qt::Key_C, Qt::ControlModifier);
    fx.panel.setPlayhead(fr(50));
    QTest::keyClick(fx.panel.view(), Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(fx.panel.clipItemCount(), 2);

    // Ctrl+X removes the (now pasted) selection, keeping it on the clipboard.
    QTest::keyClick(fx.panel.view(), Qt::Key_X, Qt::ControlModifier);
    EXPECT_EQ(fx.panel.clipItemCount(), 1);
}

TEST(TimelinePanel, markerKeyAddsAtPlayhead) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.panel.setPlayhead(fr(12));
    fx.panel.view()->setFocus();
    QTest::keyClick(fx.panel.view(), Qt::Key_M, Qt::NoModifier);
    ASSERT_EQ(fx.seq().markers.size(), 1u);
    EXPECT_EQ(frameOf(fx.seq().markers.front().position), 12);
}

TEST(TimelinePanel, importSubtitleCuesAddsSubtitleItems) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));

    std::vector<bl::SubtitleCue> cues;
    bl::SubtitleCue c1;
    c1.start = fr(0);
    c1.end = fr(10);
    c1.text = "hello";
    bl::SubtitleCue c2;
    c2.start = fr(20);
    c2.end = fr(30);
    c2.text = "world";
    cues.push_back(c1);
    cues.push_back(c2);

    ASSERT_EQ(fx.panel.importSubtitleCues(cues), 2);
    // Original clip + two subtitle clips.
    EXPECT_EQ(fx.panel.clipItemCount(), 3);
    EXPECT_EQ(fx.seq().videoTracks.size(), 2u);
}

TEST(TimelinePanel, selectedClipRangeSpansSelection) {
    Fixture fx;
    fx.addClip(0, makeClip("a", 0, 5));
    fx.addClip(0, makeClip("b", 20, 7));

    bl::Time in{};
    bl::Time out{};
    EXPECT_FALSE(fx.panel.selectedClipRange(in, out));

    fx.clickOnClip("a", 0);
    ASSERT_TRUE(fx.panel.selectedClipRange(in, out));
    EXPECT_EQ(frameOf(in), 0);
    EXPECT_EQ(frameOf(out), 5);

    fx.clickOnClip("b", 0, Qt::ControlModifier);
    ASSERT_TRUE(fx.panel.selectedClipRange(in, out));
    EXPECT_EQ(frameOf(in), 0);
    EXPECT_EQ(frameOf(out), 27);
}
