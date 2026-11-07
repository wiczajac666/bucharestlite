#include "render/gl_compositor_backend.hpp"

#include <bl_core/logger.hpp>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bl::ui {

namespace {

constexpr const char* kVertexShader = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// Reproduces alphaBlendCpu per pixel: dst = src * src.a * opacity +
// dst * (1 - src.a * opacity). Sampling the canvas as a texture (ping-pong
// between two framebuffers) keeps the arithmetic identical to the CPU path,
// including its treatment of the alpha channel, instead of relying on
// fixed-function blending corner cases.
constexpr const char* kFragmentShader = R"GLSL(#version 330 core
uniform sampler2D uCanvas;
uniform sampler2D uLayer;
uniform float uOpacity;
uniform vec2 uCanvasSize;
uniform vec2 uLayerSize;
in vec2 vUV;
out vec4 fragColor;
void main() {
    vec4 dst = texture(uCanvas, vUV);
    vec2 pixel = floor(vUV * uCanvasSize);
    vec4 src = vec4(0.0);
    if (pixel.x < uLayerSize.x && pixel.y < uLayerSize.y) {
        src = texture(uLayer, (pixel + 0.5) / uLayerSize);
    }
    float op = src.a * uOpacity;
    fragColor = vec4(src.rgb * op + dst.rgb * (1.0 - op),
                     src.a * op + dst.a * (1.0 - op));
}
)GLSL";

GLuint compileShader(QOpenGLFunctions_3_3_Core& gl, GLenum type,
                     const char* source) {
    const GLuint shader = gl.glCreateShader(type);
    if (shader == 0) return 0;
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);
    GLint compiled = GL_FALSE;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        gl.glGetShaderInfoLog(shader, length, nullptr, log.data());
        BL_LOG_WARN("render", "GL shader compile failed: " + log);
        gl.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void setNearestClamp(QOpenGLFunctions_3_3_Core& gl) {
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Offscreen GL 3.3 compositor. Layers are uploaded as BGRA textures, blended
// into ping-pong RGBA framebuffers, and read back as BGRA so the byte layout
// matches the CPU compositor's canvas exactly.
class GlCompositorBackend final : public ICompositorBackend {
public:
    ~GlCompositorBackend() override;

    static std::unique_ptr<ICompositorBackend> create();

    std::string_view name() const override { return "opengl"; }
    bool gpu() const override { return true; }

    void composite(std::vector<uint8_t>& out, uint32_t outWidth,
                   uint32_t outHeight, uint32_t outLinesize,
                   const std::vector<CompositeLayer>& layers) override;

private:
    GlCompositorBackend() = default;

    bool init();
    void destroyGl();

    bool ensureCanvas(uint32_t width, uint32_t height);
    bool uploadLayer(const CompositeLayer& layer);
    void renderPass(const CompositeLayer& layer, int src, int dst);
    void readBack(std::vector<uint8_t>& out, int src, uint32_t width,
                  uint32_t height, uint32_t linesize);

    std::unique_ptr<QOffscreenSurface> surface_;
    std::unique_ptr<QOpenGLContext> context_;
    QOpenGLFunctions_3_3_Core* gl_{nullptr};
    std::unique_ptr<ICompositorBackend> cpuFallback_;

    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};
    GLuint canvasTexture_[2]{0, 0};
    GLuint canvasFbo_[2]{0, 0};
    GLuint layerTexture_{0};
    GLint layerTextureWidth_{0};
    GLint layerTextureHeight_{0};
    uint32_t canvasWidth_{0};
    uint32_t canvasHeight_{0};

    GLint canvasLocation_{-1};
    GLint layerLocation_{-1};
    GLint opacityLocation_{-1};
    GLint canvasSizeLocation_{-1};
    GLint layerSizeLocation_{-1};
};

GlCompositorBackend::~GlCompositorBackend() {
    if (context_ && surface_ && context_->makeCurrent(surface_.get())) {
        destroyGl();
        context_->doneCurrent();
    }
}

void GlCompositorBackend::destroyGl() {
    if (!gl_) return;
    if (layerTexture_ != 0) {
        gl_->glDeleteTextures(1, &layerTexture_);
        layerTexture_ = 0;
    }
    if (canvasTexture_[0] != 0 || canvasTexture_[1] != 0) {
        gl_->glDeleteTextures(2, canvasTexture_);
        canvasTexture_[0] = canvasTexture_[1] = 0;
    }
    if (canvasFbo_[0] != 0 || canvasFbo_[1] != 0) {
        gl_->glDeleteFramebuffers(2, canvasFbo_);
        canvasFbo_[0] = canvasFbo_[1] = 0;
    }
    if (vbo_ != 0) {
        gl_->glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        gl_->glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_ != 0) {
        gl_->glDeleteProgram(program_);
        program_ = 0;
    }
}

