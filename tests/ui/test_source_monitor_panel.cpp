#include <bl_core/media_source.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>

#include <app/project_controller.hpp>
#include <panels/source_monitor_panel.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>
#include <string>

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

namespace {

using bl::Time;
using bl::ui::ProjectController;
using bl::ui::SourceMonitorPanel;

std::string mediaPath(const std::string& name) {
    return std::string(BL_TEST_MEDIA_DIR) + "/" + name;
}

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

struct AppBootstrapper {
    AppBootstrapper() { ensureApp(); }
};
const AppBootstrapper kAppBootstrapper{};

struct Fixture {
    ProjectController controller;
    SourceMonitorPanel panel;
    QTemporaryDir dir;

    Fixture() : panel(&controller) {
        ensureApp();
        controller.newProject(QStringLiteral("Source"));
        panel.resize(400, 300);
        panel.show();
        QApplication::processEvents();
    }

    std::string addMedia(const std::string& name) {
        controller.addToMediaBin(QString::fromStdString(mediaPath(name)));
        QApplication::processEvents();
        return controller.mediaBin().back().id;
    }
};

TEST(SourceMonitorPanel, EmptyMediaShowsNoMarks) {
    Fixture f;
    EXPECT_EQ(f.panel.mediaId(), std::string());
    EXPECT_FALSE(f.panel.hasMarks());
}

TEST(SourceMonitorPanel, LoadMediaDefaultsMarksToFullSpan) {
    Fixture f;
    const std::string id = f.addMedia("test_video.mp4");
    f.panel.setMedia(id);
    QApplication::processEvents();

    EXPECT_EQ(f.panel.mediaId(), id);
    EXPECT_GT(f.panel.durationSeconds(), 0.0);
    EXPECT_TRUE(f.panel.hasMarks());
    EXPECT_DOUBLE_EQ(f.panel.markIn().toSeconds(), 0.0);
    EXPECT_DOUBLE_EQ(f.panel.markOut().toSeconds(), f.panel.durationSeconds());
}

TEST(SourceMonitorPanel, UnknownMediaClearsPanel) {
    Fixture f;
    const std::string id = f.addMedia("test_video.mp4");
    f.panel.setMedia(id);
    QApplication::processEvents();
    ASSERT_FALSE(f.panel.mediaId().empty());

    f.panel.setMedia("does-not-exist-id");
    QApplication::processEvents();
    EXPECT_EQ(f.panel.mediaId(), std::string());
    EXPECT_FALSE(f.panel.hasMarks());
}

TEST(SourceMonitorPanel, MarkInAndOutUpdateAndClear) {
    Fixture f;
    const std::string id = f.addMedia("test_video.mp4");
    f.panel.setMedia(id);
    QApplication::processEvents();

    f.panel.setPositionSeconds(f.panel.durationSeconds() * 0.25);
    f.panel.markInHere();
    QApplication::processEvents();
    EXPECT_GT(f.panel.markIn().toSeconds(), 0.0);

    f.panel.setPositionSeconds(f.panel.durationSeconds() * 0.75);
    f.panel.markOutHere();
    QApplication::processEvents();
    EXPECT_GT(f.panel.markOut().toSeconds(), f.panel.markIn().toSeconds());
    EXPECT_TRUE(f.panel.hasMarks());

    f.panel.clearMarks();
    QApplication::processEvents();
    EXPECT_TRUE(f.panel.hasMarks()); // full span again
    EXPECT_DOUBLE_EQ(f.panel.markIn().toSeconds(), 0.0);
    EXPECT_DOUBLE_EQ(f.panel.markOut().toSeconds(), f.panel.durationSeconds());
}

TEST(SourceMonitorPanel, InsertEditEmitsRequestedWindow) {
    Fixture f;
    const std::string id = f.addMedia("test_video.mp4");
    f.panel.setMedia(id);
    QApplication::processEvents();

    f.panel.setPositionSeconds(f.panel.durationSeconds() * 0.25);
    f.panel.markInHere();
    f.panel.setPositionSeconds(f.panel.durationSeconds() * 0.75);
    f.panel.markOutHere();
    QApplication::processEvents();

    std::string gotId;
    Time gotIn{};
    Time gotOut{};
    QObject::connect(&f.panel, &SourceMonitorPanel::insertRequested,
                     [&](const std::string& mediaId, Time in, Time out) {
                         gotId = mediaId;
                         gotIn = in;
                         gotOut = out;
                     });

    EXPECT_TRUE(f.panel.insertEdit());
    EXPECT_EQ(gotId, id);
    EXPECT_GT(gotIn.toSeconds(), 0.0);
    EXPECT_GT(gotOut.toSeconds(), gotIn.toSeconds());
}

TEST(SourceMonitorPanel, OverwriteEditEmitsRequestedWindow) {
    Fixture f;
    const std::string id = f.addMedia("test_video.mp4");
    f.panel.setMedia(id);
    QApplication::processEvents();

    f.panel.setPositionSeconds(f.panel.durationSeconds() * 0.25);
    f.panel.markInHere();
    f.panel.setPositionSeconds(f.panel.durationSeconds() * 0.75);
    f.panel.markOutHere();
    QApplication::processEvents();

    std::string gotId;
    Time gotIn{};
    Time gotOut{};
    QObject::connect(&f.panel, &SourceMonitorPanel::overwriteRequested,
                     [&](const std::string& mediaId, Time in, Time out) {
                         gotId = mediaId;
                         gotIn = in;
                         gotOut = out;
                     });

    EXPECT_TRUE(f.panel.overwriteEdit());
    EXPECT_EQ(gotId, id);
    EXPECT_GT(gotIn.toSeconds(), 0.0);
    EXPECT_GT(gotOut.toSeconds(), gotIn.toSeconds());
}

TEST(SourceMonitorPanel, EditWithoutMediaDoesNotEmit) {
    Fixture f;

    int emitted = 0;
    QObject::connect(&f.panel, &SourceMonitorPanel::insertRequested,
                     [&] { ++emitted; });
    QObject::connect(&f.panel, &SourceMonitorPanel::overwriteRequested,
                     [&] { ++emitted; });

    EXPECT_FALSE(f.panel.insertEdit());
    EXPECT_FALSE(f.panel.overwriteEdit());
    QApplication::processEvents();
    EXPECT_EQ(emitted, 0);
}

} // namespace
