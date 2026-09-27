#include "export/export_plan.hpp"

#include <algorithm>

namespace bl::ui {

bl::Duration sequenceDuration(const bl::Sequence& sequence) {
    bl::Time end;
    const auto scan = [&end](const std::vector<bl::Clip>& clips) {
        for (const auto& clip : clips) {
            const bl::Time clipEnd = clip.timelineStart + clip.effectiveDuration();
            if (clipEnd > end) end = clipEnd;
        }
    };
    for (const auto& track : sequence.videoTracks) {
        scan(track.clips());
    }
    for (const auto& track : sequence.audioTracks) {
        scan(track.clips());
    }
    return end.asDuration();
}

Result<void> ExportPlan::build(const bl::Sequence& sequence,
                               bl::Duration totalDuration) {
    const auto& seq = sequence.settings;

    width = outputWidth(settings, seq);
    height = outputHeight(settings, seq);
    if (width == 0 || height == 0) {
        return Result<void>::err(Err::InvalidArgument,
                                 "output resolution must be non-zero");
    }
    if (settings.outputPath.empty()) {
        return Result<void>::err(Err::InvalidArgument, "output path is empty");
    }
    if (!isSupportedContainer(settings.container.empty() ? "mp4"
                                                          : settings.container)) {
        return Result<void>::err(
            Err::InvalidArgument,
            "unsupported container for this version: only mp4 and webm");
    }

    range = frameRange(settings, seq, totalDuration);
    if (range.frameCount <= 0) {
        return Result<void>::err(
            Err::InvalidArgument, "selected range contains no frames to export");
    }

    fps = {static_cast<int32_t>(seq.fps.num),
           static_cast<uint32_t>(seq.fps.den)};
    sampleRate = static_cast<uint32_t>(seq.sampleRate);
    channels = static_cast<uint32_t>(seq.channelLayout > 0 ? seq.channelLayout
                                                           : 2);

    outputPath_ = settings.outputPath;
    container_ = settings.container.empty() ? "mp4" : settings.container;
    videoCodec_ = settings.videoCodec;
    audioCodec_ = settings.audioCodec;
    if (settings.includeAudio && audioCodec_.empty()) {
        // Pick a codec the container actually accepts when the caller asked
        // for audio but did not name one.
        audioCodec_ = container_ == "mp4" ? "aac" : "opus";
    }
    if (!settings.includeAudio) {
        audioCodec_.clear();
    }
    if (settings.includeAudio && !audioCodec_.empty()) {
        const bool containerOk =
            container_ == "mp4"
                ? (audioCodec_ == "aac" || audioCodec_ == "flac" ||
                   audioCodec_ == "opus")
                : (audioCodec_ == "opus" || audioCodec_ == "vorbis");
        if (!containerOk) {
            return Result<void>::err(
                Err::InvalidArgument,
                "'" + settings.audioCodec + "' is not a valid " + container_ +
                    " audio codec");
        }
    }

    preset_.name = "custom";
    preset_.container = container_.c_str();
    preset_.video_codec = videoCodec_.empty() ? nullptr : videoCodec_.c_str();
    preset_.audio_codec = audioCodec_.empty() ? nullptr : audioCodec_.c_str();
    preset_.video.width = width;
    preset_.video.height = height;
    preset_.video.fps = fps;
    preset_.video.pixel_aspect = {1, 1};
    preset_.video.pix_fmt = BL_PIXFMT_BGRA32;
    preset_.audio.sample_rate = sampleRate;
    preset_.audio.channels = channels;
    preset_.audio.bits_per_sample = 16;
    preset_.audio.sample_fmt = BL_SAMPFMT_F32_PLANAR;
    preset_.audio_enabled = settings.includeAudio;

    quality_.name = "custom";
    quality_.video_crf = settings.videoCq;
    quality_.video_bitrate =
        settings.videoCq > 0 ? 0 : static_cast<int>(1024u * 1024u * 8u);
    quality_.audio_bitrate = settings.audioBitrate;
    preset_.quality = &quality_;

    return Result<void>();
}

} // namespace bl::ui