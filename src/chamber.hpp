// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "block_buffer.hpp"
#include "controls.hpp"
#include "ports.hpp"

#include <lv2/core/lv2.h>

#include <cstdint>

namespace elements {
class Part;
}

namespace chamber {

extern const LV2_Descriptor descriptor;

// Seed for Part::Seed. The hardware uses the chip's unique ID; a fixed value
// makes every instance and render sound the same.
inline constexpr uint32_t kSeed[3] = {0x6368616d, 0x62657221, 0x00000001};
// stmlib::Random's initial state upstream (stmlib/utils/random.cc).
inline constexpr uint32_t kRandomSeed = 0x21;
// elements.cc: reverb_buffer.
inline constexpr uint32_t kReverbSize = 32768;

class Chamber {
public:
    static Chamber* create(double rate) noexcept;
    ~Chamber();
    Chamber(const Chamber&) = delete;
    Chamber& operator=(const Chamber&) = delete;

    void connect(uint32_t index, void* data) noexcept;
    void activate() noexcept;
    void run(uint32_t frames) noexcept;

private:
    explicit Chamber(double rate) noexcept;
    float control(Port port, float fallback, float minimum, float maximum) const noexcept;
    float input(Port port, uint32_t frame) const noexcept;
    void process_block(uint32_t frame) noexcept;

    double rate_;
    float smoothing_;
    PortArray ports_{};
    elements::Part* part_ = nullptr;
    uint16_t* reverb_ = nullptr;
    Controls controls_;
    BlockBuffer block_{};
    uint32_t random_state_ = kRandomSeed;
    int model_ = -1;
    int ominous_ = -1;
    // elements.cc: FillBuffer's input level followers.
    float blow_in_follower_ = 0;
    float strike_in_follower_ = 0;
    float blow_in_gain_ = 1, strike_in_gain_ = 1, output_gain_ = 1;
    bool first_ = true;
};

} // namespace chamber
