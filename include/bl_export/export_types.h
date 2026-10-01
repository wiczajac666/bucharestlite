#ifndef BL_EXPORT_EXPORT_TYPES_H
#define BL_EXPORT_EXPORT_TYPES_H

#include <bl_core/result.hpp>

#include <bl_core/time.hpp>

#include <bl_plugins/codec_plugin.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bl {

// Forward declarations
class CodecRegistry;

namespace export_ {

struct BlVideoParams {
    uint32_t width = 1920;
    uint32_t height = 1080;
    BlRational fps = {30, 1};
    BlRational pixel_aspect = {1, 1};
    uint32_t pix_fmt = BL_PIXFMT_BGRA32;
};

struct BlAudioParams {
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    uint32_t bits_per_sample = 16;
    uint32_t sample_fmt = BL_SAMPFMT_F32_PLANAR;
};

struct BlQualityPreset {
    const char* name = nullptr;
    const char* description = nullptr;
    int video_bitrate = 0;
    int video_crf = 0;
    int audio_bitrate = 0;
    const char* profile = nullptr;
};

struct BlFormatPreset {
    const char* name = nullptr;
    const char* description = nullptr;
    const char* container = nullptr;
    const char* video_codec = nullptr;
    const char* audio_codec = nullptr;
    BlVideoParams video;
    BlAudioParams audio;
    const BlParamDesc* video_params = nullptr;
    const BlParamDesc* audio_params = nullptr;
    const BlQualityPreset* quality = nullptr;
    bool audio_enabled = true;
    int video_codec_id = 0;
    int audio_codec_id = 0;
};

struct BlExportJobStatus {
    enum Value {
        Pending = 0,
        Running = 1,
        Paused = 2,
        Completed = 3,
        Failed = 4,
        Cancelled = 5
    };
};

struct BlExportJob {
    const char* name = nullptr;
    const char* source_path = nullptr;
    const char* output_path = nullptr;
    const BlFormatPreset* preset = nullptr;
    const BlQualityPreset* quality = nullptr;

    int status = (int)BlExportJobStatus::Pending;
    size_t frames_encoded = 0;
    size_t frames_total = 0;
    size_t bytes_written = 0;
    double elapsed_seconds = 0.0;
    double eta_seconds = 0.0;

    using ProgressCallback = void (*)(const BlExportJob* job, int percent,
                                              size_t current_frame,
                                              size_t total_frames,
                                              void* user_data);
    ProgressCallback progress_callback = nullptr;
    void* user_data = nullptr;
};

struct BlExportResult {
    int status = 0;
    const char* message = nullptr;
    BlExportJob job;
};

class Muxer;
class MKVMuxer;

class ExportEngine {
public:
    ExportEngine(CodecRegistry& reg, BlHostApi& host);
    ~ExportEngine();

    Result<void> initialize(const BlFormatPreset* preset);
    void cleanup();

void setMuxer(Muxer* muxer);
    void setMKVMuxer(MKVMuxer* muxer);

    // Applies the codec ids resolved during initialize() to a freshly created
    // muxer so its streams advertise the encoder actually in use.
    void configureMuxer(Muxer* muxer) const;

    // Encoder codec private data (H.264 SPS/PPS, AAC AudioSpecificConfig, ...)
    // captured from the plugins. Muxers need it to write complete track
    // headers; matroskaenc in particular has no in-band fallback and fails its
    // header write without it. Empty when the codec produces none (e.g.
    // passthrough). Valid until cleanup().
    //
    // The cache is re-polled after every successful packet because a few
    // encoders only publish parameter sets once one exists. That is too late
    // for a muxer, which writes its header in open() -- so read these before
    // open(), as configureMuxer() does. Every encoder this project ships
    // publishes at init() with BL_ENCFLAG_GLOBAL_HEADER set, so the two are
    // equivalent today; the poll is insurance for a future encoder that does
    // not, and would then require deferring the header write.
    const std::vector<uint8_t>& videoExtradata() const noexcept;
    const std::vector<uint8_t>& audioExtradata() const noexcept;

    Result<BlExportResult> encodeFrameVideo(const uint8_t* frame_data,
                                                        BlFrameMeta* meta);
    Result<BlExportResult> encodeFrameAudio(const uint8_t* audio_data,
                                                       BlFrameMeta* meta);

    Result<void> finalize();

    void setJob(BlExportJob* job);
    bool isRunning() const;

private:
    CodecRegistry& registry_;
    BlHostApi& host_;

    BlCodecPlugin* video_encoder_ = nullptr;
    BlCodecPlugin* audio_encoder_ = nullptr;
    void* video_ctx_ = nullptr;
    void* audio_ctx_ = nullptr;
    size_t video_frame_count_ = 0;
    size_t audio_sample_count_ = 0;

    const BlFormatPreset* preset_ = nullptr;
    const BlQualityPreset* quality_ = nullptr;
    BlExportJob* job_ = nullptr;

    Muxer* muxer_ = nullptr;
    MKVMuxer* mkv_muxer_ = nullptr;
    int video_codec_id_ = 0;
    int audio_codec_id_ = 0;
    uint32_t audio_granule_ = 0;
    std::vector<BlConfigEntry> params_;

    // Pulls whatever extradata the encoders have published so far. Safe to
    // call repeatedly; the plugin only reports a buffer once.
    void refreshExtradata();

    std::vector<uint8_t> video_extradata_;
    std::vector<uint8_t> audio_extradata_;
};

}  // namespace export_
}  // namespace bl

#endif /* BL_EXPORT_EXPORT_TYPES_H */