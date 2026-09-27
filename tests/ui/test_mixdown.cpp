#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_core/result.hpp>
#include <bl_core/time.hpp>
#include <bl_export/audio_pcm_source.hpp>
#include <bl_export/mixdown_audio_renderer.hpp>
#include <bl_plugins/codec_plugin.h>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/keyframes.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/track.hpp>

#include <QApplication>
#include <QDir>
#include <QString>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

namespace {

using bl::CodecRegistry;
using bl::Duration;
using bl::Rational;
using bl::Time;
using bl::export_::TimelineMixdown;
using bl::export_::TimelineMixdownOptions;

const Rational kFps{24, 1};
const Rational kRate{1'000'000, 1};

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

Time fr(int64_t frame) { return Time::fromFrameAt(frame, kFps, kRate); }
Duration durF(int64_t frames) { return Duration::fromFrames(frames, kFps); }

std::string mediaDir() { return std::string(BL_TEST_MEDIA_DIR); }
std::string pluginDir() { return std::string(BL_TEST_PLUGIN_DIR) + "/plugins"; }

BlHostApi makeHost() {
    BlHostApi api{};
    api.host_abi_version = BL_PLUGIN_ABI_VERSION;
    api.alloc = [](size_t size, void*) { return std::malloc(size); };
    api.free = [](void* ptr, void*) { std::free(ptr); };
    api.userdata = nullptr;
    return api;
}

void loadPlugins(CodecRegistry& registry,
                 std::vector<std::unique_ptr<bl::PluginHandle>>& handles) {
    (void)bl::registerBuiltins(registry);
    bl::PluginLoader loader;
    auto report = loader.scanDirectory(pluginDir(), bl::PluginOrigin::User);
    if (!report.ok()) {
        return;
    }
    for (auto& handle : report.value().plugins) {
        const BlCodecPlugin* plugin = handle->plugin();
        if (!plugin) continue;
        if (registry
                .registerPlugin(const_cast<BlCodecPlugin*>(plugin))
                .ok()) {
            handles.push_back(std::move(handle));
        }
    }
}

struct MixFixture {
    bl::Sequence seq;
    CodecRegistry registry;
    std::vector<std::unique_ptr<bl::PluginHandle>> handles;
    BlHostApi host;

    MixFixture()
        : host(makeHost()) {
        ensureApp();
        seq.settings.fps = kFps;
        seq.settings.sampleRate = 48000;
        seq.addAudioTrack("A1");
    }

    bool load() {
        loadPlugins(registry, handles);
        return registry.count() > 0;
    }

    void addClip(const std::string& mediaItemId, const std::string& clipId,
                 double gain = 1.0, double pan = 0.0) {
        bl::Clip clip;
        clip.id = clipId;
        clip.source.mediaItemId = mediaItemId;
        clip.source.sourceIn = fr(0);
        clip.source.sourceOut = fr(48);
        clip.timelineStart = fr(0);
        clip.timelineDuration = durF(48);
        clip.audio.gain = gain;
        clip.audio.pan = pan;
        seq.audioTracks[0].addClip(clip);
    }

