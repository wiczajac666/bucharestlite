#include "bl_export/export_types.h"

#include "bl_export/muxer.h"
#include "bl_export/mkv_muxer.h"

#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>

#include <bl_plugins/codec_plugin.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <cstring>
#include <limits>

namespace bl {
namespace export_ {

namespace {

// Builds a terminated BlConfigEntry array for one encoder stream (the
// terminator entry has name == NULL, matching what find_param() in the
// plugins expects). Video uses "crf" (constant-quality) or "bitrate" (VBV);
// audio uses "bitrate".
BlConfigEntry makeEntry(const char* name, int64_t value) {
    BlConfigEntry e{};
    e.name = name;
    e.value.type = BL_VALUE_INT;
    e.value.i = value;
    return e;
}

std::vector<BlConfigEntry> buildVideoParams(const BlQualityPreset* quality) {
    std::vector<BlConfigEntry> params;
    if (quality) {
        if (quality->video_crf > 0) {
            params.push_back(makeEntry("crf", quality->video_crf));
        } else if (quality->video_bitrate > 0) {
            params.push_back(makeEntry("bitrate", quality->video_bitrate));
        }
    }
    params.push_back(BlConfigEntry{});  // terminator: name == nullptr
    return params;
}

std::vector<BlConfigEntry> buildAudioParams(const BlQualityPreset* quality) {
    std::vector<BlConfigEntry> params;
    if (quality && quality->audio_bitrate > 0) {
        params.push_back(makeEntry("bitrate", quality->audio_bitrate));
    }
    params.push_back(BlConfigEntry{});  // terminator: name == nullptr
    return params;
}

// The plugin slices audio into full frame_size granules (see
// push_pending_samples); each encoded packet therefore represents that many
// samples. Resolve the granule the encoder will actually use so the muxer can
// assign exact per-packet durations. Plugins that expose an explicit
// "frame_size" capability param (opus, flac, ...) advertise it ahead of the
// codec's built-in default.
uint32_t resolveAudioGranule(const BlCodecPlugin* plugin) {
    if (!plugin) return 0;
    if (plugin->caps.params) {
        for (const BlParamDesc* p = plugin->caps.params; p && p->name; ++p) {
            if (std::strcmp(p->name, "frame_size") == 0) {
                const int def = static_cast<int>(p->def);
                return def > 0 ? static_cast<uint32_t>(def) : 0;
            }
        }
    }
    if (!plugin->caps.ff_encoder) return 0;
    const AVCodec* codec = avcodec_find_encoder_by_name(plugin->caps.ff_encoder);
    if (!codec) return 0;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return 0;
    AVChannelLayout layout;
    av_channel_layout_default(&layout, 2);
    ctx->ch_layout = layout;
    ctx->sample_rate = 48000;
    const uint32_t granule =
        avcodec_open2(ctx, codec, nullptr) >= 0 && ctx->frame_size > 0
            ? static_cast<uint32_t>(ctx->frame_size)
            : 1024;
    avcodec_free_context(&ctx);
    return granule;
}

} // namespace

ExportEngine::ExportEngine(CodecRegistry& reg, BlHostApi& host)
    : registry_(reg), host_(host), video_encoder_(nullptr),
      audio_encoder_(nullptr), video_ctx_(nullptr), audio_ctx_(nullptr),
      video_frame_count_(0), audio_sample_count_(0), muxer_(nullptr),
      mkv_muxer_(nullptr) {}

ExportEngine::~ExportEngine() { cleanup(); }

Result<void> ExportEngine::initialize(const BlFormatPreset* preset) {
    if (!preset) {
        return Result<void>::err(Err::InvalidArgument, "null preset");
    }

    // Select the video encoder: named codec wins, otherwise registry default.
    video_encoder_ =
        preset->video_codec ? registry_.find(preset->video_codec) : nullptr;
    if (!video_encoder_) {
        video_encoder_ = registry_.defaultFor(BL_CODEC_VIDEO, CodecRole::Export);
    }
    if (!video_encoder_) {
        return Result<void>::err(Err::InvalidArgument,
                                 "no video encoder plugin available");
    }

    // Audio is optional: only initialized when both a codec was requested (or
    // a default exists) and the preset did not disable the audio track.
    audio_encoder_ = nullptr;
    if (preset->audio_enabled) {
        audio_encoder_ = preset->audio_codec
                             ? registry_.find(preset->audio_codec)
                             : nullptr;
        if (!audio_encoder_) {
            audio_encoder_ =
                registry_.defaultFor(BL_CODEC_AUDIO, CodecRole::Export);
        }
    }

    // Per-stream config entries; kept alive until both init() calls return.
    const std::vector<BlConfigEntry> videoParams =
        buildVideoParams(preset->quality);
    const std::vector<BlConfigEntry> audioParams =
        buildAudioParams(preset->quality);

    // Prepare video codec config
    BlCodecConfig video_cfg = {};
    video_cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    video_cfg.host = &host_;
    video_cfg.codec_name = video_encoder_->name;
    video_cfg.params = videoParams.data();
    video_cfg.video.width = preset->video.width;
    video_cfg.video.height = preset->video.height;
    video_cfg.video.fps = preset->video.fps;
    video_cfg.video.pixel_aspect = preset->video.pixel_aspect;
    video_cfg.video.pix_fmt = preset->video.pix_fmt;
    video_cfg.enc_flags = BL_ENCFLAG_GLOBAL_HEADER;

    // Prepare audio codec config
    if (audio_encoder_) {
        BlCodecConfig audio_cfg = {};
        audio_cfg.abi_version = BL_PLUGIN_ABI_VERSION;
        audio_cfg.host = &host_;
        audio_cfg.codec_name = audio_encoder_->name;
        audio_cfg.params = audioParams.data();
        audio_cfg.audio.sample_rate = preset->audio.sample_rate;
        audio_cfg.audio.channels = preset->audio.channels;
        audio_cfg.audio.bits_per_sample = preset->audio.bits_per_sample;
        audio_cfg.audio.sample_fmt = preset->audio.sample_fmt;
        audio_cfg.enc_flags = BL_ENCFLAG_GLOBAL_HEADER;

        int ret = audio_encoder_->init(&audio_ctx_, &audio_cfg);
        if (ret != BL_OK) {
            return Result<void>::err(Err::Internal,
                                     "failed to init audio encoder: " +
                                         std::to_string(ret));
        }
    }

    // Initialize video encoder context
    int ret = video_encoder_->init(&video_ctx_, &video_cfg);
    if (ret != BL_OK) {
        cleanup();
        return Result<void>::err(Err::Internal,
                                 "failed to init video encoder: " +
                                     std::to_string(ret));
    }

    // Resolve the concrete FFmpeg codec ids so the muxer can advertise the
    // encoder actually in use instead of defaulting to H.264/AAC.
    if (const AVCodec* codec =
            video_encoder_->caps.ff_encoder
                ? avcodec_find_encoder_by_name(video_encoder_->caps.ff_encoder)
                : nullptr) {
        video_codec_id_ = codec->id;
    }
    if (audio_encoder_ && audio_encoder_->caps.ff_encoder) {
        if (const AVCodec* codec =
                avcodec_find_encoder_by_name(audio_encoder_->caps.ff_encoder)) {
            audio_codec_id_ = codec->id;
        }
    }
    audio_granule_ = resolveAudioGranule(audio_encoder_);

    // Every encoder this project ships publishes its parameter sets during
    // init() once BL_ENCFLAG_GLOBAL_HEADER is set, so the muxer gets them via
    // configureMuxer(). The poll after the first packet exists for encoders
    // that publish later; see the header note on why that is too late for a
    // muxer header but still worth caching.
    refreshExtradata();

    preset_ = preset;
    quality_ = preset ? preset->quality : nullptr;

    return Result<void>();
}

void ExportEngine::cleanup() {
    if (video_ctx_) {
        video_encoder_->cleanup(video_ctx_);
        video_ctx_ = nullptr;
    }
    if (audio_ctx_) {
        audio_encoder_->cleanup(audio_ctx_);
        audio_ctx_ = nullptr;
    }
    video_encoder_ = nullptr;
    audio_encoder_ = nullptr;
    preset_ = nullptr;
    quality_ = nullptr;
    job_ = nullptr;
    muxer_ = nullptr;
    mkv_muxer_ = nullptr;
    video_frame_count_ = 0;
    audio_sample_count_ = 0;
    video_codec_id_ = 0;
    audio_codec_id_ = 0;
    audio_granule_ = 0;
    video_extradata_.clear();
    audio_extradata_.clear();
    params_.clear();
}

void ExportEngine::setMuxer(Muxer* muxer) { muxer_ = muxer; }

void ExportEngine::setMKVMuxer(MKVMuxer* muxer) { mkv_muxer_ = muxer; }

void ExportEngine::configureMuxer(Muxer* muxer) const {
    if (!muxer) {
        return;
    }
    muxer->setVideoCodecId(video_codec_id_);
    muxer->setAudioCodecId(audio_codec_id_);
    muxer->setAudioEnabled(audio_encoder_ != nullptr);
    if (!video_extradata_.empty()) {
        muxer->setVideoExtradata(video_extradata_.data(),
                                 video_extradata_.size());
    }
    if (!audio_extradata_.empty()) {
        muxer->setAudioExtradata(audio_extradata_.data(),
                                 audio_extradata_.size());
    }
}

void ExportEngine::refreshExtradata() {
    // Copies whatever the encoder has published so far. The buffer belongs to
    // the plugin (valid until its cleanup()), so it has to be copied out.
    // Optional in ABI v3: older plugins leave the slot null.
    if (video_encoder_ && video_encoder_->get_extradata && video_ctx_ &&
        video_extradata_.empty()) {
        size_t size = 0;
        if (const uint8_t* data =
                video_encoder_->get_extradata(video_ctx_, &size)) {
            if (size > 0) {
                video_extradata_.assign(data, data + size);
            }
        }
    }
    if (audio_encoder_ && audio_encoder_->get_extradata && audio_ctx_ &&
        audio_extradata_.empty()) {
        size_t size = 0;
        if (const uint8_t* data =
                audio_encoder_->get_extradata(audio_ctx_, &size)) {
            if (size > 0) {
                audio_extradata_.assign(data, data + size);
            }
        }
    }
}

const std::vector<uint8_t>& ExportEngine::videoExtradata() const noexcept {
    return video_extradata_;
}

const std::vector<uint8_t>& ExportEngine::audioExtradata() const noexcept {
    return audio_extradata_;
}

Result<BlExportResult> ExportEngine::encodeFrameVideo(const uint8_t* frame_data,
                                                   BlFrameMeta* meta) {
    if (!video_ctx_ || !video_encoder_) {
        return Result<BlExportResult>::err(
            Err::InvalidArgument, "video encoder not initialized");
    }

    // Feed one frame; the codec may buffer it and return the packets for
    // earlier frames on this or a later call (drain-before-feed).
    uint8_t* out_pkt = nullptr;
    size_t out_size = 0;
    int ret = video_encoder_->encode(video_ctx_, frame_data, 0, &out_pkt,
                                     &out_size, meta);
    if (ret < 0) {
        return Result<BlExportResult>::err(
            Err::Internal, "video encode failed: " + std::to_string(ret));
    }
    if (ret == BL_OK && out_pkt) {
        if (!meta) {
            host_.free(out_pkt, host_.userdata);
            return Result<BlExportResult>::err(
                Err::InvalidArgument, "video encode packet without frame meta");
        }
        if (muxer_) {
            bool keyframe = meta->keyframe != 0;
            auto mux_result = muxer_->writeVideoPacket(
                out_pkt, out_size, meta->pts, keyframe);
            if (!mux_result.ok()) {
                host_.free(out_pkt, host_.userdata);
                return Result<BlExportResult>::err(
                    Err::IoError, "failed to write video packet to muxer");
            }
            video_frame_count_++;
        }
        host_.free(out_pkt, host_.userdata);
        refreshExtradata();
    }

    BlExportResult result = {};
    result.status = BL_OK;
    result.message = nullptr;
    if (job_) result.job = *job_;
    return Result<BlExportResult>::ok(result);
}

Result<BlExportResult> ExportEngine::encodeFrameAudio(const uint8_t* audio_data,
                                                    BlFrameMeta* meta) {
    if (!audio_ctx_ || !audio_encoder_) {
        // Audio track disabled: accept and ignore so callers can feed an
        // optional silent stream without special-casing.
        BlExportResult result = {};
        result.status = BL_OK;
        if (job_) result.job = *job_;
        return Result<BlExportResult>::ok(result);
    }

    uint8_t* out_pkt = nullptr;
    size_t out_size = 0;
    int ret = audio_encoder_->encode(audio_ctx_, audio_data, 0, &out_pkt,
                                     &out_size, meta);
    if (ret < 0) {
        return Result<BlExportResult>::err(
            Err::Internal, "audio encode failed: " + std::to_string(ret));
    }
    if (ret == BL_OK && out_pkt) {
        if (muxer_) {
            // One encoded packet covers one encoder granule (frame_size) of
            // input samples; the muxer assigns monotonic timestamps on that
            // basis. The final flush packet may carry a shorter sub-frame
            // remainder, which under-counts the last granule slightly (a
            // container-only imprecision at the very tail).
            auto mux_result = muxer_->writeAudioPacket(
                out_pkt, out_size, audio_granule_);
            if (!mux_result.ok()) {
                host_.free(out_pkt, host_.userdata);
                return Result<BlExportResult>::err(
                    Err::IoError, "failed to write audio packet to muxer");
            }
            audio_sample_count_ += audio_granule_;
        }
        host_.free(out_pkt, host_.userdata);
        refreshExtradata();
    }

    BlExportResult result = {};
    result.status = BL_OK;
    result.message = nullptr;
    if (job_) result.job = *job_;
    return Result<BlExportResult>::ok(result);
}

Result<void> ExportEngine::finalize() {
    // Flush encoders (sends EOF; peel every remaining packet). Delayed
    // packets surface in chronological order, so a running counter is a valid
    // monotonic pts for the muxer.
    if (video_ctx_ && video_encoder_) {
        while (true) {
            uint8_t* out_pkt = nullptr;
            size_t out_size = 0;
            int ret = video_encoder_->flush(video_ctx_, &out_pkt, &out_size);
            if (ret == BL_OK && out_pkt) {
                if (muxer_) {
                    auto mux_result = muxer_->writeVideoPacket(
                        out_pkt, out_size, video_frame_count_, true);
                    if (!mux_result.ok()) {
                        host_.free(out_pkt, host_.userdata);
                        return Result<void>::err(
                            Err::IoError,
                            "failed to write flushed video packet");
                    }
                    video_frame_count_++;
                }
                host_.free(out_pkt, host_.userdata);
            } else {
                if (out_pkt) host_.free(out_pkt, host_.userdata);
                break;
            }
        }
    }

    if (audio_ctx_ && audio_encoder_) {
        while (true) {
            uint8_t* out_pkt = nullptr;
            size_t out_size = 0;
            int ret = audio_encoder_->flush(audio_ctx_, &out_pkt, &out_size);
            if (ret == BL_OK && out_pkt) {
                if (muxer_) {
                    auto mux_result = muxer_->writeAudioPacket(
                        out_pkt, out_size, audio_granule_);
                    if (!mux_result.ok()) {
                        host_.free(out_pkt, host_.userdata);
                        return Result<void>::err(
                            Err::IoError,
                            "failed to write flushed audio packet");
                    }
                    audio_sample_count_ += audio_granule_;
                }
                host_.free(out_pkt, host_.userdata);
            } else {
                if (out_pkt) host_.free(out_pkt, host_.userdata);
                break;
            }
        }
    }

    // Close muxer
    if (muxer_ && muxer_->isOpen()) {
        muxer_->close();
    }

    return Result<void>();
}

void ExportEngine::setJob(BlExportJob* job) { job_ = job; }

bool ExportEngine::isRunning() const {
    return job_ != nullptr && job_->status == (int)BlExportJobStatus::Running;
}

}  // namespace export_
}  // namespace bl