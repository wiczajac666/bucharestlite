#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>

#include <app/project_controller.hpp>
#include <panels/timeline_panel.hpp>

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPoint>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

using bl::Clip;
using bl::ClipId;
using bl::Duration;
using bl::Time;
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