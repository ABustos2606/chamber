// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once
#include "../src/ports.hpp"

#include <lv2/core/lv2.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace test {

void require(bool condition, const std::string& message);

class Library {
public:
    explicit Library(const char* path);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    const LV2_Descriptor* descriptor = nullptr;
    const LV2_Descriptor* (*get)(uint32_t) = nullptr;

private:
    void* library_;
};

struct Stereo {
    float l, r;
    bool operator==(const Stereo& o) const { return l == o.l && r == o.r; }
};

// One plugin instance, run period by period, each period cut into random
// slices as Ingen does at control changes.
class Instance {
public:
    static constexpr float kGuard = 12345.0f;
    Instance(const LV2_Descriptor* descriptor, double rate, uint32_t block = 128);
    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    float& control(chamber::Port port) { return controls_[static_cast<uint32_t>(port)]; }
    float& signal(chamber::Port port, uint32_t i) { return buffers_[static_cast<uint32_t>(port)][i]; }
    void activate();
    // The Turtle defaults instead of Elements' panel values (host.cpp).
    void plugin_defaults();

    // Called for every frame of a period before it runs, to write audio and
    // CV inputs (and controls, which then hold for the whole period).
    using Input = std::function<void(Instance&, uint32_t index, uint64_t frame)>;
    std::vector<Stereo> render(uint64_t frames, const Input& input = {});

    const LV2_Descriptor* descriptor;
    LV2_Handle handle = nullptr;
    double rate;
    uint32_t block;
    uint64_t frame = 0;
    bool random_slices = true;
    bool measure = false;
    std::vector<double> run_times; // microseconds per period, when measuring

private:
    void period();
    std::array<float, chamber::kPortCount> controls_{};
    std::array<std::vector<float>, chamber::kPortCount> buffers_;
    uint32_t rng_ = 0x6A09E667;
};

} // namespace test
