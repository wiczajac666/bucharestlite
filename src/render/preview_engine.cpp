#include <bl_render/preview_engine.hpp>

#include <bl_core/logger.hpp>

#include <utility>

namespace bl {

struct PreviewEngine::Impl {
    Compositor compositor;
    FrameCache cache;
    MediaDecodeSource decoder;

    Impl(Compositor comp, Config config, std::vector<MediaBinItem> mediaBin)
        : compositor(std::move(comp)),
          cache(config.cacheBytes),
          decoder(std::move(mediaBin), cache, config.plugins) {}
};

PreviewEngine::PreviewEngine(PreviewEngine&&) noexcept = default;
PreviewEngine& PreviewEngine::operator=(PreviewEngine&&) noexcept = default;
PreviewEngine::~PreviewEngine() = default;

Result<PreviewEngine> PreviewEngine::create(Config config,
                                            std::vector<MediaBinItem> mediaBin) {
    if (config.outputWidth == 0 || config.outputHeight == 0) {
        return Result<PreviewEngine>::err(Err::InvalidArgument,
                                          "preview size must be non-zero");
    }
    if (config.outputWidth > 4096 || config.outputHeight > 4096) {
        return Result<PreviewEngine>::err(Err::InvalidArgument,
                                          "preview exceeds 4096x4096");
    }

    CompositorConfig compConfig{config.outputWidth, config.outputHeight};
    auto compositor = Compositor::create(compConfig);
    if (!compositor.ok()) {
        return Result<PreviewEngine>::err(compositor.code(), compositor.message());
    }

    PreviewEngine engine;
    engine.impl_ = std::make_unique<Impl>(std::move(compositor.value()), config,
                                          std::move(mediaBin));
    return Result<PreviewEngine>::ok(std::move(engine));
}

Result<CompositorResult> PreviewEngine::runAt(const TimelineSnapshot& snapshot, Time t) {
    if (!impl_) {
        return Result<CompositorResult>::err(Err::InvalidArgument,
                                             "preview engine not initialized");
    }
    return impl_->compositor.renderFrame(snapshot, t, impl_->decoder);
}

void PreviewEngine::reset() {
    if (impl_) {
        impl_->decoder.reset();
        impl_->cache.clear();
    }
}

const CompositorConfig& PreviewEngine::config() const noexcept {
    static const CompositorConfig kEmpty;
    return impl_ ? impl_->compositor.config() : kEmpty;
}

} // namespace bl