bool GlCompositorBackend::init() {
    if (!QGuiApplication::instance()) return false;

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::SingleBuffer);

    surface_ = std::make_unique<QOffscreenSurface>();
    surface_->setFormat(format);
    surface_->create();
    if (!surface_->isValid()) {
        BL_LOG_WARN("render", "no offscreen surface for GPU compositor");
        return false;
    }

    context_ = std::make_unique<QOpenGLContext>();
    context_->setFormat(format);
    if (!context_->create()) {
        BL_LOG_WARN("render", "could not create OpenGL context");
        return false;
    }
    if (!context_->makeCurrent(surface_.get())) {
        BL_LOG_WARN("render", "could not make OpenGL context current");
        return false;
    }

    gl_ = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(
        context_.get());
    if (!gl_ || !gl_->initializeOpenGLFunctions()) {
        BL_LOG_WARN("render", "OpenGL 3.3 core functions unavailable");
        return false;
    }

    cpuFallback_ = makeCpuCompositorBackend();

    const GLuint vertex = compileShader(*gl_, GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment =
        compileShader(*gl_, GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) gl_->glDeleteShader(vertex);
        if (fragment != 0) gl_->glDeleteShader(fragment);
        return false;
    }
    program_ = gl_->glCreateProgram();
    gl_->glAttachShader(program_, vertex);
    gl_->glAttachShader(program_, fragment);
    gl_->glLinkProgram(program_);
    gl_->glDeleteShader(vertex);
    gl_->glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    gl_->glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        gl_->glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        gl_->glGetProgramInfoLog(program_, length, nullptr, log.data());
        BL_LOG_WARN("render", "GL program link failed: " + log);
        return false;
    }

    canvasLocation_ = gl_->glGetUniformLocation(program_, "uCanvas");
    layerLocation_ = gl_->glGetUniformLocation(program_, "uLayer");
    opacityLocation_ = gl_->glGetUniformLocation(program_, "uOpacity");
    canvasSizeLocation_ = gl_->glGetUniformLocation(program_, "uCanvasSize");
    layerSizeLocation_ = gl_->glGetUniformLocation(program_, "uLayerSize");

    const float vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                              -1.0f, 1.0f,  1.0f, 1.0f};
    gl_->glGenVertexArrays(1, &vao_);
    gl_->glBindVertexArray(vao_);
    gl_->glGenBuffers(1, &vbo_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                      GL_STATIC_DRAW);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                               2 * static_cast<GLsizei>(sizeof(float)),
                               nullptr);
    gl_->glBindVertexArray(0);

    gl_->glDisable(GL_BLEND);
    gl_->glDisable(GL_DEPTH_TEST);
    gl_->glDisable(GL_SCISSOR_TEST);
    gl_->glDisable(GL_CULL_FACE);
    context_->doneCurrent();
    return true;
}

bool GlCompositorBackend::ensureCanvas(uint32_t width, uint32_t height) {
    if (canvasWidth_ == width && canvasHeight_ == height &&
        canvasTexture_[0] != 0) {
        return true;
    }

    if (canvasTexture_[0] != 0 || canvasTexture_[1] != 0) {
        gl_->glDeleteTextures(2, canvasTexture_);
        canvasTexture_[0] = canvasTexture_[1] = 0;
    }
    if (canvasFbo_[0] != 0 || canvasFbo_[1] != 0) {
        gl_->glDeleteFramebuffers(2, canvasFbo_);
        canvasFbo_[0] = canvasFbo_[1] = 0;
    }

    gl_->glGenTextures(2, canvasTexture_);
    gl_->glGenFramebuffers(2, canvasFbo_);
    for (int i = 0; i < 2; ++i) {
        gl_->glBindTexture(GL_TEXTURE_2D, canvasTexture_[i]);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                          static_cast<GLsizei>(width),
                          static_cast<GLsizei>(height), 0, GL_BGRA,
                          GL_UNSIGNED_BYTE, nullptr);
        setNearestClamp(*gl_);

        gl_->glBindFramebuffer(GL_FRAMEBUFFER, canvasFbo_[i]);
        gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    GL_TEXTURE_2D, canvasTexture_[i], 0);
        if (gl_->glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            BL_LOG_WARN("render", "GL framebuffer incomplete");
            gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    canvasWidth_ = width;
    canvasHeight_ = height;
    return true;
}

