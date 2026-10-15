#include <bl_core/undo_stack.hpp>
#include <bl_timeline/project_template.hpp>
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
        // Derived from the project dir, not fs::absolute(): "/media/clip.mp4"
        // is root-relative, so on Windows the repository joins it onto the
        // project dir and keeps that dir's drive. fs::absolute() would use the
        // process CWD's drive instead, which differs when the checkout and the
        // temp dir are on different volumes (the CI runner puts them on D: and
        // C: respectively). Joining on the project dir yields the right answer
        // on both platforms, because on POSIX the absolute path wins outright.
        EXPECT_EQ(controller.mediaBin()[0].path,
                  (std::filesystem::path(dir.path().toStdString()) / clipPath)
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

TEST(ProjectController, restoreFromAutosaveAdoptsDocumentAsDirty) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString dirPath = dir.path();
    const QString mediaPath = QDir(dirPath).filePath(QStringLiteral("interview.mp4"));
    const QString mainPath = QDir(dirPath).filePath(QStringLiteral("demo.blproj"));

    std::string autosaveText;
    {
        ProjectController source;
        source.newProject(QStringLiteral("Demo"));
        ASSERT_TRUE(source.addToMediaBin(mediaPath));
        ASSERT_TRUE(source.saveAs(mainPath).ok());
        // A crash after this edit leaves only the autosave with the change.
        source.renameProject(QStringLiteral("Recovered"));
        autosaveText = source.serializeDocument().dump(2);
    }

    const QString autosavePath = QDir(dirPath).filePath(QStringLiteral("autosave.blproj"));
    {
        QFile file(autosavePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(autosaveText.c_str());
    }

    {
        ProjectController recovered;
        const auto result =
            recovered.restoreFromAutosave(autosavePath, mainPath);
        ASSERT_TRUE(result.ok()) << result.message();

        EXPECT_EQ(stl(recovered.projectName()), "Recovered");
        EXPECT_EQ(stl(recovered.filePath()), stl(mainPath));
        EXPECT_TRUE(recovered.dirty());
        ASSERT_EQ(recovered.mediaBin().size(), 1u);
        // Media stored relative to the original project dir resolves against it.
        EXPECT_EQ(recovered.mediaBin()[0].path,
                  stl(QFileInfo(mediaPath).absoluteFilePath()));

        // A save writes the recovered content to the real project file and
        // returns the document to a clean state.
        ASSERT_TRUE(recovered.save().ok());
        EXPECT_FALSE(recovered.dirty());
    }

    {
        ProjectController reopened;
        ASSERT_TRUE(reopened.open(mainPath).ok());
        EXPECT_EQ(stl(reopened.projectName()), "Recovered");
        EXPECT_FALSE(reopened.dirty());
    }
}

TEST(ProjectController, serializeDocumentIncludesTimelineExtension) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Doc"));
    const nlohmann::json doc = controller.serializeDocument();

    ASSERT_TRUE(doc.contains("settings"));
    EXPECT_EQ(doc["settings"]["name"], "Doc");
    ASSERT_TRUE(doc.contains("extensionTraceable") == false ||
                doc.contains("extensions"));
    ASSERT_TRUE(doc.at("extensions").is_object());
    EXPECT_TRUE(doc["extensions"].contains("timeline"));
}
TEST(ProjectController, imageSequenceFramesCollapseToOnePatternEntry) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString base = dir.path();
    QString framePaths[3];
    for (int index : {1, 2, 3}) {
        const QString name =
            base + QStringLiteral("/frame_%1.png").arg(index, 4, 10,
                                                       QLatin1Char('0'));
        QFile file(name);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
        framePaths[index - 1] = name;
    }

    ProjectController controller;
    controller.newProject(QStringLiteral("Seq"));

    ASSERT_TRUE(controller.addToMediaBin(framePaths[0]));
    // The two sibling frames map to the same sequence entry.
    EXPECT_FALSE(controller.addToMediaBin(framePaths[1]));
    EXPECT_FALSE(controller.addToMediaBin(framePaths[2]));
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    EXPECT_EQ(controller.mediaBin()[0].path,
              stl(base) + "/frame_%04d.png");
    EXPECT_EQ(controller.mediaBin()[0].name, "frame (3 frames)");

    // Undo removes the sequence; redo restores it.
    controller.undoStack().undo();
    EXPECT_TRUE(controller.mediaBin().empty());
    controller.undoStack().redo();
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    EXPECT_EQ(controller.mediaBin()[0].name, "frame (3 frames)");
}

