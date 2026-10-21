// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "chamber.hpp"

#include "elements/dsp/part.h"
#include "stmlib/utils/random.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace chamber {
namespace {

// elements.cc: kNoiseGateThreshold.
constexpr float kNoiseGateThreshold = 0.0001f;
// A model or Ominous switch fades out over 4 ms, then in over 4 ms
// (128 samples at 32 kHz).
constexpr float kSwitchStep = 1.0f / 128.0f;

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000) { return nullptr; }
    return Chamber::create(rate);
}
void connect_port(LV2_Handle h, uint32_t index, void* data) { static_cast<Chamber*>(h)->connect(index, data); }
void activate(LV2_Handle h) { static_cast<Chamber*>(h)->activate(); }
void run(LV2_Handle h, uint32_t frames) { static_cast<Chamber*>(h)->run(frames); }
void cleanup(LV2_Handle h) { delete static_cast<Chamber*>(h); }
const void* extension_data(const char*) { return nullptr; }

float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }

// elements.cc: FillBuffer's input level follower and noise gate.
float gate_input(float sample, float& level)
{
    const float error = sample * sample - level;
    level += error * (error > 0.0f ? 0.1f : 0.0001f);
    const float gain = level <= kNoiseGateThreshold ? (1.0f / kNoiseGateThreshold) * level : 1.0f;
    return gain * sample;
}

} // namespace

const LV2_Descriptor descriptor{
    kURI, instantiate, connect_port, activate, run, nullptr, cleanup, extension_data,
};

Chamber* Chamber::create(double rate) noexcept
{
    auto* chamber = new (std::nothrow) Chamber(rate);
    if (!chamber) { return nullptr; }
    // Part's constructor leaves its members as they are, and Part::Init does
    // not set all of them (panic_, easter_egg_): on the hardware Part is a
    // zero-initialised global. activate() builds it in zeroed memory.
    void* memory = std::calloc(1, sizeof(elements::Part));
    chamber->reverb_ = new (std::nothrow) uint16_t[kReverbSize];
    const bool bridge = chamber->bridge_.configure(static_cast<uint32_t>(std::llround(rate)));
    if (!memory || !chamber->reverb_ || !bridge) {
        std::free(memory);
        delete chamber;
        return nullptr;
    }
    chamber->part_ = static_cast<elements::Part*>(memory);
    chamber->activate();
    return chamber;
}

Chamber::Chamber(double rate) noexcept
    : rate_(rate), smoothing_(static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate))))
{
}

Chamber::~Chamber()
{
    if (part_) {
        part_->~Part();
        std::free(part_);
    }
    delete[] reverb_;
}

void Chamber::connect(uint32_t index, void* data) noexcept
{
    if (index < kPortCount) { ports_[index] = static_cast<float*>(data); }
}

void Chamber::activate() noexcept
{
    // Part::Init leaves some voice state as it was (the hardware only runs it
    // once, at power-on), so start again from zeroed memory.
    std::memset(static_cast<void*>(part_), 0, sizeof(elements::Part));
    part_ = new (part_) elements::Part;
    part_->Init(reverb_);
    uint32_t seed[3] = {kSeed[0], kSeed[1], kSeed[2]};
    part_->Seed(seed, 3);
    part_->set_easter_egg(false);
    controls_.reset();
    bridge_.reset();
    random_state_ = kRandomSeed;
    model_ = ominous_ = -1;
    switch_gain_ = 1;
    blow_in_follower_ = strike_in_follower_ = 0;
    first_ = true;
}

float Chamber::control(Port port, float fallback, float minimum, float maximum) const noexcept
{
    const float* data = ports_[static_cast<uint32_t>(port)];
    return data && std::isfinite(*data) ? std::clamp(*data, minimum, maximum) : fallback;
}

float Chamber::input(Port port, uint32_t frame) const noexcept
{
    const float* data = ports_[static_cast<uint32_t>(port)];
    // NaN or infinity reads as silence.
    return data && std::isfinite(data[frame]) ? data[frame] : 0.0f;
}

