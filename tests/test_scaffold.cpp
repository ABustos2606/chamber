// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "host.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

using chamber::Port;
using test::Instance;
using test::require;
using test::Stereo;

namespace {

float peak(const std::vector<Stereo>& audio, size_t from = 0, size_t to = SIZE_MAX)
{
    float result = 0;
    for (size_t i = from; i < std::min(to, audio.size()); ++i) {
        require(std::isfinite(audio[i].l) && std::isfinite(audio[i].r), "finite output");
        result = std::max({result, std::fabs(audio[i].l), std::fabs(audio[i].r)});
    }
    return result;
}

// A gate held for 0.2 s every second, a slow V/oct and Geometry CV, and noise
// at Strike in: exercises gate, CV, external input and the Random state.
void performance(Instance& instance, uint32_t i, uint64_t frame)
{
    const double t = static_cast<double>(frame) / instance.rate;
    instance.signal(Port::gate_cv, i) = std::fmod(t, 1.0) < 0.2 ? 1.0f : 0.0f;
    instance.signal(Port::voct_cv, i) = static_cast<float>(0.1 * std::sin(t * 1.3));
    instance.signal(Port::geometry_cv, i) = static_cast<float>(0.5 * std::sin(t * 0.7));
    uint32_t x = static_cast<uint32_t>(frame) * 2654435761u;
    x ^= x >> 15;
    instance.signal(Port::strike_in, i) = t > 2.5 ? static_cast<float>(x % 2001) / 10000.0f - 0.1f : 0.0f;
}

void blowing_string(Instance& instance)
{
    instance.control(Port::model) = 1;
    instance.control(Port::blow_level) = 0.8f;
    instance.control(Port::bow_level) = 0.5f;
}

void test_descriptor(const test::Library& library)
{
    require(std::strcmp(library.descriptor->URI, chamber::kURI) == 0, "URI");
    require(!library.get(1) && !library.get(UINT32_MAX), "one descriptor");
    const auto* d = library.descriptor;
    for (double rate : {0.0, -1.0, 100.0, std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity()}) {
        require(!d->instantiate(d, rate, ".", nullptr), "reject rate " + std::to_string(rate));
    }
    for (double rate : {32000.0, 44100.0, 48000.0, 96000.0}) {
        auto h = d->instantiate(d, rate, ".", nullptr);
        require(h, "instantiate at " + std::to_string(rate));
        // Unconnected ports and unknown indices must not crash.
        d->connect_port(h, 999, nullptr);
        d->run(h, 0);
        d->run(h, 128);
        d->activate(h);
        d->run(h, 37);
        d->cleanup(h);
    }
    std::cout << "PASS descriptor: one plugin, rates, unconnected ports, zero frames\n";
}

void test_play(const LV2_Descriptor* d)
{
    Instance instance(d, 32000);
    const auto silent = instance.render(16000);
    require(peak(silent) == 0.0f, "silent until played");
    instance.control(Port::play) = 1;
    const auto struck = instance.render(6400);
    instance.control(Port::play) = 0;
    const auto tail = instance.render(32000);
    require(peak(struck) > 0.05f, "Play strikes a note: peak " + std::to_string(peak(struck)));
    require(peak(tail, 28000) < peak(struck) * 0.5f, "the note decays");

    Instance gated(d, 32000);
    const auto by_gate = gated.render(6400, [](Instance& in, uint32_t i, uint64_t) {
        in.signal(Port::gate_cv, i) = 1.0f;
    });
    require(peak(by_gate) > 0.05f, "the gate input strikes a note");
    const float latency = gated.control(Port::latency);
    require(latency == 16.0f, "latency port reports one block");
    std::cout << "PASS play: silence, Play and Gate strike a decaying note, latency " << latency << '\n';
}

void test_slicing(const LV2_Descriptor* d)
{
    for (double rate : {32000.0, 48000.0}) {
        Instance whole(d, rate, 128), sliced(d, rate, 128), small(d, rate, 37);
        whole.random_slices = false;
        for (auto* instance : {&whole, &sliced, &small}) { blowing_string(*instance); }
        const auto a = whole.render(4 * 32000, performance);
        const auto b = sliced.render(4 * 32000, performance);
        const auto c = small.render(4 * 32000, performance);
        require(peak(a) > 0.01f, "slicing test makes sound");
        require(a == b && a == c, "same output for any slicing at " + std::to_string(rate));
    }
    std::cout << "PASS slicing: identical output for whole periods, random slices, odd period length\n";
}

void test_instances(const LV2_Descriptor* d)
{
    // Exciter and string noise draw from stmlib::Random: every instance keeps
    // its own sequence whether alone, interleaved on one thread or on two.
    Instance alone(d, 48000);
    blowing_string(alone);
    const auto reference = alone.render(3 * 48000, performance);

    Instance a(d, 48000), b(d, 48000);
    blowing_string(a); blowing_string(b);
    std::vector<Stereo> out_a, out_b;
    for (int i = 0; i < 3 * 48000 / 1024 + 1; ++i) {
        for (auto& f : a.render(1024, performance)) { out_a.push_back(f); }
        for (auto& f : b.render(1024, performance)) { out_b.push_back(f); }
    }
    out_a.resize(reference.size()); out_b.resize(reference.size());
    require(out_a == reference && out_b == reference, "interleaved instances on one thread");

    Instance t1(d, 48000), t2(d, 48000);
    blowing_string(t1); blowing_string(t2);
    std::vector<Stereo> r1, r2;
    std::thread first([&] { r1 = t1.render(3 * 48000, performance); });
    std::thread second([&] { r2 = t2.render(3 * 48000, performance); });
    first.join(); second.join();
    require(r1 == reference && r2 == reference, "instances on two threads");

    alone.activate();
    alone.frame = 0;
    require(alone.render(3 * 48000, performance) == reference, "activate restarts the voice");
    std::cout << "PASS instances: own Random sequence alone, interleaved and threaded; activate resets\n";
}

void test_models_and_inputs(const LV2_Descriptor* d)
{
    std::vector<Stereo> renders[4];
    for (int i = 0; i < 4; ++i) {
        Instance instance(d, 32000);
        instance.control(Port::model) = static_cast<float>(i % 3);
        instance.control(Port::ominous) = i == 3 ? 1.0f : 0.0f;
        renders[i] = instance.render(64000, performance);
        require(peak(renders[i]) > 0.01f, "model " + std::to_string(i) + " sounds");
    }
    require(!(renders[0] == renders[1]) && !(renders[1] == renders[2]) && !(renders[0] == renders[3]),
            "models and the Ominous voice differ");

    Instance instance(d, 32000);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (Port p : {Port::geometry, Port::coarse, Port::model, Port::output}) { instance.control(p) = nan; }
    const auto out = instance.render(32000, [&](Instance& in, uint32_t i, uint64_t frame) {
        performance(in, i, frame);
        in.signal(Port::strike_in, i) = frame % 3 ? nan : inf;
        in.signal(Port::blow_in, i) = 1e30f;
        in.signal(Port::voct_cv, i) = frame % 2 ? -inf : nan;
        in.signal(Port::fm_cv, i) = 1e30f;
    });
    peak(out);
    std::cout << "PASS models: three models and the Ominous voice sound and differ; non-finite input stays finite\n";
}

void test_timing(const LV2_Descriptor* d)
{
    // Worst case: 64 modes, the Strings model, full reverb, every exciter on.
    Instance instance(d, 48000, 128);
    instance.measure = true;
    instance.control(Port::model) = 2;
    instance.control(Port::space) = 1;
    instance.control(Port::bow_level) = instance.control(Port::blow_level) = 1;
    const double seconds = 60;
    instance.render(static_cast<uint64_t>(seconds * 48000), [](Instance& in, uint32_t i, uint64_t frame) {
        performance(in, i, frame);
        uint32_t x = static_cast<uint32_t>(frame / 4800) * 2246822519u;
        in.control(Port::geometry) = static_cast<float>(x % 1000) / 1000.0f;
        in.signal(Port::space_cv, i) = 1;
    });
    auto times = instance.run_times;
    std::sort(times.begin(), times.end());
    const double period_us = 128 / 48000.0 * 1e6;
    double total = 0;
    for (double t : times) { total += t; }
    std::cout << "TIMING 48 kHz, 128 frames (" << period_us << " us): mean " << total / times.size()
              << " us, 99.9% " << times[times.size() * 999 / 1000] << " us, worst " << times.back()
              << " us, load " << 100 * total / (seconds * 1e6) << "%\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: test_scaffold chamber.so [--timing]\n"; return 1; }
    try {
        test::Library library(argv[1]);
        if (argc > 2 && std::strcmp(argv[2], "--timing") == 0) {
            test_timing(library.descriptor);
            return 0;
        }
        test_descriptor(library);
        test_play(library.descriptor);
        test_slicing(library.descriptor);
        test_instances(library.descriptor);
        test_models_and_inputs(library.descriptor);
    } catch (const std::exception& e) {
        std::cerr << "FAIL scaffold: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