TEST(ProjectController, loneFrameStaysAPlainMediaEntry) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/shot001.png");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");

    ProjectController controller;
    controller.newProject(QStringLiteral("Plain"));
    // No sibling frames => ordinary media entry with the literal path.
    ASSERT_TRUE(controller.addToMediaBin(path));
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    EXPECT_EQ(controller.mediaBin()[0].path, stl(path));
    EXPECT_EQ(controller.mediaBin()[0].name, "shot001.png");
}

namespace {

const bl::MediaBinFolder* findFolder(const ProjectController& controller,
                                     const std::string& id) {
    for (const bl::MediaBinFolder& folder : controller.folders()) {
        if (folder.id == id) return &folder;
    }
    return nullptr;
}

} // namespace

TEST(ProjectController, addFolderIsUndoable) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    const std::string id = controller.addFolder(QStringLiteral("Footage"));
    EXPECT_FALSE(id.empty());
    ASSERT_EQ(controller.folders().size(), 1u);
    EXPECT_EQ(controller.folders()[0].id, id);
    EXPECT_EQ(controller.folders()[0].name, "Footage");
    EXPECT_EQ(controller.folders()[0].parentId, "");
    EXPECT_TRUE(controller.dirty());

    controller.undoStack().undo();
    EXPECT_TRUE(controller.folders().empty());
    EXPECT_FALSE(controller.dirty());

    controller.undoStack().redo();
    ASSERT_EQ(controller.folders().size(), 1u);
    EXPECT_EQ(controller.folders()[0].name, "Footage");
}

TEST(ProjectController, addFolderNestsAndRejectsBadInput) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    const std::string parent = controller.addFolder(QStringLiteral("Parent"));
    const std::string child =
        controller.addFolder(QStringLiteral("Child"), parent);
    ASSERT_EQ(controller.folders().size(), 2u);
    ASSERT_NE(findFolder(controller, child), nullptr);
    EXPECT_EQ(findFolder(controller, child)->parentId, parent);

    // Empty names and unknown parents are refused without touching the tree.
    EXPECT_TRUE(controller.addFolder(QString()).empty());
    EXPECT_TRUE(controller.addFolder(QStringLiteral("Orphan"), "nope").empty());
    EXPECT_EQ(controller.folders().size(), 2u);
}

TEST(ProjectController, renameFolderIsUndoable) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    const std::string id = controller.addFolder(QStringLiteral("Old"));
    ASSERT_TRUE(controller.renameFolder(id, QStringLiteral("New")));
    ASSERT_NE(findFolder(controller, id), nullptr);
    EXPECT_EQ(findFolder(controller, id)->name, "New");

    // Renaming to the same name is a no-op (nothing to undo).
    EXPECT_FALSE(controller.renameFolder(id, QStringLiteral("New")));
    EXPECT_FALSE(controller.renameFolder("missing", QStringLiteral("X")));
    EXPECT_FALSE(controller.renameFolder(id, QString()));

    controller.undoStack().undo();
    ASSERT_NE(findFolder(controller, id), nullptr);
    EXPECT_EQ(findFolder(controller, id)->name, "Old");

    controller.undoStack().redo();
    ASSERT_NE(findFolder(controller, id), nullptr);
    EXPECT_EQ(findFolder(controller, id)->name, "New");
}

