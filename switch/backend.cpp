// The Switch test renderer: the harness's CPU reference renderer draws a frame now and then into guest memory, and
// that frame goes to the screen. In between, frames are counted and paced to 60 a second without being drawn, so
// the game runs at its speed while the picture updates every so often. A GPU backend replaces this later.
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "reference_renderer.h"
#include "switch/screen.h"
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
            // 60 frames a second while nothing is drawn: the game's own pace, and the CPU left to its threads
            auto due = last_present_ + std::chrono::microseconds(16667);
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

private:
    unsigned long long frames_ = 0, draws_ = 0, shown_ = 0, draws_at_draw_ = 0;
    bool drawing_ = false;
    Clock::time_point next_draw_, draw_start_, last_present_;
    std::vector<uint32_t> pixels_;
};

}  // namespace

std::unique_ptr<Backend> CreateBackend() { return std::make_unique<SwitchBackend>(); }

}  // namespace gpu