// Real-time section: no allocation, locks or I/O.
void Chamber::run(uint32_t frames) noexcept
{
    const float blow_in_target = db_to_gain(control(Port::blow_in_level, 0, -24, 24));
    const float strike_in_target = db_to_gain(control(Port::strike_in_level, 0, -24, 24));
    const float output_target = db_to_gain(control(Port::output, 0, -24, 12));
    if (first_) {
        blow_in_gain_ = blow_in_target; strike_in_gain_ = strike_in_target; output_gain_ = output_target;
        first_ = false;
    }
    float* out_l = ports_[static_cast<uint32_t>(Port::out_l)];
    float* out_r = ports_[static_cast<uint32_t>(Port::out_r)];

    for (uint32_t i = 0; i < frames; ++i) {
        blow_in_gain_ += smoothing_ * (blow_in_target - blow_in_gain_);
        strike_in_gain_ += smoothing_ * (strike_in_target - strike_in_gain_);
        output_gain_ += smoothing_ * (output_target - output_gain_);
        // Input levels are clipped at the hardware codec's full scale.
        const float in[2] = {
            std::clamp(input(Port::blow_in, i) * blow_in_gain_, -1.0f, 1.0f),
            std::clamp(input(Port::strike_in, i) * strike_in_gain_, -1.0f, 1.0f),
        };
        float out[2];
        bridge_.frame(in, out,
            [this](float* voice_in) {
                voice_in[0] = gate_input(voice_in[0], blow_in_follower_);
                voice_in[1] = gate_input(voice_in[1], strike_in_follower_);
            },
            // A block completed in host frame i uses that frame's CVs.
            [this, i](BlockBuffer& block) { process_block(block, i); });
        if (out_l) { out_l[i] = out[0] * output_gain_; }
        if (out_r) { out_r[i] = out[1] * output_gain_; }
    }
    if (float* latency = ports_[static_cast<uint32_t>(Port::latency)]) {
        *latency = static_cast<float>(bridge_.latency());
    }
}

void Chamber::process_block(BlockBuffer& block, uint32_t frame) noexcept
{
    // set_resonator_model and set_easter_egg only choose what Part renders:
    // the voice that was sounding stops dead, and the other resumes from
    // where it was left, which clicks. A change fades the output out, then
    // switches, then fades back in. The first block applies the patch's
    // settings at once.
    const int model = static_cast<int>(std::lround(control(Port::model, 0, 0, 2)));
    const int ominous = control(Port::ominous, 0, 0, 1) >= 0.5f;
    bool switching = model != model_ || ominous != ominous_;
    if (switching && (model_ < 0 || switch_gain_ <= 0.0f)) {
        part_->set_resonator_model(static_cast<elements::ResonatorModel>(model));
        part_->set_easter_egg(ominous);
        model_ = model;
        ominous_ = ominous;
        switching = false;
    }

    elements::PerformanceState state;
    controls_.read(ports_, frame, part_->mutable_patch(), &state);
    // elements.cc: the Play button's gate is added to the gate input's.
    state.gate |= control(Port::play, 0, 0, 1) >= 0.5f;

    // stmlib::Random is one state per thread (third_party/eurorack/README.md);
    // each instance keeps its own sequence.
    stmlib::Random::Seed(random_state_);
    part_->Process(state, block.blow, block.strike, block.main, block.aux, BlockBuffer::kSize);
    random_state_ = stmlib::Random::state();

    if (!switching && switch_gain_ == 1.0f) { return; }
    const float step = switching ? -kSwitchStep : kSwitchStep;
    for (uint32_t i = 0; i < BlockBuffer::kSize; ++i) {
        switch_gain_ = std::clamp(switch_gain_ + step, 0.0f, 1.0f);
        block.main[i] *= switch_gain_;
        block.aux[i] *= switch_gain_;
    }
}

} // namespace chamber
