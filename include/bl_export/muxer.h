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
    // When false, no audio stream is created (default true).
    void setAudioEnabled(bool enabled);
    // When true and the output container is MP4, a MOV_TEXT subtitle stream is
    // created (default false). Independent of the audio stream.
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