// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "ports.hpp"

#include <array>
#include <cstdint>

namespace elements {
struct Patch;
struct PerformanceState;
} // namespace elements

namespace chamber {

using PortArray = std::array<float*, kPortCount>;

// Turns control ports and CV inputs into elements::Patch and
// elements::PerformanceState, once per 16-frame block at 32 kHz, as the
// hardware's CvScaler::Read (cv_scaler.cc) does once per codec block.
class Controls {
public:
    void reset();
    // `frame` is the host frame whose CV values the block uses.
    void read(const PortArray& ports, uint32_t frame, elements::Patch* patch,
              elements::PerformanceState* state);

    // Pots in the hardware's order (drivers/pots_adc.h).
    enum Pot : uint32_t {
        envelope_shape, bow_level, bow_timbre, bow_timbre_att, blow_level, blow_meta,
        blow_meta_att, blow_timbre, blow_timbre_att, strike_level, strike_meta,
        strike_meta_att, strike_timbre, strike_timbre_att, coarse, fine, fm_att,
        geometry, geometry_att, brightness, brightness_att, damping, damping_att,
        position, position_att, space, space_att, pot_count
    };

private:
    void read_pots(const PortArray& ports);

    std::array<float, pot_count> pot_raw_{};
    std::array<float, pot_count> pot_lp_{};
    float coarse_quantized_ = 0;
    float note_ = 0;
    float modulation_ = 0;
    bool gate_ = false;
    bool previous_gate_ = false;
    bool first_ = true;
};

} // namespace chamber
