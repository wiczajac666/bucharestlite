#pragma once

#include <bl_core/decoder_bridge.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_core/project_data.hpp>
#include <bl_render/compositor.hpp>
#include <bl_render/frame_cache.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bl {

// Decodes source media frames on demand for the compositor.
//
// Maps timeline clip references (mediaItemId) to files on disk, opens a
// Demuxer + DecoderBridge per source, seeks to the requested source time and
// feeds video packets until a frame covering the request is produced. Every
// successfully decoded frame is parked in a FrameCache (keyed by source
// time + size) so sequential preview playback warms up on later calls.
//
// Thread-affinity: instances are single-threaded and must be driven from one
// thread (the UI thread in the preview path).
class MediaDecodeSource : public IDecodeProvider {
public:
    struct Spec {
        // Extra plugin roots scanned on top of the platform defaults.
        // (path, origin) pairs; subdirectories {video,audio} are scanned
        // automatically. Empty by default: only built-in plugins available.
        std::vector<std::pair<std::string, PluginOrigin>> pluginDirs;
    };

    MediaDecodeSource(std::vector<MediaBinItem> mediaBin, FrameCache& cache,
                      Spec spec = {});
    ~MediaDecodeSource();

    MediaDecodeSource(MediaDecodeSource&&) noexcept;
    MediaDecodeSource& operator=(MediaDecodeSource&&) noexcept;

    MediaDecodeSource(const MediaDecodeSource&) = delete;
    MediaDecodeSource& operator=(const MediaDecodeSource&) = delete;

    Result<Frame> getFrame(const std::string& mediaItemId, Time sourceTime,
                           uint32_t width, uint32_t height) override;

    // Closes every open source so a changed project/media bin releases its
    // file handles and decoder contexts.
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl