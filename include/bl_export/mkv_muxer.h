#ifndef BL_EXPORT_MKV_MUXER_H
#define BL_EXPORT_MKV_MUXER_H

#include <bl_export/export_types.h>

#include <cstddef>
#include <cstdint>

namespace bl {
namespace export_ {

class MKVMuxer {
public:
    MKVMuxer();
    ~MKVMuxer();

    Result<void> open(const BlFormatPreset* preset, const char* output_path);
    void close();

    Result<void> writeVideoPacket(const uint8_t* data, size_t size,
                                                    uint64_t pts, bool keyframe);
Result<void> writeAudioPacket(const uint8_t* data, size_t size,
                                            uint32_t duration_samples);
    Result<void> writeSubtitlePacket(const uint8_t* data, size_t size,
                                                       uint64_t pts);

    bool isOpen() const;
    const char* outputPath() const;
    size_t bytesWritten() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace export_
}  // namespace bl

#endif /* BL_EXPORT_MKV_MUXER_H */