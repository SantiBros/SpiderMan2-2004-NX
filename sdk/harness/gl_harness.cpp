// GL=1: the test build draws with the GL renderer (gl/) on a headless EGL context (Mesa, llvmpipe without a GPU),
// every frame, into a 1280x720 screen like the Switch's. Paced to 60 frames a second when it keeps up.
// GLCAP=N: every Nth frame's screen saved as cap/gl_<frame>.tga (the 4:3 picture, 960x720); GLCAP_AT="f f ...": those
// frames too. Built with -DXBE_GL_HARNESS and gl/*.cpp, linked with -lEGL.
#ifdef XBE_GL_HARNESS
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gl/gl_api.h"
#include "gl/gl_backend.h"
#include "gpu/backend.h"
#include "harness.h"
#include "reference_renderer.h"

namespace harness {

namespace {

constexpr uint32_t kScreenW = 1280, kScreenH = 720;

class EglHost final : public glr::Host {
public:
    bool MakeCurrent() override {
        if (ctx_ != EGL_NO_CONTEXT) return eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_);
        auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (get_display) dpy_ = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (!dpy_) dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!dpy_ || !eglInitialize(dpy_, nullptr, nullptr)) {
            LOGE("gl harness: no EGL display");
            return false;
        }
        eglBindAPI(EGL_OPENGL_API);
        const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
        EGLConfig cfg = nullptr;
        EGLint n = 0;
        eglChooseConfig(dpy_, cfg_attr, &cfg, 1, &n);
        const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                   EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
        ctx_ = eglCreateContext(dpy_, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
        if (!ctx_ || !eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_)) {
            LOGE("gl harness: no OpenGL 4.3 core context (EGL error %X)", eglGetError());
            return false;
        }
        const char* missing = nullptr;
        if (!glapi::Load([](const char* name) { return reinterpret_cast<void*>(eglGetProcAddress(name)); }, &missing)) {
            LOGE("gl harness: %s missing", missing);
            return false;
        }
        // the screen: a framebuffer object the size of the Switch's
        glGenTextures(1, &screen_tex_);
        glBindTexture(GL_TEXTURE_2D, screen_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kScreenW, kScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &screen_);
        glBindFramebuffer(GL_FRAMEBUFFER, screen_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screen_tex_, 0);
        static const long every = Env("GLCAP");
        every_ = every;
        if (const char* at = std::getenv("GLCAP_AT"))
            for (const char* p = at; *p;) {
                char* q;
                long f = std::strtol(p, &q, 10);
                if (q == p) break;
                at_.insert(f);
                p = q;
            }
        return true;
    }
    void Release() override { eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); }
    uint32_t ScreenFramebuffer() override { return screen_; }
    void ScreenSize(uint32_t& w, uint32_t& h) override {
        w = kScreenW;
        h = kScreenH;
    }
    void Presented(uint32_t drawn) override {
        frames_++;
        g_frames = frames_;
        if (drawn && !shown_) {
            shown_ = true;
            LOGI("renderer: first picture at frame %llu", (unsigned long long)frames_);
        }
        if ((every_ && frames_ % uint64_t(every_) == 0) || at_.count(long(frames_))) Save();
        if (frames_ % 600 == 0) LOGI("renderer: frame %llu", (unsigned long long)frames_);
    }
    void Swap() override {
        glFlush();
        // the Switch's pace: 60 frames a second at most
        auto due = last_ + std::chrono::microseconds(16667);
        auto now = std::chrono::steady_clock::now();
        if (now < due) std::this_thread::sleep_until(due);
        last_ = std::chrono::steady_clock::now();
    }

private:
    // the 4:3 picture in the middle of the screen, rows top first
    void Save() {
        constexpr uint32_t w = 960, h = 720, x0 = (kScreenW - w) / 2;
        std::vector<uint8_t> px(size_t(w) * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, screen_);
        glReadPixels(GLint(x0), 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        char name[64];
        std::snprintf(name, sizeof name, "cap/gl_%05llu.tga", (unsigned long long)frames_);
        FILE* f = std::fopen((app::ExeDir() / name).string().c_str(), "wb");
        if (!f) return;
        uint8_t head[18] = {0, 0, 2};
        head[12] = uint8_t(w), head[13] = uint8_t(w >> 8), head[14] = uint8_t(h), head[15] = uint8_t(h >> 8);
        head[16] = 24;
        head[17] = 0x00;  // bottom row first: the window's rows are GL's
        std::fwrite(head, 1, 18, f);
        std::vector<uint8_t> row(size_t(w) * 3);
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++)
                for (int c = 0; c < 3; c++) row[x * 3 + c] = px[(size_t(y) * w + x) * 4 + c];
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
    }

    EGLDisplay dpy_ = EGL_NO_DISPLAY;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    GLuint screen_ = 0, screen_tex_ = 0;
    uint64_t frames_ = 0;
    long every_ = 0;
    std::set<long> at_;
    bool shown_ = false;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

// REFAT="f f ...": those frames drawn by the reference renderer too, into guest memory, saved as cap/ref_<frame>.tga
// beside the GL picture (and with IDMAP=1, cap/ids_<frame>.bin), to compare the two renderers on the same frame
class DualBackend final : public gpu::Backend {
public:
    explicit DualBackend(std::unique_ptr<gpu::Backend> gl) : gl_(std::move(gl)) {
        if (const char* at = std::getenv("REFAT"))
            for (const char* p = at; *p;) {
                char* q;
                long f = std::strtol(p, &q, 10);
                if (q == p) break;
                at_.insert(f);
                p = q;
            }
    }
    bool Init(uint32_t w, uint32_t h) override { return gl_->Init(w, h); }
    void Clear(const gpu::State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        if (ref_) reference::Clear(s, n, rects, flags, color, z, stencil);
        gl_->Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const gpu::State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        if (ref_) reference::Draw(s, prim, start, count, indices);
        gl_->Draw(s, prim, start, count, indices);
    }
    void Copy(const gpu::Surface& src, const gpu::Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        if (ref_) reference::Copy(src, dst, n, rects, points);
        gl_->Copy(src, dst, n, rects, points);
    }
    void Present(const gpu::Surface& front) override {
        if (ref_) {
            char name[64];
            std::snprintf(name, sizeof name, "cap/ref_%05llu.tga", (unsigned long long)frame_ + 1);
            reference::WriteTga(front, (app::ExeDir() / name).string().c_str());
        }
        gl_->Present(front);
        frame_++;
        ref_ = at_.count(long(frame_) + 1) != 0;  // the next present is number frame_ + 1, as the host counts
        if (ref_) reference::BeginFrame();
    }
    void Sync(uint32_t data, uint32_t size) override { gl_->Sync(data, size); }
    void SyncLate(uint32_t data) override { gl_->SyncLate(data); }
    void Flush() override { gl_->Flush(); }
    void SetGamma(const uint8_t* ramp) override { gl_->SetGamma(ramp); }
    void ClearReport() override { gl_->ClearReport(); }
    void GetReport(uint32_t r) override { gl_->GetReport(r); }

private:
    std::unique_ptr<gpu::Backend> gl_;
    std::set<long> at_;
    uint64_t frame_ = 0;
    bool ref_ = false;
};

}  // namespace

std::unique_ptr<gpu::Backend> CreateGlBackend() {
    static EglHost host;
    std::filesystem::create_directories(app::ExeDir() / "cap");
    auto gl = glr::CreateBackend(&host);
    if (std::getenv("REFAT")) return std::make_unique<DualBackend>(std::move(gl));
    return gl;
}

}  // namespace harness
#endif
