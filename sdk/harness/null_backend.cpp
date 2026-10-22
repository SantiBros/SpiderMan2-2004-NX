// headless renderer: counts frames and draws, logs draw lists, draws chosen frames with the reference renderer
// REF=N: every Nth frame drawn on the CPU into guest memory and saved as cap/ref_<frame>.tga
// CAP=N: every Nth frame's draws listed in cap/draws_<frame>.txt
// snap in the run folder: the next two frames drawn and saved as snap_1.tga and snap_2.tga, then snap is removed
// SWITCH=1: the Switch build's pacing (switch/backend.cpp): 60 frames a second, a frame drawn when as much time as the
//   last drawing took has passed (at least half a second), saved as cap/switch_<frame>.tga from the first one with draws
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "harness.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "reference_renderer.h"

namespace gpu {

namespace {

class NullBackend final : public Backend {
public:
    bool Init(uint32_t w, uint32_t h) override {
        LOGI("renderer: headless %ux%u", w, h);
        std::filesystem::create_directories(app::ExeDir() / "cap");
        return true;
    }
    void Clear(const State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        if (drawing_) reference::Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        draws_++;
        if (drawing_) reference::Draw(s, prim, start, count, indices);
        if (list_) {
            List(s, prim, start, count);
            if (drawing_) Stats();
        }
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        if (drawing_) reference::Copy(src, dst, n, rects, points);
        else CopySurfaceMemory(src, dst, n, rects, points);
    }
    void Present(const Surface& front) override {
        static const bool like_switch = harness::Env("SWITCH") != 0;
        if (like_switch) return PresentLikeSwitch(front);
        if (list_) {
            std::fclose(list_);
            list_ = nullptr;
        }
        if (draws_ == last_draws_) empty_++;
        last_draws_ = draws_;
        if (drawing_ && !snap_) reference::WriteTga(front, File("cap/ref_%05llu.tga", frames_).c_str());
        // snap asks for the next two frames drawn
        std::string snap = (app::ExeDir() / "snap").string();
        if (snap_ == 2) {
            reference::WriteTga(front, (app::ExeDir() / "snap_2.tga").string().c_str());
            std::remove(snap.c_str());
            snap_ = 0;
        } else if (snap_ == 1) {
            reference::WriteTga(front, (app::ExeDir() / "snap_1.tga").string().c_str());
            snap_ = 2;
        } else if (FILE* f = std::fopen(snap.c_str(), "r")) {
            std::fclose(f);
            snap_ = 1;
        }
        static const long every = harness::Env("REF"), cap = harness::Env("CAP");
        drawing_ = snap_ || (every && (frames_ + 1) % every == 0);
        if (drawing_) reference::BeginFrame();
        harness::g_frames = frames_ + 1;
        if (++frames_ % 60 == 0) LOGI("renderer: frame %llu, %llu draws, %llu empty frames", frames_, draws_, empty_);
        if (cap && frames_ % cap == 0) list_ = std::fopen(File("cap/draws_%05llu.txt", frames_).c_str(), "w");
    }
    void Sync(uint32_t, uint32_t) override {}
    void SyncLate(uint32_t) override {}
    void Flush() override {}
    void SetGamma(const uint8_t*) override {}
    void SetSwapInterval(uint32_t blanks) override { blanks_ = blanks; }
    uint32_t blanks_ = 1;
    // visibility tests: a frame the reference renderer draws counts its pixels; in the others nothing is drawn, so a
    // report repeats the count it had when it was last drawn, and one never drawn says visible
    void ClearReport() override { reference::ResetCount(); }
    void GetReport(uint32_t va) override {
        static const bool off = harness::Env("NOREPORTS") != 0;
        if (off) return;
        uint32_t count;
        if (drawing_) {
            count = uint32_t(std::min<uint64_t>(reference::CountedPixels(), 0xFFFFFFFF));
            report_counts_[va] = count;
        } else {
            auto it = report_counts_.find(va);
            count = it != report_counts_.end() ? it->second : kVisible;
        }
        LOG_FIRST(8, "harness: visibility report at %08X: %u pixels%s", va, count, drawing_ ? "" : " (not drawn, last count)");
        kernel::W32(va, uint32_t(frames_));
        kernel::W32(va + 4, 0);
        kernel::W32(va + 8, count);
        kernel::W32(va + 12, 0);  // done
    }

private:
    static constexpr uint32_t kVisible = 1000;
    std::map<uint32_t, uint32_t> report_counts_;
    using Clock = std::chrono::steady_clock;
    void PresentLikeSwitch(const Surface& front) {
        auto now = Clock::now();
        if (list_) {
            std::fclose(list_);
            list_ = nullptr;
        }
        if (drawing_) {
            if (shown_ || draws_ > draws_at_draw_) {
                if (!shown_) LOGI("renderer: first picture with %llu draws, the status text goes", draws_ - draws_at_draw_);
                reference::WriteTga(front, File("cap/switch_%05llu.tga", frames_).c_str());
                Ids(front, File("cap/ids_%05llu.bin", frames_));
                shown_++;
            }
            auto took = std::chrono::duration_cast<std::chrono::milliseconds>(now - draw_start_).count();
            next_draw_ = now + std::chrono::milliseconds(std::max<long long>(500, took));
            if (drawn_++ < 20 || drawn_ % 50 == 0)
                LOGI("renderer: frame %llu drawn in %lld ms, %llu draws in it, %llu so far", frames_, (long long)took,
                     draws_ - draws_at_draw_, draws_);
        } else {
            auto due = last_present_ + std::chrono::nanoseconds(kernel::HostNsFor(16666667 * int64_t(std::max<uint32_t>(blanks_, 1))));
            if (now < due) std::this_thread::sleep_until(due);
        }
        last_present_ = Clock::now();
        harness::g_frames = ++frames_;
        if (frames_ % 600 == 0) LOGI("renderer: frame %llu, %llu draws", frames_, draws_);
        static const bool never = harness::Env("NODRAW") != 0;  // pacing only: what the game itself costs
        drawing_ = !never && last_present_ >= next_draw_;
        if (drawing_) {
            draw_start_ = last_present_;
            draws_at_draw_ = draws_;
            reference::BeginFrame();
            // CAP=1: every drawn frame's draws listed
            static const long cap = harness::Env("CAP");
            if (cap) list_ = std::fopen(File("cap/draws_%05llu.txt", frames_).c_str(), "w");
        }
    }
    unsigned long long drawn_ = 0, shown_ = 0, draws_at_draw_ = 0;
    Clock::time_point next_draw_ = Clock::now(), draw_start_, last_present_ = Clock::now();

