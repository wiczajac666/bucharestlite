#include <bl_core/undo_stack.hpp>
#include <bl_timeline/sequence.hpp>

#include <app/project_controller.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace {

using bl::ui::ProjectController;

std::string stl(const QString& s) {
    return s.toStdString();
}

} // namespace

TEST(ProjectController, newProjectBuildsDefaultDocument) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    EXPECT_TRUE(controller.isOpen());
    EXPECT_FALSE(controller.dirty());
    EXPECT_EQ(stl(controller.projectName()), "Demo");
    EXPECT_TRUE(controller.filePath().isEmpty());
    EXPECT_EQ(controller.project().settings.author, "Anonymous");
    EXPECT_FALSE(controller.project().settings.createdIso8601.empty());

    const auto& timeline = controller.timeline();
    EXPECT_EQ(timeline.sequence().videoTracks.size(), 1u);
    EXPECT_EQ(timeline.sequence().audioTracks.size(), 1u);
    EXPECT_EQ(timeline.sequence().name, "Demo");
    EXPECT_FALSE(controller.canUndo());
}

TEST(ProjectController, renameProjectIsUndoable) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    controller.renameProject(QStringLiteral("Renamed"));
    EXPECT_TRUE(controller.dirty());
    EXPECT_EQ(stl(controller.projectName()), "Renamed");
    EXPECT_TRUE(controller.canUndo());

    controller.undoStack().undo();
    EXPECT_FALSE(controller.dirty());
    EXPECT_EQ(stl(controller.projectName()), "Demo");

    controller.undoStack().redo();
    EXPECT_EQ(stl(controller.projectName()), "Renamed");
    EXPECT_TRUE(controller.dirty());
}

TEST(ProjectController, mediaBinMutatorsAreUndoable) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    const QString clip = QStringLiteral("/media/clip.mp4");
    ASSERT_TRUE(controller.addToMediaBin(clip));
    EXPECT_FALSE(controller.addToMediaBin(clip));
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    EXPECT_EQ(controller.mediaBin()[0].path, "/media/clip.mp4");
    EXPECT_EQ(controller.mediaBin()[0].name, "clip.mp4");
    EXPECT_FALSE(controller.mediaBin()[0].id.empty());

    const QString id = QString::fromStdString(controller.mediaBin()[0].id);
    ASSERT_TRUE(controller.removeFromMediaBin(id));
    EXPECT_TRUE(controller.mediaBin().empty());
    EXPECT_TRUE(controller.dirty());

    controller.undoStack().undo();
    ASSERT_FALSE(controller.mediaBin().empty());
    EXPECT_EQ(controller.mediaBin()[0].path, "/media/clip.mp4");
    EXPECT_TRUE(controller.dirty());

    controller.undoStack().undo();
    EXPECT_TRUE(controller.mediaBin().empty());
    EXPECT_FALSE(controller.dirty());
}

TEST(ProjectController, dirtyClearsWhenUndoReturnsToSavedState) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));
    ASSERT_FALSE(controller.dirty());

    controller.addToMediaBin(QStringLiteral("/media/a.mp4"));
    controller.addToMediaBin(QStringLiteral("/media/b.mp4"));
    controller.renameProject(QStringLiteral("Renamed"));
    EXPECT_TRUE(controller.dirty());
    EXPECT_TRUE(controller.canUndo());

    // Undo only part of the way: still dirty.
    controller.undoStack().undo();
    EXPECT_TRUE(controller.dirty());

    // Undo everything: back to the pristine adopted state.
    controller.undoStack().undo();
    controller.undoStack().undo();
    EXPECT_FALSE(controller.dirty());
    EXPECT_TRUE(controller.mediaBin().empty());

    // Redo past the saved state marks it dirty again.
    controller.undoStack().redo();
    EXPECT_TRUE(controller.dirty());
}

TEST(ProjectController, saveAndOpenRoundTripPreservesDocument) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("demo.blproj"));

    const std::string clipPath = "/media/clip.mp4";
    {
        ProjectController controller;
        controller.newProject(QStringLiteral("Demo"));
        ASSERT_TRUE(controller.addToMediaBin(QString::fromStdString(clipPath)));
        controller.renameProject(QStringLiteral("Round Trip"));

        const auto saved = controller.saveAs(path);
        ASSERT_TRUE(saved.ok()) << saved.message();
        EXPECT_FALSE(controller.dirty());
        EXPECT_EQ(stl(controller.filePath()), stl(path));
    }

    {
        ProjectController controller;
        const auto loaded = controller.open(path);
        ASSERT_TRUE(loaded.ok()) << loaded.message();
        EXPECT_FALSE(controller.dirty());
        EXPECT_EQ(stl(controller.projectName()), "Round Trip");
        ASSERT_EQ(controller.mediaBin().size(), 1u);
        EXPECT_EQ(controller.mediaBin()[0].path,
                  std::filesystem::absolute(
                      std::filesystem::path(clipPath))
                      .generic_string());

        const auto& timeline = controller.timeline();
        EXPECT_EQ(timeline.sequence().name, "Round Trip");
        EXPECT_EQ(timeline.sequence().videoTracks.size(), 1u);
        EXPECT_EQ(timeline.sequence().audioTracks.size(), 1u);
        EXPECT_FALSE(controller.undoStack().canUndo());
    }
}

TEST(ProjectController, openMissingFileReportsFailure) {
    ProjectController controller;
    const auto loaded = controller.open(QStringLiteral("/nonexistent/bl.proj"));
    EXPECT_FALSE(loaded.ok());
    EXPECT_FALSE(controller.isOpen());
}

TEST(ProjectController, saveWithoutPathGivesInvalidArgument) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));
    const auto saved = controller.save();
    EXPECT_FALSE(saved.ok());
    EXPECT_EQ(saved.code(), bl::Err::InvalidArgument);
}