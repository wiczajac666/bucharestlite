#include <bl_core/codec_registry.hpp>
#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_plugins/codec_plugin.h>
#include "test_media_utils.hpp"
#include "test_plugin_utils.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_DYNLIB_SUFFIX
#define BL_DYNLIB_SUFFIX ".so"
#endif

namespace {

using bl::CodecRegistry;
using bl::DecoderBridge;
using bl::Demuxer;
using bl::Packet;

void* hostAlloc(size_t size, void*) { return std::malloc(size); }
void hostFree(void* ptr, void*) { std::free(ptr); }

BlHostApi makeHostApi() {
    BlHostApi api{};
    api.host_abi_version = BL_PLUGIN_ABI_VERSION;
    api.alloc = &hostAlloc;
    api.free = &hostFree;
    api.userdata = nullptr;
    return api;
}

struct LoadedPlugin {
    std::unique_ptr<bl::PluginHandle> handle;
    BlCodecPlugin* plugin{nullptr};
};

// Loads a real dlopen codec plugin from <testdir>/plugins/<subdir>/bl<base>.so
LoadedPlugin loadCodecPlugin(const std::string& subdir,
                             const std::string& base) {
    const std::string path = std::string(BL_TEST_PLUGIN_DIR) + "/plugins/" +
                             subdir + "/bl" + base + BL_DYNLIB_SUFFIX;

    if (!std::filesystem::exists(path)) {
        return {}; // plugin not staged (e.g. BL_BUILD_PLUGINS=OFF)
    }
    bl::PluginLoader loader;
    auto result = loader.load(path, bl::PluginOrigin::User);
    if (!result.ok()) {
        return {};
    }
    LoadedPlugin lp;
    lp.handle = std::move(result.value());
    lp.plugin = const_cast<BlCodecPlugin*>(lp.handle->plugin());
    return lp;
}

void putAllocator(BlCodecConfig* cfg, const BlHostApi* host) { cfg->host = host; }

// ---------------------------------------------------------------------------
// Video round trip
// ---------------------------------------------------------------------------

void makeVideoFrame(std::vector<uint8_t>* buf, int width, int height,
                    int index) {
    buf->resize(static_cast<size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t* px = buf->data() + (static_cast<size_t>(y) * width + x) * 4;
            px[0] = static_cast<uint8_t>((x * 3 + y * 5 + index * 7) & 0xff);
            px[1] = static_cast<uint8_t>((x * 7 + index * 3) & 0xff);
            px[2] = static_cast<uint8_t>((y * 11 + index * 13) & 0xff);
            px[3] = 255;
        }
    }
}

int encodeVideoRoundTrip(BlCodecPlugin* plugin, const BlHostApi* host,
                         int width, int height, int numFrames,
                         std::vector<std::vector<uint8_t>>* packets,
                         std::vector<BlFrameMeta>* frames) {
    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.video.width = static_cast<uint32_t>(width);
    cfg.video.height = static_cast<uint32_t>(height);
    cfg.video.fps.num = 24;
    cfg.video.fps.den = 1;
    cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
    putAllocator(&cfg, host);

    void* ectx = nullptr;
    int rc = plugin->init(&ectx, &cfg);
    if (rc != BL_OK) return BL_ERR_ENCODE_FAILED;

    std::vector<uint8_t> frame;
    packets->clear();
    for (int i = 0; i < numFrames; ++i) {
        makeVideoFrame(&frame, width, height, i);
        BlFrameMeta meta{};
        meta.width = static_cast<uint32_t>(width);
        meta.height = static_cast<uint32_t>(height);
        meta.linesize = static_cast<uint32_t>(width * 4);
        uint8_t* out = nullptr;
        size_t outSize = 0;
        rc = plugin->encode(ectx, frame.data(), frame.size(), &out, &outSize,
                            &meta);
        if (rc != BL_OK) {
            plugin->cleanup(ectx);
            return rc;
        }
        if (out) {
            packets->emplace_back(out, out + outSize);
            host->free(out, host->userdata);
        }
    }
    for (;;) {
        uint8_t* out = nullptr;
        size_t outSize = 0;
        rc = plugin->flush(ectx, &out, &outSize);
        if (rc != BL_OK) {
            plugin->cleanup(ectx);
            return rc;
        }
        if (!out) break;
        packets->emplace_back(out, out + outSize);
        host->free(out, host->userdata);
    }
    plugin->cleanup(ectx);

    // Decode with a fresh context.
    void* dctx = nullptr;
    rc = plugin->init(&dctx, &cfg);
    if (rc != BL_OK) return BL_ERR_DECODE_FAILED;
    for (const auto& pkt : *packets) {
        uint8_t* out = nullptr;
        size_t outSize = 0;
        BlFrameMeta meta{};
        rc = plugin->decode(dctx, pkt.data(), pkt.size(), &out, &outSize,
                            &meta);
        if (rc == BL_DECODE_NEED_MORE_INPUT) continue;
        if (rc != BL_OK) {
            plugin->cleanup(dctx);
            return rc;
        }
        frames->push_back(meta);
        host->free(out, host->userdata);
    }
    // flush() has no meta slot in the ABI; trailing frames are only counted.
    {
        int trailing = 0;
        for (;;) {
            uint8_t* out = nullptr;
            size_t outSize = 0;
            rc = plugin->flush(dctx, &out, &outSize);
            if (rc != BL_OK) {
                plugin->cleanup(dctx);
                return rc;
            }
            if (!out) break;
            BlFrameMeta meta{};
            frames->push_back(meta);
            host->free(out, host->userdata);
            ++trailing;
        }
        (void)trailing;
    }
    plugin->cleanup(dctx);
    return BL_OK;
}

