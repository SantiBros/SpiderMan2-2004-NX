// Sound on the Switch through audout: 48 kHz stereo, four 20 ms buffers the SDK's DirectSound mixer fills as each one
// finishes playing. Without audout the mixer is still pulled every 20 ms, so the game's streams keep moving.
#include <switch.h>

#include <malloc.h>
#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "audio/audio.h"
#include "core/log.h"
#include "switch/state.h"

namespace audio {

namespace {
constexpr uint32_t kFrames = kRate / 50;                                  // 20 ms
constexpr uint32_t kBytes = kFrames * 2 * sizeof(int16_t);                // stereo 16 bit
constexpr uint32_t kAlloc = (kBytes + 0xFFF) & ~0xFFFu;                   // audout wants 4 KB aligned buffers
constexpr int kBuffers = 4;

void Fill(const std::function<void(float*, uint32_t)>& render, std::vector<float>& mix, int16_t* out) {
    render(mix.data(), kFrames);
    const float volume = sw::g_volume.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < kFrames * 2; i++) {
        float v = std::clamp(mix[i] * volume, -1.0f, 1.0f);
        out[i] = int16_t(v * 32767.0f);
    }
}
}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    // a pthread with room: std::thread gets 128 KB on the Switch, and the mixer decodes on its stack
    auto* fn = new std::function<void()>([render] {
        std::vector<float> mix(kFrames * 2);
        if (sw::g_no_sound) {
            LOGI("audio: sound off (L held at start), the mixer runs without output");
            for (;;) {
                render(mix.data(), kFrames);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        LOGI("audio: opening audout");
        Result rc = audoutInitialize();
        LOGI("audio: audoutInitialize %08X", rc);
        if (R_SUCCEEDED(rc)) {
            rc = audoutStartAudioOut();
            LOGI("audio: audoutStartAudioOut %08X", rc);
        }
        if (R_FAILED(rc)) {
            LOGE("audio: audout failed (%08X), sound is off", rc);
            for (;;) {
                render(mix.data(), kFrames);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        LOGI("audio: audout at %u Hz, %u channels", audoutGetSampleRate(), audoutGetChannelCount());
        static AudioOutBuffer buffers[kBuffers];
        for (int i = 0; i < kBuffers; i++) {
            void* data = memalign(0x1000, kAlloc);
            std::memset(data, 0, kAlloc);
            buffers[i] = {};
            buffers[i].next = nullptr;
            buffers[i].buffer = data;
            buffers[i].buffer_size = kAlloc;
            buffers[i].data_size = kBytes;
            buffers[i].data_offset = 0;
            Fill(render, mix, static_cast<int16_t*>(data));
            rc = audoutAppendAudioOutBuffer(&buffers[i]);
            if (R_FAILED(rc)) LOGE("audio: buffer %d not queued (%08X)", i, rc);
        }
        LOGI("audio: %d buffers queued", kBuffers);
        int failures = 0;
        for (;;) {
            AudioOutBuffer* done = nullptr;
            u32 count = 0;
            rc = audoutWaitPlayFinish(&done, &count, 1'000'000'000ull);
            if (R_FAILED(rc) || !done) {
                // never spin on the service: a timeout or an error waits a buffer's length before the next try
                if (failures++ < 5) LOGW("audio: no buffer back (%08X, %u)", rc, count);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (sw::g_stats.sound.fetch_add(1, std::memory_order_relaxed) == 0) LOGI("audio: first buffer played");
            Fill(render, mix, static_cast<int16_t*>(done->buffer));
            audoutAppendAudioOutBuffer(done);
        }
    });
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1u << 20);
    pthread_t th;
    if (pthread_create(&th, &attr, [](void* p) -> void* {
            (*static_cast<std::function<void()>*>(p))();
            return nullptr;
        }, fn) == 0)
        pthread_detach(th);
    else
        LOGE("audio: the sound thread could not start");
    pthread_attr_destroy(&attr);
}

}  // namespace audio
