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
            auto mux_result = muxer_->writeAudioPacket(
                out_pkt, out_size, meta ? meta->pts : 0);
            if (!mux_result.ok()) {
                host_.free(out_pkt, host_.userdata);
                return Result<BlExportResult>::err(
                    Err::IoError, "failed to write audio packet to muxer");
            }
            audio_sample_count_++;
        }
        host_.free(out_pkt, host_.userdata);
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
                        out_pkt, out_size, audio_sample_count_);
                    if (!mux_result.ok()) {
                        host_.free(out_pkt, host_.userdata);
                        return Result<void>::err(
                            Err::IoError,
                            "failed to write flushed audio packet");
                    }
                    audio_sample_count_++;
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