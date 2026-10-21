// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "block_buffer.hpp"
#include "resampler.hpp"

#include <cmath>
#include <cstdint>
#include <new>

namespace chamber {

// dsp/dsp.h: kSampleRate. The voice always runs at this rate.
inline constexpr uint32_t kVoiceRate = 32000;

// Host rate -> 32 kHz -> host rate around a voice that runs in Elements'
// 16-frame blocks. At a 32 kHz host rate both resamplers are bypassed.
class RateBridge {
public:
    // Allocates; not real-time.
    bool configure(uint32_t host_rate) noexcept
    {
        try {
            if (!down_.configure(host_rate, kVoiceRate) || !up_.configure(kVoiceRate, host_rate)) { return false; }
            // Round the upsampler's delay up so the total latency is a whole
            // number of host frames when the rate allows (48 kHz: 88 frames).
            uint32_t up_delay = up_.delay();
            for (uint32_t extra = 0; extra < 8 && !up_.identity(); ++extra) {
                if ((static_cast<uint64_t>(up_delay + extra + BlockBuffer::kSize) * host_rate) % kVoiceRate == 0) {
                    up_delay += extra;
                    break;
                }
            }
            if (!up_.configure(kVoiceRate, host_rate, up_delay)) { return false; }
            // The downsampler's delay in host frames, then the upsampler's and
            // the staging's in voice samples.
            exact_latency_ = down_.delay() + static_cast<double>(up_.delay() + BlockBuffer::kSize) * host_rate / kVoiceRate;
            latency_ = static_cast<uint32_t>(std::lround(exact_latency_));
        } catch (const std::bad_alloc&) {
            return false;
        }
        reset();
        return true;
    }

    void reset() noexcept
    {
        down_.reset();
        up_.reset();
        block_.reset();
    }

    uint32_t latency() const { return latency_; }
    double exact_latency() const { return exact_latency_; }

    // One host frame. `in` is {blow, strike}; `out` gets {left, right}
    // (elements.cc writes aux to the left and main to the right).
    // condition(float* voice_in) sees each 32 kHz input pair before it is
    // staged; process(BlockBuffer&) runs the voice on a full block.
    template <typename Condition, typename Process>
    void frame(const float* in, float* out, Condition&& condition, Process&& process) noexcept
    {
        down_.push(in);
        while (down_.ready()) {
            float voice_in[2];
            down_.next(voice_in);
            condition(voice_in);
            const uint32_t p = block_.position;
            block_.blow[p] = voice_in[0];
            block_.strike[p] = voice_in[1];
            const float voice_out[2] = {block_.aux[p], block_.main[p]};
            up_.push(voice_out);
            if (++block_.position == BlockBuffer::kSize) {
                process(block_);
                block_.position = 0;
            }
        }
        // Always ready: a host frame's output needs voice samples up to its
        // own time, which the frames so far have produced.
        out[0] = out[1] = 0.0f;
        if (up_.ready()) { up_.next(out); }
    }

private:
    Resampler<2> down_;
    Resampler<2> up_;
    BlockBuffer block_{};
    uint32_t latency_ = 0;
    double exact_latency_ = 0;
};

} // namespace chamber
