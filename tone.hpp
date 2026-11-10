#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Mono signed 16-bit PCM, capped at ten seconds per clip.
inline std::vector<int16_t> MakeTone(float freqHz, float durationSec, int type,
                                     float amp = 0.6f) {
    constexpr int sampleRate = 44100;
    constexpr double tau = 6.283185307179586;
    if (!std::isfinite(freqHz) || !std::isfinite(durationSec) ||
        !std::isfinite(amp) || freqHz <= 0 || freqHz >= sampleRate / 2 ||
        durationSec <= 0 || durationSec > 10 || amp < 0 || amp > 1 ||
        type < 0 || type > 4) return {};
    const int samples = static_cast<int>(sampleRate * durationSec);
    std::vector<int16_t> buffer(samples);
    uint32_t rng = 1234567u;
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double cycle = freqHz * t;
        double value = 0;
        switch (type) {
        case 0: value = std::sin(tau * cycle); break;
        case 1: value = std::fmod(cycle, 1.0) < 0.5 ? 1 : -1; break;
        case 2: value = 2 * std::fmod(cycle, 1.0) - 1; break;
        case 3:
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            value = static_cast<double>(rng & 0xffff) / 32767.5 - 1;
            break;
        case 4:
            // Integrate frequency for a linear chirp; finish below Nyquist.
            value = std::sin(tau * (cycle + 0.5 *
                std::min<double>(freqHz, sampleRate / 2.0 - freqHz - 1) *
                t * t / durationSec));
            break;
        }
        const double fade = std::min(1.0, static_cast<double>(samples - 1 - i) /
                                               std::max(1.0, samples * 0.1));
        buffer[i] = static_cast<int16_t>(value * amp * fade * 32767);
    }
    return buffer;
}