TEST(ProjectController, removeFolderRehomesMediaAndChildren) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    ASSERT_TRUE(controller.addToMediaBin(QStringLiteral("/media/a.mp4")));
    const std::string mediaId = controller.mediaBin()[0].id;

    const std::string parent = controller.addFolder(QStringLiteral("Parent"));
    const std::string child =
        controller.addFolder(QStringLiteral("Child"), parent);
    const std::string sibling = controller.addFolder(QStringLiteral("Sibling"));
    ASSERT_TRUE(controller.moveMediaToFolder(mediaId, parent));
    EXPECT_EQ(controller.mediaBin()[0].folderId, parent);

    ASSERT_TRUE(controller.removeFolder(parent));
    ASSERT_EQ(controller.folders().size(), 2u);  // child + sibling
    // Media is rehomed to the root...
    EXPECT_EQ(controller.mediaBin()[0].folderId, "");
    // ...and so is the child folder; the sibling is untouched.
    ASSERT_NE(findFolder(controller, child), nullptr);
    EXPECT_EQ(findFolder(controller, child)->parentId, "");
    ASSERT_NE(findFolder(controller, sibling), nullptr);
    EXPECT_EQ(findFolder(controller, sibling)->parentId, "");

    // One undo restores the folder, the media membership and the child link.
    controller.undoStack().undo();
    ASSERT_EQ(controller.folders().size(), 3u);
    ASSERT_NE(findFolder(controller, parent), nullptr);
    EXPECT_EQ(controller.mediaBin()[0].folderId, parent);
    EXPECT_EQ(findFolder(controller, child)->parentId, parent);

    controller.undoStack().redo();
    ASSERT_EQ(controller.folders().size(), 2u);
    EXPECT_EQ(controller.mediaBin()[0].folderId, "");
    EXPECT_EQ(findFolder(controller, child)->parentId, "");
}

TEST(ProjectController, moveMediaToFolderValidatesAndUndoes) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));
    ASSERT_TRUE(controller.addToMediaBin(QStringLiteral("/media/a.mp4")));
    const std::string mediaId = controller.mediaBin()[0].id;
    const std::string folder = controller.addFolder(QStringLiteral("Folder"));

    // Unknown folder and no-op moves are refused.
    EXPECT_FALSE(controller.moveMediaToFolder(mediaId, "missing"));
    EXPECT_FALSE(controller.moveMediaToFolder("missing", folder));
    EXPECT_FALSE(controller.moveMediaToFolder(mediaId, ""));
    EXPECT_EQ(controller.mediaBin()[0].folderId, "");

    ASSERT_TRUE(controller.moveMediaToFolder(mediaId, folder));
    EXPECT_EQ(controller.mediaBin()[0].folderId, folder);

    ASSERT_TRUE(controller.moveMediaToFolder(mediaId, ""));
    EXPECT_EQ(controller.mediaBin()[0].folderId, "");

    controller.undoStack().undo();
    EXPECT_EQ(controller.mediaBin()[0].folderId, folder);
    controller.undoStack().undo();
    EXPECT_EQ(controller.mediaBin()[0].folderId, "");
}

TEST(ProjectController, moveFolderRejectsCyclesAndUndoes) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));

    const std::string a = controller.addFolder(QStringLiteral("A"));
    const std::string b = controller.addFolder(QStringLiteral("B"), a);
    const std::string d = controller.addFolder(QStringLiteral("D"));

    // Self, missing parent and descendant targets are all refused.
    EXPECT_FALSE(controller.moveFolder(a, a));
    EXPECT_FALSE(controller.moveFolder(a, "missing"));
    EXPECT_FALSE(controller.moveFolder(a, b));  // b is a descendant of a
    EXPECT_EQ(findFolder(controller, a)->parentId, "");

    // Moving D under A is fine; moving A under D (its descendant) is not.
    ASSERT_TRUE(controller.moveFolder(d, a));
    EXPECT_EQ(findFolder(controller, d)->parentId, a);
    EXPECT_FALSE(controller.moveFolder(a, d));

    // Moving B to the root is fine and undoable.
    ASSERT_TRUE(controller.moveFolder(b, ""));
    EXPECT_EQ(findFolder(controller, b)->parentId, "");

    controller.undoStack().undo();
    EXPECT_EQ(findFolder(controller, b)->parentId, a);
    controller.undoStack().redo();
    EXPECT_EQ(findFolder(controller, b)->parentId, "");
}