    static std::string File(const char* fmt, unsigned long long n) {
        char name[64];
        std::snprintf(name, sizeof name, fmt, n);
        return (app::ExeDir() / name).string();
    }

    // what the reference renderer did with the draw
    void Stats() {
        const reference::DrawStats& d = reference::LastDrawStats();
        if (d.skipped) {
            std::fprintf(list_, "  ref #%u skipped\n", d.draw);
            return;
        }
        std::fprintf(list_, "  ref #%u tris %u clipped %u culled %u outside %u | px %u killed %u alpha %u stencil %u z %u written %u | box %.0f %.0f %.0f %.0f\n",
                     d.draw, d.triangles, d.clipped, d.culled, d.outside, d.pixels, d.killed, d.alpha_failed, d.stencil_failed,
                     d.z_failed, d.written, d.min_x, d.min_y, d.max_x, d.max_y);
    }
    // IDMAP=1: the front buffer's draw numbers, w and h as two u32 then four u16 per pixel
    static void Ids(const Surface& front, const std::string& path) {
        uint32_t w = 0, h = 0;
        const std::vector<uint16_t>* ids = reference::DrawIds(front, w, h);
        if (!ids) {
            static const bool want = harness::Env("IDMAP") != 0;
            if (want) LOG_FIRST(4, "harness: no draw ids for the front buffer at %08X", front.data);
            return;
        }
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fwrite(&w, 4, 1, f);
            std::fwrite(&h, 4, 1, f);
            std::fwrite(ids->data(), 2, ids->size(), f);
            std::fclose(f);
        }
    }

