// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// match_elements: at a 32 kHz host rate the plugin's output is the vendored
// Part's, driven directly the way the hardware drives it (elements.cc
// FillBuffer: one control read and one Process call per 16-frame block, one
// Random sequence), bit for bit, one block later.
#include "host.hpp"
#include "../src/chamber.hpp"
#include "../src/controls.hpp"

#include "elements/dsp/part.h"
#include "stmlib/utils/random.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

using chamber::Port;
using test::Instance;
using test::require;
using test::Stereo;

namespace {

constexpr uint32_t kBlock = 16;
constexpr uint64_t kFrames = 6 * 32000;

struct Settings {
    const char* name;
    float model, bow, blow, strike;
};

// Changes on 128-frame period boundaries only (8960 = 70 * 128).
float play(uint64_t frame) { return frame % 32000 < 8960 ? 1.0f : 0.0f; }
float voct(uint64_t frame) { return static_cast<float>(0.15 * std::sin(frame / 32000.0 * 0.9)); }
float damping_cv(uint64_t frame) { return static_cast<float>(0.8 * std::sin(frame / 32000.0 * 2.1)); }

std::vector<Stereo> reference(const Settings& s)
{
    // Part, zeroed as a hardware global.
    void* memory = std::calloc(1, sizeof(elements::Part));
    auto* part = new (memory) elements::Part;
    std::vector<uint16_t> reverb(chamber::kReverbSize);
    part->Init(reverb.data());
    uint32_t seed[3] = {chamber::kSeed[0], chamber::kSeed[1], chamber::kSeed[2]};
    part->Seed(seed, 3);
    part->set_resonator_model(static_cast<elements::ResonatorModel>(s.model));
    stmlib::Random::Seed(chamber::kRandomSeed);

    std::array<float, chamber::kPortCount> controls{};
    std::vector<float> voct_buffer(kFrames), damping_buffer(kFrames);
    chamber::PortArray ports{};
    for (uint32_t p = 0; p < chamber::kPortCount; ++p) { ports[p] = &controls[p]; }
    ports[static_cast<uint32_t>(Port::voct_cv)] = voct_buffer.data();
    ports[static_cast<uint32_t>(Port::damping_cv)] = damping_buffer.data();
    for (uint32_t p = 0; p < chamber::kPortCount; ++p) {
        if (chamber::is_signal(p) && ports[p] == &controls[p]) { ports[p] = nullptr; }
    }
    auto c = [&](Port p) -> float& { return controls[static_cast<uint32_t>(p)]; };
    c(Port::contour) = 0.4f; c(Port::bow_level) = s.bow; c(Port::bow_timbre) = 0.5f;
    c(Port::blow_level) = s.blow; c(Port::blow_meta) = 0.3f; c(Port::blow_timbre) = 0.6f;
    c(Port::strike_level) = s.strike; c(Port::strike_meta) = 0.5f; c(Port::strike_timbre) = 0.5f;
    c(Port::geometry) = 0.4f; c(Port::brightness) = 0.6f; c(Port::damping) = 0.4f; c(Port::position) = 0.3f;
    c(Port::coarse) = 33; c(Port::fine) = 0.2f; c(Port::space) = 0.8f; c(Port::strength) = 0.7f;
    c(Port::damping_amount) = 0.6f;
    for (uint64_t i = 0; i < kFrames; ++i) { voct_buffer[i] = voct(i); damping_buffer[i] = damping_cv(i); }

    chamber::Controls scaler;
    scaler.reset();
    float silence[kBlock] = {}, main[kBlock], aux[kBlock];
    std::vector<Stereo> out(kBlock, Stereo{0, 0}); // the plugin's one-block lag
    for (uint64_t start = 0; start + kBlock <= kFrames; start += kBlock) {
        const uint64_t last = start + kBlock - 1;
        elements::PerformanceState state;
        scaler.read(ports, static_cast<uint32_t>(last), part->mutable_patch(), &state);
        state.gate |= play(last) >= 0.5f;
        part->Process(state, silence, silence, main, aux, kBlock);
        for (uint32_t i = 0; i < kBlock; ++i) { out.push_back({aux[i], main[i]}); }
    }
    part->~Part();
    std::free(memory);
    out.resize(kFrames);
    return out;
}

std::vector<Stereo> plugin(const LV2_Descriptor* d, const Settings& s)
{
    Instance instance(d, 32000, 128);
    auto c = [&](Port p) -> float& { return instance.control(p); };
    c(Port::model) = s.model;
    c(Port::contour) = 0.4f; c(Port::bow_level) = s.bow; c(Port::bow_timbre) = 0.5f;
    c(Port::blow_level) = s.blow; c(Port::blow_meta) = 0.3f; c(Port::blow_timbre) = 0.6f;
    c(Port::strike_level) = s.strike; c(Port::strike_meta) = 0.5f; c(Port::strike_timbre) = 0.5f;
    c(Port::geometry) = 0.4f; c(Port::brightness) = 0.6f; c(Port::damping) = 0.4f; c(Port::position) = 0.3f;
    c(Port::coarse) = 33; c(Port::fine) = 0.2f; c(Port::space) = 0.8f; c(Port::strength) = 0.7f;
    c(Port::damping_amount) = 0.6f;
    // A control port changes only between periods; play() does too.
    return instance.render(kFrames, [](Instance& in, uint32_t i, uint64_t frame) {
        in.signal(Port::voct_cv, i) = voct(frame);
        in.signal(Port::damping_cv, i) = damping_cv(frame);
        if (i == 0) { in.control(Port::play) = play(frame); }
    });
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: test_match chamber.so\n"; return 1; }
    try {
        test::Library library(argv[1]);
        const Settings settings[] = {
            {"modal, strike", 0, 0, 0, 0.8f},
            {"string, bow and blow", 1, 0.6f, 0.7f, 0},
            {"strings, every exciter", 2, 0.4f, 0.5f, 0.6f},
        };
        for (const auto& s : settings) {
            const auto expected = reference(s);
            const auto actual = plugin(library.descriptor, s);
            float peak = 0;
            for (const auto& f : expected) { peak = std::max({peak, std::fabs(f.l), std::fabs(f.r)}); }
            require(peak > 0.01f, std::string(s.name) + ": reference makes sound");
            for (size_t i = 0; i < expected.size(); ++i) {
                require(expected[i] == actual[i], std::string(s.name) + ": differs at frame " + std::to_string(i));
            }
        }
        std::cout << "PASS match_elements: 32 kHz output equals Part driven directly, bit for bit (3 models)\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL match_elements: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
