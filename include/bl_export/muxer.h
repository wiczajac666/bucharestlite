#ifndef BL_EXPORT_MUXER_H
#define BL_EXPORT_MUXER_H

#include <bl_export/export_types.h>

#include <cstddef>
#include <cstdint>

namespace bl {
namespace export_ {

class Muxer {
public:
    Muxer();
    ~Muxer();

    Result<void> open(const BlFormatPreset* preset, const char* output_path);
    void close();

    // Override the codec ids advertised in the stream headers (defaults:
    // H.264 video / AAC audio). Set before open().
    void setVideoCodecId(int codec_id);
    void setAudioCodecId(int codec_id);
    // Encoder codec private data (H.264 SPS/PPS, AAC AudioSpecificConfig, ...),
    // copied and attached to the output track headers. Required by Matroska;
    // set before open().
    void setVideoExtradata(const uint8_t* data, size_t size);
    void setAudioExtradata(const uint8_t* data, size_t size);
    // When false, no audio stream is created (default true).
    void setAudioEnabled(bool enabled);
    // When true, a soft-text subtitle stream is created for containers that
    // have one: MOV_TEXT for MP4/MOV, SubRip for Matroska, WebVTT for WebM
    // (default false). Independent of the audio stream.
    void setSubtitlesEnabled(bool enabled);

    Result<void> writeVideoPacket(const uint8_t* data, size_t size,
                                        uint64_t pts, bool keyframe);
    Result<void> writeAudioPacket(const uint8_t* data, size_t size,
                                        uint32_t duration_samples);
    // Writes a subtitle sample occupying [pts_seconds, pts_seconds +
    // duration_seconds) in timeline time.
    Result<void> writeSubtitlePacket(const uint8_t* data, size_t size,
                                           double pts_seconds,
                                           double duration_seconds);

    bool isOpen() const;
    const char* outputPath() const;
    size_t bytesWritten() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace export_
}  // namespace bl

#endif /* BL_EXPORT_MUXER_H */