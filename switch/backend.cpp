// The Switch's renderers. The GPU one (gl/, OpenGL 4.3 through Mesa's nouveau driver) draws every frame; the CPU one
// is the fallback when GL cannot start, or with R held at start: the harness's reference renderer draws a frame now
// and then into guest memory and that frame goes to the screen. In between, frames are counted and paced to 60 a
// second without being drawn, so the game runs at its speed while the picture updates every so often.
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "core/log.h"
#include "gl/gl_backend.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "reference_renderer.h"
#include "switch/screen.h"
#include "switch/settings.h"
#include "switch/state.h"

namespace gpu {

namespace {

using Clock = std::chrono::steady_clock;

class SwitchBackend final : public Backend {
public:
    bool Init(uint32_t w, uint32_t h) override {
        LOGI("renderer: Switch test renderer (CPU), %ux%u", w, h);
        next_draw_ = Clock::now();
        last_present_ = Clock::now();
        return true;
    }
    void Clear(const State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        if (drawing_) reference::Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        draws_++;
        sw::g_stats.draws.fetch_add(1, std::memory_order_relaxed);
        if (drawing_) reference::Draw(s, prim, start, count, indices);
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        if (drawing_) reference::Copy(src, dst, n, rects, points);
        else CopySurfaceMemory(src, dst, n, rects, points);
    }
    void Present(const Surface& front) override {
        auto now = Clock::now();
        if (drawing_) {
            sw::g_stats.drawn.fetch_add(1, std::memory_order_relaxed);
            // the status text stays until the game draws something: its first frames are only cleared to black
            uint32_t w = 0, h = 0;
            if ((sw::ShowingGame() || draws_ > draws_at_draw_) && reference::ReadRgba(front, pixels_, w, h)) {
                if (!sw::ShowingGame()) LOGI("renderer: first picture with %llu draws, the status text goes", draws_ - draws_at_draw_);
                sw::Frame(pixels_.data(), w, h);
                sw::g_stats.shown.fetch_add(1, std::memory_order_relaxed);
            }
            auto took = std::chrono::duration_cast<std::chrono::milliseconds>(now - draw_start_).count();
            // as much time running undrawn as the last drawing took, at least half a second
            next_draw_ = now + std::chrono::milliseconds(std::max<long long>(500, took));
            if (shown_++ < 20 || shown_ % 50 == 0)
                LOGI("renderer: frame %llu drawn in %lld ms, %llu draws so far", frames_, (long long)took, draws_);
        } else {
            // the game's own pace while nothing is drawn (30 frames a second for this game), the CPU left to its threads
            auto due = last_present_ + std::chrono::microseconds(16667 * std::max<uint32_t>(blanks_, 1));
            if (now < due) std::this_thread::sleep_until(due);
        }
        last_present_ = Clock::now();
        frames_++;
        sw::g_stats.frames.fetch_add(1, std::memory_order_relaxed);
        if (frames_ % 600 == 0) LOGI("renderer: frame %llu, %llu draws", frames_, draws_);
        drawing_ = last_present_ >= next_draw_;
        if (drawing_) {
            draw_start_ = last_present_;
            draws_at_draw_ = draws_;
            reference::BeginFrame();
        }
    }
    void Sync(uint32_t, uint32_t) override {}
    void SyncLate(uint32_t) override {}
    void Flush() override {}
    void SetGamma(const uint8_t*) override {}
    void SetSwapInterval(uint32_t blanks) override { blanks_ = blanks; }

private:
    uint32_t blanks_ = 1;
    unsigned long long frames_ = 0, draws_ = 0, shown_ = 0, draws_at_draw_ = 0;
    bool drawing_ = false;
    Clock::time_point next_draw_, draw_start_, last_present_;
    std::vector<uint32_t> pixels_;
};

// the window and its context for the GPU renderer, from switch/screen.cpp
class SwitchHost final : public glr::Host {
public:
    bool MakeCurrent() override { return sw::GlTake(); }
    void Release() override { sw::GlRelease(); }
    void ScreenSize(uint32_t& w, uint32_t& h) override {
        w = 1280;
        h = 720;
    }
    void Presented(uint32_t draws) override { sw::GlPresented(draws); }
    void SetSwapInterval(uint32_t blanks) override { sw::GlSwapInterval(blanks); }
    // beside the .nro (the working folder): the programs built, built again ahead at the next start
    std::string CachePath() override { return "cache/shaders.cache"; }
    // the debug menu's Resolution, Screen and Upscaling
    glr::Picture GetPicture() override {
        glr::Picture p;
        p.height = sw::g_resolution.load(std::memory_order_relaxed);
        // the handheld screen has 720 rows: 1080p there would only be drawn to be shrunk, for half the frame rate
        if (p.height > 720 && appletGetOperationMode() != AppletOperationMode_Console) p.height = 720;
        int screen = sw::g_screen.load(std::memory_order_relaxed);
        p.wide = screen == int(sw::Screen::Wide);
        p.sides = screen == int(sw::Screen::Blur) ? glr::Picture::Sides::Blur
                  : screen == int(sw::Screen::Web) ? glr::Picture::Sides::Web
                  : screen == int(sw::Screen::Suit) ? glr::Picture::Sides::Suit
                                                    : glr::Picture::Sides::Black;
        p.sharp_movies = sw::g_sharp.load(std::memory_order_relaxed);
        p.anisotropy = sw::g_anisotropy.load(std::memory_order_relaxed);
        return p;
    }
    void Swap() override { sw::GlSwap(); }
};

// the GPU renderer, or the CPU one when it does not start
class FirstGl final : public Backend {
public:
    bool Init(uint32_t w, uint32_t h) override {
        static SwitchHost host;
        inner_ = glr::CreateBackend(&host);
        if (inner_->Init(w, h)) return true;
        LOGE("renderer: the GPU renderer did not start");
        inner_.reset();
        sw::GlFallBack();
        inner_ = std::make_unique<SwitchBackend>();
        return inner_->Init(w, h);
    }
    void Clear(const State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        inner_->Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        inner_->Draw(s, prim, start, count, indices);
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        inner_->Copy(src, dst, n, rects, points);
    }
    void Present(const Surface& front) override { inner_->Present(front); }
    void Sync(uint32_t data, uint32_t size) override { inner_->Sync(data, size); }
    void SyncLate(uint32_t data) override { inner_->SyncLate(data); }
    void Flush() override { inner_->Flush(); }
    void SetGamma(const uint8_t* ramp) override { inner_->SetGamma(ramp); }
    void SetSwapInterval(uint32_t blanks) override { inner_->SetSwapInterval(blanks); }
    void ClearReport() override { inner_->ClearReport(); }
    void GetReport(uint32_t r) override { inner_->GetReport(r); }

private:
    std::unique_ptr<Backend> inner_;
};

}  // namespace

std::unique_ptr<Backend> CreateBackend() {
    if (sw::UsingGl()) return std::make_unique<FirstGl>();
    return std::make_unique<SwitchBackend>();
}

}  // namespace gpu
