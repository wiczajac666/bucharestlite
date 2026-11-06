#include <bl_audio/audio_engine.hpp>
#include <bl_audio/device_output.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace bl {
namespace {

class RecordingOutput : public IDeviceOutput {
public:
    Result<void> open(const AudioMixConfig& config) override {
        config_ = config;
        return Result<void>{};
    }
    void write(const AudioBuffer& buffer) override {
        last_ = buffer;
        ++writes_;
    }
    void close() override {}

    const AudioBuffer& last() const { return last_; }
    int writes() const { return writes_; }
    AudioMixConfig config_;

private:
    AudioBuffer last_;
    int writes_{0};
};

AudioBuffer planarBuffer(uint32_t frames, uint32_t channels, float left, float right) {
    AudioBuffer buffer;
    buffer.resize(frames, channels);
    buffer.clear();
    for (uint32_t i = 0; i < frames; ++i) {
        buffer.channelData(0)[i] = left;
        if (channels > 1) buffer.channelData(1)[i] = right;
    }
    return buffer;
}

TEST(AudioEngineTest, PushPullKeepsChannelsInTheirPlane) {
    auto output = std::make_unique<RecordingOutput>();
    auto* recorder = output.get();

    auto created = AudioEngine::create(std::move(output));
    ASSERT_TRUE(created.ok());
    auto engine = std::move(created.value());

    ASSERT_TRUE(engine.start(AudioMixConfig{48000, 2}).ok());
    EXPECT_EQ(engine.pushSamples(0, planarBuffer(100, 2, 0.5f, 0.25f)), 100u);

    AudioBuffer mixed;
    ASSERT_TRUE(engine.pullMix(mixed, 100).ok());

    // Centre pan applies the -3 dB pan law: cos(pi/4).
    constexpr double kCentre = 0.7071067811865476;
    ASSERT_EQ(mixed.channels, 2u);
    ASSERT_EQ(mixed.sampleCount, 100u);
    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_NEAR(mixed.channelData(0)[i], 0.5 * kCentre, 1e-5);
        EXPECT_NEAR(mixed.channelData(1)[i], 0.25 * kCentre, 1e-5);
    }

    EXPECT_EQ(recorder->writes(), 1);
    ASSERT_EQ(recorder->last().channels, 2u);
    EXPECT_NEAR(recorder->last().channelData(1)[3], 0.25 * kCentre, 1e-5);
}

TEST(AudioEngineTest, PushReturnsZeroWhenStopped) {
    auto output = std::make_unique<RecordingOutput>();
    auto created = AudioEngine::create(std::move(output));
    ASSERT_TRUE(created.ok());
    auto engine = std::move(created.value());

    EXPECT_EQ(engine.pushSamples(0, planarBuffer(16, 2, 1.0f, 1.0f)), 0u);
    AudioBuffer out;
    EXPECT_FALSE(engine.pullMix(out, 16).ok());
}

TEST(AudioEngineTest, PushClampsToRingCapacity) {
    auto output = std::make_unique<RecordingOutput>();
    auto created = AudioEngine::create(std::move(output));
    ASSERT_TRUE(created.ok());
    auto engine = std::move(created.value());
    ASSERT_TRUE(engine.start(AudioMixConfig{8000, 2}).ok());

    // Ring holds two seconds at 8 kHz.
    const uint32_t accepted = engine.pushSamples(0, planarBuffer(20000, 2, 0.1f, 0.1f));
    EXPECT_EQ(accepted, 16000u);
}

TEST(DeviceOutputTest, InterleaveToSwapsPlanarIntoFrames) {
    AudioBuffer buffer;
    buffer.resize(3, 2);
    buffer.channelData(0)[0] = 1.0f;
    buffer.channelData(0)[1] = 2.0f;
    buffer.channelData(0)[2] = 3.0f;
    buffer.channelData(1)[0] = 10.0f;
    buffer.channelData(1)[1] = 20.0f;
    buffer.channelData(1)[2] = 30.0f;

    std::vector<float> interleaved(6, 0.0f);
    interleaveTo(buffer, interleaved.data());

    const std::vector<float> expected{1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f};
    EXPECT_EQ(interleaved, expected);
}

TEST(DeviceOutputTest, FactoryReturnsANonNullSink) {
    EXPECT_NE(createDefaultAudioOutput(), nullptr);
}

#if defined(__linux__)
TEST(DeviceOutputTest, AlsaNullDeviceAcceptsFrames) {
    setenv("BL_AUDIO_DEVICE", "null", 1);
    auto device = createDefaultAudioOutput();
    auto opened = device->open(AudioMixConfig{48000, 2});
    if (!opened.ok()) {
        unsetenv("BL_AUDIO_DEVICE");
        GTEST_SKIP() << "ALSA null device unavailable: " << opened.message();
    }

    device->write(planarBuffer(480, 2, 0.1f, -0.1f));
    device->close();
    unsetenv("BL_AUDIO_DEVICE");
    SUCCEED();
}
#endif

} // namespace
} // namespace bl
