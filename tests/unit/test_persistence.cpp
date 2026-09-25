#include <bl_core/autosave_ring.hpp>
#include <bl_core/project_repository.hpp>
#include <bl_timeline/sequence.hpp>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

using bl::AutosaveEntry;
using bl::AutosaveRing;
using bl::Err;
using bl::LoadReport;
using bl::MediaBinItem;
using bl::ProjectData;
using bl::ProjectRepository;
using bl::ProjectSettings;

class TempDir {
public:
    TempDir() {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("bl_core6_" + std::to_string(stamp));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const { return path_; }
    std::string stringPath() const { return path_.generic_string(); }
    std::string child(const std::string& name) const {
        return (path_ / name).generic_string();
    }

private:
    fs::path path_;
};

ProjectData sampleProject() {
    ProjectData data;
    data.settings.name = "My Documentary";
    data.settings.author = "L Hustla";
    data.settings.createdIso8601 = "2026-08-25T12:00:00Z";

    data.mediaBin.push_back({"item-1", "media/interview.mp4", "Interview"});
    data.mediaBin.push_back({"item-2", "/absolutely/elsewhere/broll.mp4",
                             "B-Roll"});

    data.extensions["sequences"] = nlohmann::json::array(
        {{{"id", "seq-1"}, {"fps", {{"num", 24000}, {"den", 1001}}}}});
    data.extensions["futureMarker"] = true;
    return data;
}

TEST(ProjectRepositoryTest, RoundTripPreservesEverything) {
    TempDir dir;
    ProjectRepository repo;
    const std::string file = dir.child("project.blproj");

    ASSERT_TRUE(repo.save(sampleProject(), file).ok());

    auto loaded = repo.load(file);
    ASSERT_TRUE(loaded.ok()) << loaded.message();

    const ProjectData& restored = (*loaded).project;
    EXPECT_EQ(restored.settings.name, "My Documentary");
    EXPECT_EQ(restored.settings.author, "L Hustla");
    EXPECT_EQ(restored.settings.createdIso8601, "2026-08-25T12:00:00Z");

    ASSERT_EQ(restored.mediaBin.size(), 2u);
    EXPECT_EQ(restored.mediaBin[0].id, "item-1");
    EXPECT_EQ(restored.mediaBin[0].name, "Interview");
    EXPECT_EQ(restored.mediaBin[1].path,
              fs::absolute(fs::path("/absolutely/elsewhere/broll.mp4"))
                  .generic_string());
    EXPECT_EQ((*loaded).missingMedia.size(), 2u);

    const nlohmann::json& ext = restored.extensions;
    ASSERT_TRUE(ext.contains("sequences"));
    EXPECT_EQ(ext["sequences"][0]["id"], "seq-1");
    EXPECT_EQ(ext["sequences"][0]["fps"]["num"], 24000);
    EXPECT_EQ(ext["futureMarker"], true);
}

TEST(ProjectRepositoryTest, ToJsonRoundTripsWithRelativeBase) {
    TempDir dir;
    ProjectRepository repo;
    const std::string file = dir.child("project.blproj");

    ProjectData data = sampleProject();
    data.mediaBin[0].path = dir.child("media/interview.mp4");

    const nlohmann::json doc = repo.toJson(data, file);
    EXPECT_EQ(doc["schemaVersion"], 1);
    ASSERT_TRUE(doc["mediaBin"].is_array());
    EXPECT_EQ(doc["mediaBin"][0]["path"], "media/interview.mp4");

    auto loaded = repo.fromDocument(doc.dump(2), file);
    ASSERT_TRUE(loaded.ok()) << loaded.message();
    EXPECT_EQ((*loaded).project.settings.name, "My Documentary");
    ASSERT_EQ((*loaded).project.mediaBin.size(), 2u);
    EXPECT_EQ((*loaded).project.mediaBin[0].path,
              (dir.path() / "media/interview.mp4").generic_string());
}

TEST(ProjectRepositoryTest, SavedDocumentIsReadableJson) {
    TempDir dir;
    ProjectRepository repo;
    const std::string file = dir.child("readable.blproj");

    ASSERT_TRUE(repo.save(sampleProject(), file).ok());

    std::ifstream in(file);
    nlohmann::json doc = nlohmann::json::parse(in);
    EXPECT_EQ(doc["schemaVersion"], 1u);
    EXPECT_EQ(doc["settings"]["name"], "My Documentary");
    EXPECT_TRUE(doc.contains("extensions"));
}

TEST(ProjectRepositoryTest, AtomicSaveLeavesNoTempFiles) {
    TempDir dir;
    ProjectRepository repo;
    const std::string file = dir.child("atomic.blproj");

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(repo.save(sampleProject(), file).ok());
    }

    int tempCount = 0;
    for (const auto& entry : fs::directory_iterator(dir.path())) {
        if (entry.path().extension() == ".tmp") ++tempCount;
    }
    EXPECT_EQ(tempCount, 0);
}