TEST(ProjectController, foldersRoundTripThroughSaveOpen) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("folders.blproj"));

    std::string folderId;
    std::string mediaId;
    {
        ProjectController controller;
        controller.newProject(QStringLiteral("Folders"));
        ASSERT_TRUE(controller.addToMediaBin(QStringLiteral("/media/a.mp4")));
        mediaId = controller.mediaBin()[0].id;
        folderId = controller.addFolder(QStringLiteral("Footage"));
        ASSERT_FALSE(folderId.empty());
        ASSERT_TRUE(controller.moveMediaToFolder(mediaId, folderId));

        const auto saved = controller.saveAs(path);
        ASSERT_TRUE(saved.ok()) << saved.message();
    }

    {
        ProjectController controller;
        const auto loaded = controller.open(path);
        ASSERT_TRUE(loaded.ok()) << loaded.message();
        ASSERT_EQ(controller.folders().size(), 1u);
        EXPECT_EQ(controller.folders()[0].id, folderId);
        EXPECT_EQ(controller.folders()[0].name, "Footage");
        ASSERT_EQ(controller.mediaBin().size(), 1u);
        EXPECT_EQ(controller.mediaBin()[0].folderId, folderId);
    }
}

TEST(ProjectController, newProjectFromTemplateCreatesCleanUnsavedProject) {
    ProjectController controller;
    const bl::ProjectTemplate* tmpl = bl::findTemplate("1080p30-standard");
    ASSERT_NE(tmpl, nullptr);

    ASSERT_TRUE(controller.newProjectFromTemplate(tmpl->id,
                                                  QStringLiteral("From Template")));

    EXPECT_TRUE(controller.isOpen());
    EXPECT_TRUE(controller.dirty());
    EXPECT_TRUE(controller.filePath().isEmpty());
    EXPECT_EQ(stl(controller.projectName()), "From Template");
    EXPECT_TRUE(controller.mediaBin().empty());

    const bl::Sequence& seq = controller.timeline().sequence();
    EXPECT_EQ(seq.name, "From Template");
    EXPECT_EQ(seq.settings, tmpl->settings);
    ASSERT_EQ(seq.videoTracks.size(), 1u);
    ASSERT_EQ(seq.audioTracks.size(), 1u);
    EXPECT_EQ(seq.videoTracks[0].kind(), bl::TrackKind::Video);
    EXPECT_EQ(seq.audioTracks[0].kind(), bl::TrackKind::Audio);

    // Adoption clears the undo stack; the dirty flag alone marks the project
    // as needing a save.
    EXPECT_FALSE(controller.canUndo());
}

TEST(ProjectController, newProjectFromTemplateAppliesSeedTrackEffects) {
    ProjectController controller;
    const bl::ProjectTemplate* tmpl = bl::findTemplate("audio-podcast");
    ASSERT_NE(tmpl, nullptr);

    ASSERT_TRUE(controller.newProjectFromTemplate(tmpl->id,
                                                  QStringLiteral("Podcast")));
    const bl::Sequence& seq = controller.timeline().sequence();
    ASSERT_EQ(seq.videoTracks.size(), 1u);
    ASSERT_EQ(seq.audioTracks.size(), 2u);
    ASSERT_FALSE(tmpl->trackEffects.empty());
    ASSERT_EQ(seq.videoTracks[0].effects().size(), tmpl->trackEffects.size());
    EXPECT_EQ(seq.videoTracks[0].effects()[0].effectId,
              tmpl->trackEffects[0].effectId);
}

TEST(ProjectController, newProjectFromTemplateUnknownIdChangesNothing) {
    ProjectController controller;
    controller.newProject(QStringLiteral("Demo"));
    ASSERT_FALSE(controller.dirty());

    EXPECT_FALSE(
        controller.newProjectFromTemplate("no-such-template", QStringLiteral("X")));

    // The open document is untouched: same name, settings and (clean) state.
    EXPECT_EQ(stl(controller.projectName()), "Demo");
    EXPECT_FALSE(controller.dirty());
    EXPECT_EQ(controller.timeline().sequence().settings,
              bl::SequenceSettings{});
    EXPECT_EQ(controller.timeline().sequence().name, "Demo");
}