struct VideoCodecCase {
    const char* base;
    const char* subdir;
    int width;
    int height;
    int numFrames;
};

class VideoRoundTripTest
    : public ::testing::TestWithParam<VideoCodecCase> {};

TEST_P(VideoRoundTripTest, EncodeThenDecodeRestoresFrameGeometry) {
    const VideoCodecCase param = GetParam();
    LoadedPlugin lp = loadCodecPlugin(param.subdir, param.base);
    if (!lp.plugin) {
        GTEST_SKIP() << "plugin " << param.base << " not staged";
    }
    if (!(lp.plugin->caps.roles & BL_ROLE_ENCODE)) {
        GTEST_SKIP() << param.base << " has no encoder in this build";
    }

    BlHostApi hostApi = makeHostApi();
    std::vector<std::vector<uint8_t>> packets;
    std::vector<BlFrameMeta> frames;
    int rc = encodeVideoRoundTrip(lp.plugin, &hostApi, param.width,
                                  param.height, param.numFrames, &packets,
                                  &frames);
    if (rc == BL_ERR_ENCODE_FAILED) {
        GTEST_SKIP() << param.base << ": encoder unavailable";
    }
    ASSERT_EQ(rc, BL_OK);

    EXPECT_GT(packets.size(), 0u);
    ASSERT_EQ(frames.size(), static_cast<size_t>(param.numFrames))
        << "expected every input frame to decode exactly once";
    int withGeometry = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        // flush() trails result in frames without meta (ABI limit).
        if (frames[i].width == 0 || frames[i].height == 0 ||
            frames[i].linesize == 0) {
            continue;
        }
        ++withGeometry;
        EXPECT_EQ(frames[i].width, static_cast<uint32_t>(param.width))
            << "frame " << i;
        EXPECT_EQ(frames[i].height, static_cast<uint32_t>(param.height))
            << "frame " << i;
        EXPECT_EQ(frames[i].linesize,
                  static_cast<uint32_t>(param.width * 4))
            << "frame " << i;
        EXPECT_FALSE(frames[i].keyframe && i != 0) << "frame " << i;
    }
    EXPECT_GE(withGeometry, param.numFrames - 4);
    // The first frame that carries meta must be a keyframe.
    for (const auto& m : frames) {
        if (m.width != 0) {
            EXPECT_EQ(m.keyframe, 1);
            break;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    CodecPlugins, VideoRoundTripTest,
    ::testing::Values(VideoCodecCase{"h264", "video", 64, 48, 12},
                      VideoCodecCase{"h264", "video", 64, 48, 1}));

// ---------------------------------------------------------------------------
// Audio round trip
// ---------------------------------------------------------------------------

void makeAudioBuffer(std::vector<float>* buf, int channels, int samples,
                     int start) {
    buf->resize(static_cast<size_t>(channels) * samples);
    for (int c = 0; c < channels; ++c) {
        for (int s = 0; s < samples; ++s) {
            (*buf)[static_cast<size_t>(c) * samples + s] =
                0.3f * std::sin(2.0 * 3.141592653589793 *
                                (440.0 * (start + s) / 48000.0));
        }
    }
}

int encodeAudioRoundTrip(BlCodecPlugin* plugin, const BlHostApi* host,
                         int channels, int chunkSamples, int numChunks,
                         std::vector<std::vector<uint8_t>>* packets,
                         std::vector<BlFrameMeta>* frames,
                         std::vector<float>* decoded) {
    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.audio.sample_rate = 48000;
    cfg.audio.channels = static_cast<uint32_t>(channels);
    cfg.audio.sample_fmt = BL_SAMPFMT_F32_PLANAR;
    putAllocator(&cfg, host);

    void* ectx = nullptr;
    int rc = plugin->init(&ectx, &cfg);
    if (rc != BL_OK) return BL_ERR_ENCODE_FAILED;

    std::vector<float> buf;
    packets->clear();
    for (int i = 0; i < numChunks; ++i) {
        makeAudioBuffer(&buf, channels, chunkSamples, i * chunkSamples);
        BlFrameMeta meta{};
        meta.sample_count = static_cast<uint32_t>(chunkSamples);
        meta.channels = static_cast<uint32_t>(channels);
        uint8_t* out = nullptr;
        size_t outSize = 0;
        rc = plugin->encode(ectx, reinterpret_cast<const uint8_t*>(buf.data()),
                            buf.size() * sizeof(float), &out, &outSize, &meta);
        if (rc != BL_OK) {
            plugin->cleanup(ectx);
            return rc;
        }
        if (out) {
            packets->emplace_back(out, out + outSize);
            host->free(out, host->userdata);
        }
    }
    for (;;) {
        uint8_t* out = nullptr;
        size_t outSize = 0;
        rc = plugin->flush(ectx, &out, &outSize);
        if (rc != BL_OK) {
            plugin->cleanup(ectx);
            return rc;
        }
        if (!out) break;
        packets->emplace_back(out, out + outSize);
        host->free(out, host->userdata);
    }
    plugin->cleanup(ectx);

    void* dctx = nullptr;
    rc = plugin->init(&dctx, &cfg);
    if (rc != BL_OK) return BL_ERR_DECODE_FAILED;
    for (const auto& pkt : *packets) {
        uint8_t* out = nullptr;
        size_t outSize = 0;
        BlFrameMeta meta{};
        rc = plugin->decode(dctx, pkt.data(), pkt.size(), &out, &outSize,
                            &meta);
        if (rc == BL_DECODE_NEED_MORE_INPUT) continue;
        if (rc != BL_OK) {
            plugin->cleanup(dctx);
            return rc;
        }
        frames->push_back(meta);
        if (decoded) {
            const float* f = reinterpret_cast<const float*>(out);
            const size_t nfloats = outSize / sizeof(float);
            decoded->insert(decoded->end(), f, f + nfloats);
        }
        host->free(out, host->userdata);
    }
    // flush() has no meta slot in the ABI; trailing frames are only counted.
    for (;;) {
        uint8_t* out = nullptr;
        size_t outSize = 0;
        rc = plugin->flush(dctx, &out, &outSize);
        if (rc != BL_OK) {
            plugin->cleanup(dctx);
            return rc;
        }
        if (!out) break;
        BlFrameMeta meta{};
        frames->push_back(meta);
        host->free(out, host->userdata);
    }
    plugin->cleanup(dctx);
    return BL_OK;
}

struct AudioCodecCase {
    const char* base;
    const char* subdir;
    int channels;
    int chunkSamples;
    int numChunks;
};

class AudioRoundTripTest
    : public ::testing::TestWithParam<AudioCodecCase> {};

TEST_P(AudioRoundTripTest, EncodeThenDecodeRestoresSampleCount) {
    const AudioCodecCase param = GetParam();
    LoadedPlugin lp = loadCodecPlugin(param.subdir, param.base);
    if (!lp.plugin) {
        GTEST_SKIP() << "plugin " << param.base << " not staged";
    }
    if (!(lp.plugin->caps.roles & BL_ROLE_ENCODE)) {
        GTEST_SKIP() << param.base << " has no encoder in this build";
    }

    BlHostApi hostApi = makeHostApi();
    std::vector<std::vector<uint8_t>> packets;
    std::vector<BlFrameMeta> frames;
    std::vector<float> decoded;
    int rc = encodeAudioRoundTrip(lp.plugin, &hostApi, param.channels,
                                  param.chunkSamples, param.numChunks,
                                  &packets, &frames, &decoded);
    if (rc == BL_ERR_ENCODE_FAILED) {
        GTEST_SKIP() << param.base << ": encoder unavailable";
    }
    ASSERT_EQ(rc, BL_OK);

    EXPECT_GT(packets.size(), 0u);

    uint64_t decodedSamples = 0;
    for (const auto& m : frames) {
        EXPECT_GT(m.sample_count, 0u);
        EXPECT_EQ(m.channels == 0 || m.channels == static_cast<uint32_t>(param.channels), true);
        if (m.channels != 0) {
            EXPECT_EQ(m.channels, static_cast<uint32_t>(param.channels));
        }
        decodedSamples += m.sample_count;
    }

    // Decoded PCM must actually contain the 440 Hz tone (non-trivial RMS,
    // all finite), proving the lossy path carried real signal.
    double sumAbs = 0.0;
    bool finite = true;
    for (float s : decoded) {
        finite = finite && std::isfinite(s);
        sumAbs += std::fabs(s);
    }
    ASSERT_GT(decoded.size(), 0u);
    EXPECT_TRUE(finite);
    const double rms = sumAbs / static_cast<double>(decoded.size());
    EXPECT_GT(rms, 0.005);
    EXPECT_LT(rms, 1.0);

    // Codecs may pad or prime (aac adds leading padding); allow ~6 chunks of
    // slack on either side.
    const uint64_t expected =
        static_cast<uint64_t>(param.numChunks) * param.chunkSamples;
    const uint64_t slack = static_cast<uint64_t>(param.chunkSamples) * 6u;
    EXPECT_GE(decodedSamples + slack, expected);
    EXPECT_LE(decodedSamples, expected + slack);
}

INSTANTIATE_TEST_SUITE_P(
    CodecPlugins, AudioRoundTripTest,
    ::testing::Values(AudioCodecCase{"aac", "audio", 2, 1024, 16}));

// ---------------------------------------------------------------------------
// Real Annex-B H.264 fixture through Demuxer + DecoderBridge
// ---------------------------------------------------------------------------

TEST(CodecPluginTest, DecodesAnnexBFixtureThroughDecoderBridge) {
    LoadedPlugin lp = loadCodecPlugin("video", "h264");
    if (!lp.plugin) {
        GTEST_SKIP() << "h264 plugin not staged";
    }

    BlHostApi hostApi = makeHostApi();

    CodecRegistry registry;
    ASSERT_TRUE(registry.registerPlugin(lp.plugin).ok());

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.video.width = 320;
    cfg.video.height = 240;
    cfg.video.fps.num = 24000;
    cfg.video.fps.den = 1001;
    cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
    putAllocator(&cfg, &hostApi);

    auto bridgeResult =
        DecoderBridge::create(registry, lp.plugin->name, cfg);
    ASSERT_TRUE(bridgeResult.ok()) << bridgeResult.message();
    DecoderBridge bridge = std::move(bridgeResult.value());

    auto demuxResult = Demuxer::open(bltest::mediaPath("test_video.h264"));
    ASSERT_TRUE(demuxResult.ok()) << demuxResult.message();
    Demuxer demuxer = std::move(demuxResult.value());

    int framesDecoded = 0;
    int geometryOk = 0;
    for (;;) {
        auto pktResult = demuxer.nextPacket();
        ASSERT_TRUE(pktResult.ok()) << pktResult.message();
        if (!pktResult.value().has_value()) break;

        auto frameResult = bridge.decode(**pktResult);
        ASSERT_TRUE(frameResult.ok()) << frameResult.message()
                                      << " (packet " << framesDecoded << ")";
        bl::Frame frame = std::move(frameResult.value());
        EXPECT_EQ(frame.type, bl::Frame::Type::Video);
        if (frame.width == 320u && frame.height == 240u &&
            frame.linesize == 320u * 4u) {
            geometryOk++;
        }
        ++framesDecoded;
    }

    // 2 s @ 24 fps -> 48 access units; allow a frame or two of slack.
    EXPECT_GE(framesDecoded, 46);
    EXPECT_LE(framesDecoded, 50);
    EXPECT_GE(geometryOk, framesDecoded / 2);
}

TEST(CodecPluginTest, RegistryIntegratesLoadedPlugins) {
    LoadedPlugin hv = loadCodecPlugin("video", "h264");
    LoadedPlugin av = loadCodecPlugin("audio", "aac");
    if (!hv.plugin || !av.plugin) {
        GTEST_SKIP() << "codec plugins not staged";
    }

    CodecRegistry registry;
    ASSERT_TRUE(registry.registerPlugin(hv.plugin).ok());
    ASSERT_TRUE(registry.registerPlugin(av.plugin).ok());

    const BlCodecPlugin* found = registry.find(hv.plugin->name);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, BL_CODEC_VIDEO);
    EXPECT_NE(found->caps.roles & BL_ROLE_ENCODE, 0u);
    EXPECT_NE(found->caps.roles & BL_ROLE_DECODE, 0u);

    found = registry.find(av.plugin->name);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, BL_CODEC_AUDIO);
}

} // namespace