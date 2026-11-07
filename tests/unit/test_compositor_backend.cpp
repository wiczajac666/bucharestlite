#include <bl_render/composite_layer.hpp>
#include <bl_render/compositor.hpp>
#include <bl_render/compositor_backend.hpp>
#include <bl_timeline/timeline.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using bl::Clip;
using bl::CompositeLayer;
using bl::Compositor;
using bl::CompositorConfig;
using bl::Duration;
using bl::Frame;
using bl::ICompositorBackend;
using bl::IDecodeProvider;
using bl::Rational;
using bl::Sequence;
using bl::Time;
using bl::TimelineSnapshot;
using bl::Track;
using bl::VideoTrack;

const Rational kFps{24, 1};

Time frame(int64_t f) { return Time::fromFrame(f, kFps); }
Duration dur(int64_t f) { return Duration::fromFrames(f, kFps); }

CompositeLayer solidLayer(uint32_t width, uint32_t height, uint8_t b, uint8_t g,
                          uint8_t r, uint8_t a, double opacity = 1.0) {
    CompositeLayer layer;
    layer.width = width;
    layer.height = height;
    layer.linesize = width * 4;
    layer.opacity = opacity;
    layer.data.resize(static_cast<size_t>(layer.linesize) * height);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = layer.data.data() + static_cast<size_t>(y) * layer.linesize;
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = a;
        }
    }
    return layer;
}

// Solid-colour frame source, matching what the compositor asks the decoder for.
class SolidProvider : public IDecodeProvider {
public:
    bl::Result<Frame> getFrame(const std::string& /*mediaItemId*/,
                               Time /*sourceTime*/, uint32_t width,
                               uint32_t height) override {
        Frame f;
        f.type = Frame::Type::Video;
        f.width = width;
        f.height = height;
        f.linesize = width * 4;
        f.dataSize = static_cast<size_t>(width) * height * 4;
        f.data = new uint8_t[f.dataSize];
        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = f.data + static_cast<size_t>(y) * f.linesize;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 4 + 0] = 255;  // B
                row[x * 4 + 1] = 0;    // G
                row[x * 4 + 2] = 0;    // R
                row[x * 4 + 3] = 255;  // A
            }
        }
        return bl::Result<Frame>::ok(std::move(f));
    }
};

// Delegating backend that records how many times the compositor used it.
class RecordingBackend final : public ICompositorBackend {
public:
    std::string_view name() const override { return "recording"; }
    void composite(std::vector<uint8_t>& out, uint32_t outWidth,
                   uint32_t outHeight, uint32_t outLinesize,
                   const std::vector<CompositeLayer>& layers) override {
        ++calls;
        lastLayerCount = layers.size();
        cpu_->composite(out, outWidth, outHeight, outLinesize, layers);
    }

    int calls{0};
    size_t lastLayerCount{0};
    std::unique_ptr<ICompositorBackend> cpu_ = bl::makeCpuCompositorBackend();
};

Sequence singleClipSequence() {
    Sequence seq;
    seq.settings.width = 4;
    seq.settings.height = 2;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "media";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(10);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(10);
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));
    return seq;
}

TEST(CpuCompositorBackend, BlendsLayersInOrderAndClearsFirst) {
    auto backend = bl::makeCpuCompositorBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->name(), "cpu");
    EXPECT_FALSE(backend->gpu());

    std::vector<uint8_t> out(2 * 4, 0xEE);  // stale canvas
    const std::vector<CompositeLayer> layers = {
        solidLayer(2, 1, 0, 0, 255, 255),             // red, opaque
        solidLayer(2, 1, 255, 0, 0, 255, 0.5),        // blue at 50 %
    };
    backend->composite(out, 2, 1, 8, layers);

    // Second layer covers half: R = 255 * 0.5 = 127 (truncated), B = 127.
    for (size_t x = 0; x < 2; ++x) {
        EXPECT_EQ(out[x * 4 + 0], 127u);
        EXPECT_EQ(out[x * 4 + 1], 0u);
        EXPECT_EQ(out[x * 4 + 2], 127u);
        EXPECT_EQ(out[x * 4 + 3], 255u);
    }
}

TEST(CpuCompositorBackend, EmptyStackLeavesTransparentCanvas) {
    auto backend = bl::makeCpuCompositorBackend();
    std::vector<uint8_t> out(3 * 4, 0xFF);
    backend->composite(out, 3, 1, 12, {});
    for (uint8_t value : out) EXPECT_EQ(value, 0u);
}

TEST(CpuCompositorBackend, SkipsZeroOpacityAndSmallerLayers) {
    auto backend = bl::makeCpuCompositorBackend();
    std::vector<uint8_t> out(4 * 2 * 4, 0);
    const std::vector<CompositeLayer> layers = {
        solidLayer(2, 1, 0, 255, 0, 255),  // green, covers top-left 2x1
        solidLayer(4, 2, 255, 0, 0, 255, 0.0),  // blue at zero opacity
    };
    backend->composite(out, 4, 2, 16, layers);
    EXPECT_EQ(out[0 * 4 + 1], 255u);  // covered pixel
    EXPECT_EQ(out[0 * 4 + 0], 0u);
    EXPECT_EQ(out[3 * 4 + 0], 0u);  // outside the smaller layer
    EXPECT_EQ(out[3 * 4 + 1], 0u);
}

TEST(CompositorBackend, DefaultsToCpu) {
    auto comp = Compositor::create({2, 2});
    ASSERT_TRUE(comp.ok()) << comp.message();
    EXPECT_EQ(comp->backendName(), "cpu");
    EXPECT_FALSE(comp->usingGpu());
}

TEST(CompositorBackend, UsesInjectedBackendForEachFrame) {
    const CompositorConfig config{4, 2};
    auto backend = std::make_unique<RecordingBackend>();
    RecordingBackend* raw = backend.get();

    auto comp = Compositor::create(config, std::move(backend));
    ASSERT_TRUE(comp.ok()) << comp.message();
    EXPECT_EQ(comp->backendName(), "recording");

    TimelineSnapshot snap(singleClipSequence());
    SolidProvider provider;

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    EXPECT_EQ(raw->calls, 1);
    EXPECT_EQ(raw->lastLayerCount, 1u);

    // The recording backend delegates to the CPU blender, so the picture is
    // the same concrete result as the portable path.
    EXPECT_EQ(result->data[0], 255u);  // B
    EXPECT_EQ(result->data[3], 255u);  // A

    auto second = comp->renderFrame(snap, frame(6), provider);
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(raw->calls, 2);
}

TEST(CompositorBackend, InjectedBackendReceivesNoLayersWhenNothingRenders) {
    auto backend = std::make_unique<RecordingBackend>();
    RecordingBackend* raw = backend.get();
    auto comp = Compositor::create({4, 2}, std::move(backend));
    ASSERT_TRUE(comp.ok());

    Sequence empty;
    empty.settings.width = 4;
    empty.settings.height = 2;
    TimelineSnapshot snap(std::move(empty));
    SolidProvider provider;

    auto result = comp->renderFrame(snap, frame(0), provider);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(raw->calls, 1);
    EXPECT_EQ(raw->lastLayerCount, 0u);
    for (uint8_t value : result->data) EXPECT_EQ(value, 0u);
}

} // namespace