TEST(ProjectRepositoryTest, MediaInsideProjectStoredRelative) {
    TempDir dir;
    fs::create_directories(dir.path() / "media");
    const std::string mediaFile = dir.child("media/clip.mp4");
    { std::ofstream(mediaFile) << "fake"; }

    ProjectData data;
    data.mediaBin.push_back({"m1", mediaFile, "Clip"});

    ProjectRepository repo;
    const std::string file = dir.child("proj.blproj");
    ASSERT_TRUE(repo.save(data, file).ok());

    std::ifstream in(file);
    const nlohmann::json doc = nlohmann::json::parse(in);
    EXPECT_EQ(doc["mediaBin"][0]["path"], "media/clip.mp4");

    auto loaded = repo.load(file);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ((*loaded).project.mediaBin[0].id, "m1");
    EXPECT_EQ((*loaded).missingMedia.size(), 0u);
    EXPECT_NE((*loaded).project.mediaBin[0].path.find(dir.stringPath()),
              std::string::npos);
}

TEST(ProjectRepositoryTest, RelativePathsResolveAfterDirectoryMove) {
    TempDir source;
    fs::create_directories(source.path() / "assets");
    const std::string asset = source.child("assets/a.mp4");
    { std::ofstream(asset) << "x"; }

    ProjectData data;
    data.mediaBin.push_back({"a", asset, "A"});

    ProjectRepository repo;
    const std::string projectFile = source.child("p.blproj");
    ASSERT_TRUE(repo.save(data, projectFile).ok());
    EXPECT_EQ(source.child("p.blproj"), projectFile);

    TempDir destParent;
    const fs::path movedRoot = fs::path(destParent.path()) / "moved";
    fs::copy(source.path(), movedRoot,
             fs::copy_options::recursive);

    auto loaded = repo.load((movedRoot / "p.blproj").generic_string());
    ASSERT_TRUE(loaded.ok()) << loaded.message();
    EXPECT_EQ((*loaded).missingMedia.size(), 0u);
    EXPECT_NE((*loaded).project.mediaBin[0].path.find("moved/assets"),
              std::string::npos);
}

TEST(ProjectRepositoryTest, CorruptJsonFailsWithByteOffset) {
    TempDir dir;
    const std::string file = dir.child("broken.blproj");
    { std::ofstream(file) << "{\"schemaVersion\": 1, \"settings\": oops"; }

    ProjectRepository repo;
    auto result = repo.load(file);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::JsonError);
    EXPECT_NE(result.message().find("byte"), std::string::npos);
}

TEST(ProjectRepositoryTest, NewerSchemaVersionRejected) {
    TempDir dir;
    const std::string file = dir.child("future.blproj");
    { std::ofstream(file) << R"({"schemaVersion": 999, "settings": {}})"; }

    ProjectRepository repo;
    auto result = repo.load(file);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::JsonError);
    EXPECT_NE(result.message().find("newer"), std::string::npos);
}

TEST(ProjectRepositoryTest, OlderSchemaMigratesForward) {
    TempDir dir;
    const std::string file = dir.child("legacy.blproj");
    { std::ofstream(file)
          << R"({"schemaVersion": 0, "projectName": "Legacy Cut", "author": "Old Me"})"; }

    ProjectRepository repo;
    auto result = repo.load(file);
    ASSERT_TRUE(result.ok()) << result.message();
    EXPECT_EQ((*result).project.settings.name, "Legacy Cut");
    EXPECT_EQ((*result).project.settings.author, "Old Me");
    EXPECT_TRUE((*result).project.mediaBin.empty());
}

TEST(ProjectRepositoryTest, MissingFileReportsNotFound) {
    ProjectRepository repo;
    auto result = repo.load("/nonexistent/x.blproj");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::FileNotFound);
}

TEST(AutosaveRingTest, RotateCreatesSequentialSlots) {
    TempDir dir;
    AutosaveRing ring("proj_a", 3, dir.stringPath());
    EXPECT_NE(ring.directory().find("autosave/proj_a"), std::string::npos);

    nlohmann::json doc = {{"schemaVersion", 1}};
    auto first = ring.rotate(doc);
    auto second = ring.rotate(doc);
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_NE(*first, *second);
    EXPECT_EQ(ring.list().size(), 2u);
}