    // one draw's state, textures, transforms, declaration and first vertex
    void List(const State& s, uint32_t prim, uint32_t start, uint32_t count) {
        FILE* f = list_;
        std::fprintf(f, "DRAW prim=%u n=%u vs=%X ps=%X rt=%08X tex0=%08X light=%u blend=%u %X %X cw=%X at=%u z=%u\n", prim, count,
                     s.vs_handle, s.ps ? s.ps->handle : 0, s.rt.data, s.textures[0], s.rs[kRsLighting], s.simple[kMBlendEnable >> 2],
                     s.simple[kMSrcBlend >> 2], s.simple[kMDestBlend >> 2], s.simple[kMColorWriteEnable >> 2],
                     s.simple[kMAlphaTestEnable >> 2], s.rs[kRsZEnable]);
        const auto& t0 = s.tss[0];
        TextureDesc d;
        if (s.textures[0] && Describe(s.textures[0], d))
            std::fprintf(f, "  tex %ux%u fmt %X cop %u %X %X aop %u %X %X\n", d.width, d.height, d.format, t0[kTssColorOp],
                         t0[kTssColorArg1], t0[kTssColorArg2], t0[kTssAlphaOp], t0[kTssAlphaArg1], t0[kTssAlphaArg2]);
        std::fprintf(f, "  stages");
        for (int i = 0; i < 4; i++) {
            TextureDesc t;
            if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], t))
                std::fprintf(f, " t%d=%08X:%ux%u/%X", i, t.data, t.width, t.height, t.format);
        }
        std::fprintf(f, " zs=%08X modes=%X\n", s.zs.data, s.rs[kRsPsTextureModes]);
        std::fprintf(f, "  ttf %X tci %X addr %u %u t16", t0[kTssTextureTransformFlags], t0[kTssTexCoordIndex], t0[kTssAddressU],
                     t0[kTssAddressV]);
        auto matrix = [&](const char* name, uint32_t which) {
            std::fprintf(f, "%s", name);
            for (float v : s.transforms[which]) std::fprintf(f, " %g", v);
        };
        matrix("", 16);
        matrix("\n  world", 256);
        matrix("\n  view", 2);
        matrix("\n  proj", 3);
        std::fprintf(f, "\n");
        if (s.vs && !s.vs->program.empty()) {
            uint64_t h = gpu::Hash(s.vs->program.data(), s.vs->program.size() * 4);
            std::fprintf(f, "  program %zu instructions %016llX\n", s.vs->program.size() / 4, (unsigned long long)h);
            // DUMPVS=1: each program once as HLSL, cap/vs_<hash>.hlsl
            static const bool dump = harness::Env("DUMPVS") != 0;
            static std::set<uint64_t> dumped;
            if (dump && dumped.insert(h).second)
                if (FILE* o = std::fopen(File("cap/vs_%016llX.hlsl", h).c_str(), "w")) {
                    std::string src = gpu::VertexProgramHlsl(s.vs->program, 0xFFFF);
                    std::fwrite(src.data(), 1, src.size(), o);
                    std::fclose(o);
                }
        }
        if (s.vs) {
            std::fprintf(f, "  decl");
            for (int r = 0; r < 16; r++) {
                const auto& in = s.vs->inputs[size_t(r)];
                if (in.stream >= 0) std::fprintf(f, " v%d:%d+%u/%X", r, in.stream, in.offset, in.type);
            }
            std::fprintf(f, "\n");
        }
        if (s.streams[0].vb) {
            // the vertex buffer's Data at +4 is a physical address
            uint32_t data = mem::VirtualFromPhysical(kernel::R32(s.streams[0].vb + 4)) + start * s.streams[0].stride;
            std::fprintf(f, "  v0 stride %u:", s.streams[0].stride);
            for (uint32_t i = 0; i < s.streams[0].stride / 4 && i < 10; i++) {
                uint32_t v = kernel::R32(data + i * 4);
                float x;
                std::memcpy(&x, &v, 4);
                std::fprintf(f, " %g(%08X)", x, v);
            }
            std::fprintf(f, "\n");
        }
    }

    unsigned long long frames_ = 0, draws_ = 0, last_draws_ = 0, empty_ = 0;
    bool drawing_ = false;
    int snap_ = 0;
    FILE* list_ = nullptr;
};

}  // namespace

}  // namespace gpu

#ifdef XBE_GL_HARNESS
namespace harness {
std::unique_ptr<gpu::Backend> CreateGlBackend();
}
#endif

namespace gpu {

// GL=1 in a build with the GL renderer: it draws, else the headless renderer above
std::unique_ptr<Backend> CreateBackend() {
#ifdef XBE_GL_HARNESS
    if (harness::Env("GL")) return harness::CreateGlBackend();
#endif
    return std::make_unique<NullBackend>();
}

}  // namespace gpu
