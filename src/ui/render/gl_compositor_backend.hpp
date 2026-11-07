#pragma once

#include <bl_render/compositor_backend.hpp>

#include <memory>

namespace bl::ui {

// Builds an OpenGL 3.3 offscreen compositing backend. Returns nullptr when no
// GPU context can be created (headless hosts, drivers without framebuffer
// support); callers then keep the CPU backend, so the renderer never fails
// because GPU compositing was requested.
//
// The context is created on the calling thread and must be used only there.
// PreviewEngine is single-threaded, so the preview panel creates this on the
// UI thread and the export pipeline never injects it.
std::unique_ptr<bl::ICompositorBackend> makeGlCompositorBackend();

} // namespace bl::ui
