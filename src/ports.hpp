// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include <cstdint>

namespace chamber {

inline constexpr char kURI[] = "https://github.com/ABustos2606/chamber#chamber";

// Permanent port ABI: never renumber or rename; new ports are appended.
// The kind comments are read by tests/ttl_check.py.
enum class Port : uint32_t {
    blow_in = 0,             // audio_in
    strike_in = 1,           // audio_in
    out_l = 2,               // audio_out
    out_r = 3,               // audio_out
    contour = 4,             // control_in
    bow_level = 5,           // control_in
    bow_timbre = 6,          // control_in
    blow_level = 7,          // control_in
    blow_meta = 8,           // control_in
    blow_timbre = 9,         // control_in
    strike_level = 10,       // control_in
    strike_meta = 11,        // control_in
    strike_timbre = 12,      // control_in
    geometry = 13,           // control_in
    brightness = 14,         // control_in
    damping = 15,            // control_in
    position = 16,           // control_in
    model = 17,              // control_in
    coarse = 18,             // control_in
    fine = 19,               // control_in
    fm_amount = 20,          // control_in
    space = 21,              // control_in
    play = 22,               // control_in
    strength = 23,           // control_in
    blow_in_level = 24,      // control_in
    strike_in_level = 25,    // control_in
    ominous = 26,            // control_in
    output = 27,             // control_in
    bow_timbre_amount = 28,  // control_in
    blow_meta_amount = 29,   // control_in
    blow_timbre_amount = 30, // control_in
    strike_meta_amount = 31, // control_in
    strike_timbre_amount = 32, // control_in
    geometry_amount = 33,    // control_in
    brightness_amount = 34,  // control_in
    damping_amount = 35,     // control_in
    position_amount = 36,    // control_in
    space_amount = 37,       // control_in
    voct_cv = 38,            // cv_in
    fm_cv = 39,              // cv_in
    strength_cv = 40,        // cv_in
    gate_cv = 41,            // cv_in
    bow_timbre_cv = 42,      // cv_in
    blow_meta_cv = 43,       // cv_in
    blow_timbre_cv = 44,     // cv_in
    strike_meta_cv = 45,     // cv_in
    strike_timbre_cv = 46,   // cv_in
    geometry_cv = 47,        // cv_in
    brightness_cv = 48,      // cv_in
    damping_cv = 49,         // cv_in
    position_cv = 50,        // cv_in
    space_cv = 51,           // cv_in
    latency = 52,            // control_out
    count = 53
};

inline constexpr uint32_t kPortCount = static_cast<uint32_t>(Port::count);

inline constexpr bool is_signal(uint32_t index)
{
    return index <= static_cast<uint32_t>(Port::out_r) ||
           (index >= static_cast<uint32_t>(Port::voct_cv) && index <= static_cast<uint32_t>(Port::space_cv));
}

} // namespace chamber
