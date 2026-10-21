// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "host.hpp"

#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <stdexcept>

namespace test {

void require(bool condition, const std::string& message)
{
    if (!condition) { throw std::runtime_error(message); }
}

Library::Library(const char* path) : library_(dlopen(path, RTLD_NOW | RTLD_LOCAL))
{
    if (!library_) { throw std::runtime_error(dlerror()); }
    get = reinterpret_cast<const LV2_Descriptor* (*)(uint32_t)>(dlsym(library_, "lv2_descriptor"));
    require(get, "lv2_descriptor export");
    descriptor = get(0);
    require(descriptor, "descriptor 0");
}

Library::~Library() { dlclose(library_); }

Instance::Instance(const LV2_Descriptor* d, double sample_rate, uint32_t size)
    : descriptor(d), rate(sample_rate), block(size)
{
    const LV2_Feature* features[] = {nullptr};
    handle = d->instantiate(d, rate, ".", features);
    require(handle, "instantiate at " + std::to_string(rate) + " Hz");
    for (auto& buffer : buffers_) { buffer.assign(size + 1, 0.0f); }
    // Elements' panel values (Part::Init), which the voice tests are written
    // against; every other control is 0. plugin_defaults() moves them to the
    // Turtle defaults (tests/ttl_check.py checks the two lists together).
    // One assignment per line: ttl_check reads them.
    using chamber::Port;
    // elements-panel-begin
    control(Port::contour) = 1.0f;
    control(Port::bow_timbre) = 0.5f;
    control(Port::blow_meta) = 0.5f;
    control(Port::blow_timbre) = 0.5f;
    control(Port::strike_level) = 0.8f;
    control(Port::strike_meta) = 0.5f;
    control(Port::strike_timbre) = 0.5f;
    control(Port::geometry) = 0.2f;
    control(Port::brightness) = 0.5f;
    control(Port::damping) = 0.25f;
    control(Port::position) = 0.3f;
    control(Port::coarse) = 30.0f;
    control(Port::space) = 0.5f;
    control(Port::strength) = 0.5f;
    // elements-panel-end
    activate();
}

// What a newly added Chamber starts with since v0.2.0: a long bell with
// reverb that a guitar at Strike in plays, every exciter off.
void Instance::plugin_defaults()
{
    using chamber::Port;
    // plugin-defaults-begin
    control(Port::strike_level) = 0.0f;
    control(Port::coarse) = 33.0f;
    control(Port::geometry) = 0.72f;
    control(Port::brightness) = 0.68f;
    control(Port::damping) = 0.82f;
    control(Port::space) = 0.88f;
    control(Port::strike_in_level) = -9.0f;
    // plugin-defaults-end
}

Instance::~Instance()
{
    if (descriptor->deactivate) { descriptor->deactivate(handle); }
    descriptor->cleanup(handle);
}

void Instance::activate()
{
    if (descriptor->activate) { descriptor->activate(handle); }
}

void Instance::period()
{
    for (uint32_t p = 0; p < chamber::kPortCount; ++p) { buffers_[p][block] = kGuard; }
    uint32_t offset = 0;
    double duration = 0;
    while (offset < block) {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        const uint32_t count = random_slices ? std::min(1 + rng_ % 97, block - offset) : block;
        for (uint32_t p = 0; p < chamber::kPortCount; ++p) {
            descriptor->connect_port(handle, p,
                chamber::is_signal(p) ? buffers_[p].data() + offset : &controls_[p]);
        }
        const auto start = std::chrono::steady_clock::now();
        descriptor->run(handle, count);
        duration += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        offset += count;
    }
    if (measure) { run_times.push_back(duration); }
    for (uint32_t p = 0; p < chamber::kPortCount; ++p) {
        if (buffers_[p][block] != kGuard) {
            throw std::runtime_error("write past the end of a period, port " + std::to_string(p));
        }
    }
}

std::vector<Stereo> Instance::render(uint64_t frames, const Input& input)
{
    using chamber::Port;
    std::vector<Stereo> output;
    output.reserve(frames);
    while (output.size() < frames) {
        if (input) {
            for (uint32_t i = 0; i < block; ++i) { input(*this, i, frame + i); }
        }
        period();
        for (uint32_t i = 0; i < block && output.size() < frames; ++i) {
            output.push_back({signal(Port::out_l, i), signal(Port::out_r, i)});
        }
        frame += block;
    }
    return output;
}

} // namespace test