    bl::Result<std::string> resolve(const std::string& id) const {
        if (id == "av") return bl::Result<std::string>::ok(mediaDir() + "/test_av.mp4");
        if (id == "opus") return bl::Result<std::string>::ok(mediaDir() + "/test_audio_opus.opus");
        if (id == "beep") return bl::Result<std::string>::ok(mediaDir() + "/test_audio_beep.opus");
        return bl::Result<std::string>::err(bl::Err::FileNotFound, "unknown media id");
    }
};

double rmsOf(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (double s : samples) sum += s * s;
    const double mean = sum / static_cast<double>(samples.size());
    return std::sqrt(mean);
}

struct Collected {
    std::vector<float> samples;
    uint64_t totalSamples{0};
    uint64_t blocks{0};
};

bool collectAll(const TimelineMixdownOptions& opts, const MixFixture& fx,
                Collected& out) {
    TimelineMixdownOptions effective = opts;
    effective.host = &fx.host;
    const auto resolver =
        [&](const std::string& id) -> bl::Result<std::string> {
        return fx.resolve(id);
    };
    return TimelineMixdown::render(
        fx.seq, effective, fx.registry, resolver,
        [&](const float* const* planes, uint32_t samples) {
            for (uint32_t c = 0; c < effective.channels; ++c) {
                out.samples.insert(out.samples.end(), planes[c],
                                   planes[c] + samples);
            }
            out.totalSamples += samples;
            out.blocks += 1;
            return true;
        }).ok();
}

TEST(Mixdown, RendersClipAudioAtSequenceRate) {
    MixFixture fx;
    ASSERT_TRUE(fx.load());
    fx.addClip("av", "c1");

    const uint64_t expected = bl::export_::timelineMixdownSampleCount(fx.seq, 48000);
    EXPECT_EQ(expected, 48000u * 2u);

    TimelineMixdownOptions opts;
    opts.sampleRate = 48000;
    opts.channels = 2;
    opts.blockSamples = 4096;
    Collected c;
    ASSERT_TRUE(collectAll(opts, fx, c));
    EXPECT_EQ(c.totalSamples, expected);
    EXPECT_GT(rmsOf(c.samples), 0.02) << "sine clip should not be silent";
}

TEST(Mixdown, MutedTrackIsSilent) {
    MixFixture fx;
    ASSERT_TRUE(fx.load());
    fx.addClip("av", "c1");
    fx.seq.audioTracks[0].setMuted(true);

    TimelineMixdownOptions opts;
    opts.sampleRate = 48000;
    opts.channels = 2;
    Collected c;
    ASSERT_TRUE(collectAll(opts, fx, c));
    EXPECT_TRUE(c.samples.empty())
        << "a muted track contributes nothing to the mix";
}

TEST(Mixdown, ClipGainScalesAmplitude) {
    MixFixture refFx;
    ASSERT_TRUE(refFx.load());
    refFx.addClip("av", "c1", 1.0);
    TimelineMixdownOptions opts;
    opts.sampleRate = 48000;
    opts.channels = 2;
    Collected full;
    ASSERT_TRUE(collectAll(opts, refFx, full));

    MixFixture halfFx;
    ASSERT_TRUE(halfFx.load());
    halfFx.addClip("av", "c1", 0.5);
    Collected half;
    ASSERT_TRUE(collectAll(opts, halfFx, half));

    const double ratio =
        half.samples.empty() || full.samples.empty()
            ? 0.0
            : rmsOf(half.samples) / rmsOf(full.samples);
    EXPECT_NEAR(ratio, 0.5, 0.1);
}

TEST(Mixdown, ExtractWindowBoundsThePass) {
    MixFixture fx;
    ASSERT_TRUE(fx.load());
    fx.addClip("av", "c1");

    TimelineMixdownOptions opts;
    opts.sampleRate = 48000;
    opts.channels = 2;
    opts.startSample = 48000;  // second half only
    opts.maxSamples = 24000;   // half a second
    Collected c;
    ASSERT_TRUE(collectAll(opts, fx, c));
    EXPECT_EQ(c.totalSamples, 24000u);
}

namespace mixdown_detail {

TEST(MixdownDetail, ReversedClipPlaysAudioBackwards) {
    // test_audio_beep.opus is 1 s of 440 Hz tone followed by 1 s of silence,
    // so the region a clip pulls from is directly observable.
    TimelineMixdownOptions opts;
    opts.sampleRate = 48000;
    opts.channels = 2;

    MixFixture fwdFx;
    ASSERT_TRUE(fwdFx.load());
    fwdFx.addClip("beep", "fwd");
    Collected fwd;
    ASSERT_TRUE(collectAll(opts, fwdFx, fwd));

    MixFixture revFx;
    ASSERT_TRUE(revFx.load());
    revFx.addClip("beep", "rev");
    ASSERT_TRUE(revFx.seq.audioTracks[0].setClipSpeed(
        "rev", bl::SpeedRemap{1, 1, true}));
    Collected rev;
    ASSERT_TRUE(collectAll(opts, revFx, rev));

    EXPECT_EQ(fwd.samples.size(), rev.samples.size());
    ASSERT_GE(fwd.samples.size(), 192000u);  // 2 s x 2 channels

    const size_t half = fwd.samples.size() / 2;
    const auto rmsRange = [](const std::vector<float>& s, size_t b,
                             size_t e) {
        return rmsOf(std::vector<float>(s.begin() + static_cast<ptrdiff_t>(b),
                                        s.begin() + static_cast<ptrdiff_t>(e)));
    };
    const double fwdBeep = rmsRange(fwd.samples, 0, half);
    const double fwdSilence = rmsRange(fwd.samples, half, fwd.samples.size());
    const double revSilence = rmsRange(rev.samples, 0, half);
    const double revBeep = rmsRange(rev.samples, half, rev.samples.size());

    // Opus decode bleeds a little tone across the tone/silence boundary, so
    // compare regions *relative* to each other instead of absolute silence.
    EXPECT_GT(fwdBeep, 0.05) << "forward must start with the tone";
    EXPECT_GT(fwdBeep, 4.0 * fwdSilence) << "forward must end with silence";
    EXPECT_GT(revBeep, 4.0 * revSilence)
        << "a reversed clip must NOT start with the tone";
    EXPECT_GT(revBeep, 0.05)
        << "a reversed clip must deliver the tone at the end";

    EXPECT_NEAR(rmsOf(rev.samples), rmsOf(fwd.samples), 0.01);
    EXPECT_GT(rmsOf(rev.samples), 0.02) << "reversed audio should not be silent";
}

TEST(MixdownDetail, PcmAudioSourceDecodesOpusToEnd) {
    MixFixture fx;
    ASSERT_TRUE(fx.load());
    BlHostApi host = makeHost();

    bl::export_::PcmAudioSource::OpenRequest req;
    req.path = mediaDir() + "/test_audio_opus.opus";
    req.targetSampleRate = 48000;
    req.host = &host;

    auto src = bl::export_::PcmAudioSource::open(req, fx.registry);
    ASSERT_TRUE(src.ok()) << src.message();
    EXPECT_EQ(src.value().channels(), 2u);

    std::vector<float> buf;
    buf.reserve(96000);
    float left[4096];
    float right[4096];
    uint32_t got = 0;
    int guard = 0;
    do {
        got = src.value().pull(left, right, 4096);
        buf.insert(buf.end(), left, left + got);
    } while (got > 0 && buf.size() < 96000 && ++guard < 100);

    EXPECT_GT(buf.size(), 40000u) << "opus fixture is ~1s at 48 kHz";
    EXPECT_GT(rmsOf(buf), 0.05) << "440 Hz sine should not decode to silence";
}

}  // namespace mixdown_detail

}  // namespace