bool GlCompositorBackend::uploadLayer(const CompositeLayer& layer) {
    if (layer.width == 0 || layer.height == 0 || layer.opacity <= 0.0) {
        return true;  // Nothing to draw; the canvas is left untouched.
    }
    const uint32_t stride = layer.linesize != 0 ? layer.linesize
                                                : layer.width * 4;
    if (layer.data.size() < static_cast<size_t>(stride) * layer.height) {
        return false;
    }
    if (stride % 4 != 0) return false;

    if (layerTexture_ == 0) {
        gl_->glGenTextures(1, &layerTexture_);
        layerTextureWidth_ = 0;
        layerTextureHeight_ = 0;
    }
    gl_->glBindTexture(GL_TEXTURE_2D, layerTexture_);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                       static_cast<GLint>(stride / 4));
    const GLsizei w = static_cast<GLsizei>(layer.width);
    const GLsizei h = static_cast<GLsizei>(layer.height);
    if (w != layerTextureWidth_ || h != layerTextureHeight_) {
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA,
                          GL_UNSIGNED_BYTE, layer.data.data());
        setNearestClamp(*gl_);
        layerTextureWidth_ = w;
        layerTextureHeight_ = h;
    } else {
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA,
                             GL_UNSIGNED_BYTE, layer.data.data());
    }
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return true;
}

void GlCompositorBackend::renderPass(const CompositeLayer& layer, int src,
                                     int dst) {
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, canvasFbo_[dst]);
    gl_->glViewport(0, 0, static_cast<GLsizei>(canvasWidth_),
                    static_cast<GLsizei>(canvasHeight_));
    gl_->glUseProgram(program_);
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, canvasTexture_[src]);
    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_2D, layerTexture_);
    gl_->glUniform1i(canvasLocation_, 0);
    gl_->glUniform1i(layerLocation_, 1);
    gl_->glUniform1f(opacityLocation_, static_cast<float>(layer.opacity));
    gl_->glUniform2f(canvasSizeLocation_, static_cast<float>(canvasWidth_),
                     static_cast<float>(canvasHeight_));
    gl_->glUniform2f(layerSizeLocation_, static_cast<float>(layer.width),
                     static_cast<float>(layer.height));
    gl_->glBindVertexArray(vao_);
    gl_->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_->glBindVertexArray(0);
    gl_->glActiveTexture(GL_TEXTURE0);
}

void GlCompositorBackend::readBack(std::vector<uint8_t>& out, int src,
                                   uint32_t width, uint32_t height,
                                   uint32_t linesize) {
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, canvasFbo_[src]);
    gl_->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_->glPixelStorei(GL_PACK_ROW_LENGTH,
                       static_cast<GLint>(linesize / 4));
    gl_->glReadPixels(0, 0, static_cast<GLsizei>(width),
                      static_cast<GLsizei>(height), GL_BGRA, GL_UNSIGNED_BYTE,
                      out.data());
    gl_->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlCompositorBackend::composite(
    std::vector<uint8_t>& out, uint32_t outWidth, uint32_t outHeight,
    uint32_t outLinesize, const std::vector<CompositeLayer>& layers) {
    const bool usable = context_ && surface_ && gl_ && cpuFallback_ &&
                        outLinesize % 4 == 0 &&
                        out.size() >= static_cast<size_t>(outLinesize) *
                                          outHeight;
    if (!usable || !context_->makeCurrent(surface_.get())) {
        if (cpuFallback_) {
            cpuFallback_->composite(out, outWidth, outHeight, outLinesize,
                                    layers);
        }
        return;
    }

    bool ok = ensureCanvas(outWidth, outHeight);
    if (ok) {
        // Start from a transparent canvas, matching the CPU backend.
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, canvasFbo_[0]);
        gl_->glViewport(0, 0, static_cast<GLsizei>(outWidth),
                        static_cast<GLsizei>(outHeight));
        gl_->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);

        int src = 0;
        int dst = 1;
        for (const CompositeLayer& layer : layers) {
            if (!uploadLayer(layer)) {
                ok = false;
                break;
            }
            if (layer.width == 0 || layer.height == 0 ||
                layer.opacity <= 0.0) {
                continue;  // uploadLayer skipped it; canvas already matches.
            }
            renderPass(layer, src, dst);
            std::swap(src, dst);
        }
        if (ok) {
            readBack(out, src, outWidth, outHeight, outLinesize);
        }
    }

    context_->doneCurrent();
    if (!ok) {
        // Mid-frame GPU failure: recompute the frame on the CPU so preview
        // never shows a partially composited image.
        cpuFallback_->composite(out, outWidth, outHeight, outLinesize, layers);
    }
}

std::unique_ptr<ICompositorBackend> GlCompositorBackend::create() {
    auto backend =
        std::unique_ptr<GlCompositorBackend>(new GlCompositorBackend());
    if (!backend->init()) {
        return nullptr;
    }
    return backend;
}

} // namespace

std::unique_ptr<bl::ICompositorBackend> makeGlCompositorBackend() {
    auto backend = GlCompositorBackend::create();
    if (!backend) {
        BL_LOG_INFO("render",
                    "GPU compositor unavailable; using the CPU backend");
    }
    return backend;
}

} // namespace bl::ui