TEST(AutosaveRingTest, RingWrapsAndPrunesAtCapacity) {
    TempDir dir;
    AutosaveRing ring("proj_b", 3, dir.stringPath());

    for (int i = 0; i < 5; ++i) {
        auto slot = ring.rotate({{"generation", i}});
        ASSERT_TRUE(slot.ok()) << slot.message();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    EXPECT_EQ(ring.list().size(), 3u);
    auto newest = ring.newest();
    ASSERT_TRUE(newest.ok());
    EXPECT_EQ(ring.read(newest->path).ok(), true);
    EXPECT_EQ((*ring.read(newest->path))["generation"], 4);
}

TEST(AutosaveRingTest, ListIsNewestFirst) {
    TempDir dir;
    AutosaveRing ring("proj_c", 5, dir.stringPath());

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(ring.rotate({{"gen", i}}).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    const auto entries = ring.list();
    ASSERT_EQ(entries.size(), 4u);
    for (size_t i = 1; i < entries.size(); ++i) {
        EXPECT_GT(entries[i - 1].modifiedKey, entries[i].modifiedKey);
    }
    EXPECT_EQ(entries.front().modifiedKey,
              ring.newest()->modifiedKey);
}

TEST(AutosaveRingTest, NewestWithoutEntriesReportsNotFound) {
    TempDir dir;
    AutosaveRing ring("empty_project", 4, dir.stringPath());
    auto result = ring.newest();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::FileNotFound);
}

TEST(AutosaveRingTest, RemoveAllClearsSlots) {
    TempDir dir;
    AutosaveRing ring("doomed", 4, dir.stringPath());
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(ring.rotate({{"gen", i}}).ok());
    }
    ring.removeAll();
    EXPECT_EQ(ring.list().size(), 0u);
    EXPECT_FALSE(ring.newest().ok());
}

TEST(AutosaveRecoveryTest, DetectsNewerAutosave) {
    TempDir dir;
    const std::string main = dir.child("main.blproj");
    const std::string autosave = dir.child("auto.blproj");

    { std::ofstream(main) << "main"; }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    { std::ofstream(autosave) << "autosave"; }

    EXPECT_TRUE(bl::isAutosaveNewerThan(autosave, main));
    EXPECT_FALSE(bl::isAutosaveNewerThan(main, autosave));

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    { std::ofstream(main) << "main updated"; }
    EXPECT_FALSE(bl::isAutosaveNewerThan(autosave, main));
}

TEST(AutosaveRecoveryTest, AutosaveOnlyExistsMeansRecoveryCandidate) {
    TempDir dir;
    const std::string autosave = dir.child("orphan.blproj");
    { std::ofstream(autosave) << "data"; }

    EXPECT_TRUE(
        bl::isAutosaveNewerThan(autosave, dir.child("never_saved.blproj")));
    EXPECT_FALSE(
        bl::isAutosaveNewerThan(dir.child("ghost.blproj"),
                                dir.child("anything.blproj")));
}

TEST(AutosaveRecoveryTest, TiedModificationTimesFallBackToContent) {
    TempDir dir;
    const std::string main = dir.child("main.blproj");
    const std::string autosave = dir.child("auto.blproj");

    { std::ofstream(main) << "saved state"; }
    { std::ofstream(autosave) << "unsaved edits"; }
    const auto pinned =
        std::filesystem::file_time_type::clock::now() -
        std::chrono::seconds(120);
    std::error_code ec;
    std::filesystem::last_write_time(main, pinned, ec);
    std::filesystem::last_write_time(autosave, pinned, ec);
    ASSERT_FALSE(ec);

    EXPECT_TRUE(bl::isAutosaveNewerThan(autosave, main));

    { std::ofstream(main) << "unsaved edits"; }
    std::filesystem::last_write_time(main, pinned, ec);
    ASSERT_FALSE(ec);
    EXPECT_FALSE(bl::isAutosaveNewerThan(autosave, main));
}

TEST(PersistenceTest, TrackMixerStateRoundTrips) {
    bl::VideoTrack track("A1", bl::TrackKind::Audio);
    track.setMuted(true);
    track.setSoloed(false);
    track.setGain(0.25);
    track.setPan(-0.5);

    nlohmann::json j = track;
    bl::VideoTrack restored;
    from_json(j, restored);

    EXPECT_EQ(restored.name(), "A1");
    EXPECT_TRUE(restored.muted());
    EXPECT_FALSE(restored.soloed());
    EXPECT_DOUBLE_EQ(restored.gain(), 0.25);
    EXPECT_DOUBLE_EQ(restored.pan(), -0.5);

    // Old projects (no gain/pan keys) must still load with defaults.
    j.erase("gain");
    j.erase("pan");
    bl::VideoTrack legacy;
    from_json(j, legacy);
    EXPECT_DOUBLE_EQ(legacy.gain(), 1.0);
    EXPECT_DOUBLE_EQ(legacy.pan(), 0.0);
}

TEST(PersistenceTest, SequenceMasterStateRoundTrips) {
    bl::SequenceSettings s;
    s.masterGain = 0.7;
    s.masterPan = 0.2;

    nlohmann::json j = s;
    bl::SequenceSettings restored;
    from_json(j, restored);
    EXPECT_DOUBLE_EQ(restored.masterGain, 0.7);
    EXPECT_DOUBLE_EQ(restored.masterPan, 0.2);

    j.erase("masterGain");
    j.erase("masterPan");
    bl::SequenceSettings legacy;
    from_json(j, legacy);
    EXPECT_DOUBLE_EQ(legacy.masterGain, 1.0);
    EXPECT_DOUBLE_EQ(legacy.masterPan, 0.0);
}

} // namespace