// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// resampler: the host <-> 32 kHz bridge the plugin uses, with an identity
// voice in place of Elements (aux = blow, main = strike), so the path is
// linear. Checks level, latency against the reported value, and everything
// that is not the input sine (aliases, images, phase interpolation error).
#include "../src/rate_bridge.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) { throw std::runtime_error(message); }
}

struct Fit {
    double gain_db, delay, residual_db, total_db;
};

// A sine at `frequency` into blow_in; fits the left output with a sine at the
// same frequency after the filters settle.
Fit measure(uint32_t rate, double frequency)
{
    chamber::RateBridge bridge;
    require(bridge.configure(rate), "configure " + std::to_string(rate));
    const uint32_t frames = rate;          // 1 s
    const uint32_t settle = rate / 10;
    const double w = 2.0 * M_PI * frequency / rate;
    const double amplitude = 0.5;
    std::vector<double> out(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        const float in[2] = {static_cast<float>(amplitude * std::sin(w * i)), 0.0f};
        float y[2];
        bridge.frame(in, y, [](float*) {}, [](chamber::BlockBuffer& b) {
            for (uint32_t k = 0; k < chamber::BlockBuffer::kSize; ++k) { b.aux[k] = b.blow[k]; b.main[k] = b.strike[k]; }
        });
        out[i] = y[0];
    }
    // Least squares: out ~ s sin(w i) + c cos(w i).
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    for (uint32_t i = settle; i < frames; ++i) {
        const double s = std::sin(w * i), c = std::cos(w * i);
        ss += s * s; sc += s * c; cc += c * c; ys += out[i] * s; yc += out[i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double residual = 0, total = 0;
    for (uint32_t i = settle; i < frames; ++i) {
        const double e = out[i] - a * std::sin(w * i) - b * std::cos(w * i);
        residual += e * e;
        total += out[i] * out[i];
    }
    total = std::sqrt(total / (frames - settle));
    residual = std::sqrt(residual / (frames - settle));
    // a sin + b cos = A sin(w i + phi), phi = -w * delay.
    double phase = std::atan2(b, a);
    const double gain = std::hypot(a, b) / amplitude;
    double delay = -phase / w;
    // Unwrap towards the expected latency.
    const double period = rate / frequency;
    delay += period * std::round((bridge.exact_latency() - delay) / period);
    return {20 * std::log10(gain), delay, 20 * std::log10(residual / amplitude + 1e-30),
            20 * std::log10(total / amplitude + 1e-30)};
}

double stopband(uint32_t rate, double frequency)
{
    // Above the 32 kHz side's 18 kHz stopband edge: whatever comes out (its
    // alias below 16 kHz, mostly) must be small. RMS relative to the input.
    return measure(rate, frequency).total_db;
}

} // namespace

int main()
{
    try {
        for (uint32_t rate : {48000u, 44100u, 96000u}) {
            chamber::RateBridge bridge;
            require(bridge.configure(rate), "configure");
            double worst_gain = 0, worst_residual = -999, worst_delay = 0;
            for (double frequency : {50.0, 440.0, 1000.0, 3000.0, 7000.0, 10000.0, 12000.0, 14000.0}) {
                const Fit f = measure(rate, frequency);
                worst_gain = std::max(worst_gain, std::fabs(f.gain_db));
                worst_residual = std::max(worst_residual, f.residual_db);
                worst_delay = std::max(worst_delay, std::fabs(f.delay - bridge.latency()));
            }
            const double reject = std::max(stopband(rate, 19000.0), stopband(rate, 21000.0));
            std::printf("resampler %u Hz: latency %u frames (exact %.3f), passband to 14 kHz within %.4f dB, "
                        "residual %.1f dB, delay error %.3f frames, 19-21 kHz at %.1f dB\n",
                        rate, bridge.latency(), bridge.exact_latency(), worst_gain, worst_residual, worst_delay, reject);
            require(worst_gain < 0.05, "passband flat within 0.05 dB");
            require(worst_residual < -70, "aliases and images below -70 dB");
            require(reject < -70, "19 and 21 kHz rejected by 70 dB");
            // At 48 and 96 kHz the latency is a whole number of frames and
            // the reported value is exact; at 44.1 kHz it is rounded.
            require(worst_delay < (rate == 44100 ? 0.51 : 0.01), "latency equals the reported value");
        }
        chamber::RateBridge bridge;
        require(bridge.configure(32000) && bridge.latency() == 16, "32 kHz: no resampling, one block");
        std::cout << "PASS resampler: 48, 44.1 and 96 kHz level, aliasing, latency; 32 kHz bypassed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL resampler: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
