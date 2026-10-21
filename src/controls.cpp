// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "controls.hpp"

#include "elements/dsp/part.h"
#include "elements/dsp/patch.h"

#include <algorithm>
#include <cmath>

namespace chamber {
namespace {

// cv_scaler.cc: CvScaler::law_.
enum Law { linear, quadratic_bipolar, quartic_bipolar, quantized_note };

struct PotSource {
    Port port;
    Law law;
    float fallback; // used when the port is unconnected or not finite
};

constexpr std::array<PotSource, Controls::pot_count> kPots{{
    {Port::contour, linear, 1.0f},
    {Port::bow_level, linear, 0.0f},
    {Port::bow_timbre, linear, 0.5f},
    {Port::bow_timbre_amount, quadratic_bipolar, 0.0f},
    {Port::blow_level, linear, 0.0f},
    {Port::blow_meta, linear, 0.5f},
    {Port::blow_meta_amount, quadratic_bipolar, 0.0f},
    {Port::blow_timbre, linear, 0.5f},
    {Port::blow_timbre_amount, quadratic_bipolar, 0.0f},
    {Port::strike_level, linear, 0.0f},
    {Port::strike_meta, linear, 0.5f},
    {Port::strike_meta_amount, quadratic_bipolar, 0.0f},
    {Port::strike_timbre, linear, 0.5f},
    {Port::strike_timbre_amount, quadratic_bipolar, 0.0f},
    {Port::coarse, quantized_note, 33.0f},
    {Port::fine, quadratic_bipolar, 0.0f},
    {Port::fm_amount, quartic_bipolar, 0.0f},
    {Port::geometry, linear, 0.72f},
    {Port::geometry_amount, quadratic_bipolar, 0.0f},
    {Port::brightness, linear, 0.68f},
    {Port::brightness_amount, quadratic_bipolar, 0.0f},
    {Port::damping, linear, 0.82f},
    {Port::damping_amount, quadratic_bipolar, 0.0f},
    {Port::position, linear, 0.3f},
    {Port::position_amount, quadratic_bipolar, 0.0f},
    {Port::space, linear, 0.88f},
    {Port::space_amount, quadratic_bipolar, 0.0f},
}};

// Peepo's CV scale is 1.0 = 5 V. Elements' attenuverted CV inputs read
// (offset - adc), where the 0 V offset is 10 V * 20k / 120k / 3.3 V
// (CvScaler::Init); the same divider gives 1 V = 1 / 19.8 of the ADC range.
// Inferred from that constant, not from the schematic: **tune**.
constexpr float kCvScale = 5.0f * 20.0f / 120.0f / 3.3f;
// Peepo's gates are 0 or 1; Poly's Rings port uses the same threshold.
constexpr float kGateThreshold = 0.4f;

float value(const PortArray& ports, Port port, float fallback, float minimum, float maximum)
{
    const float* data = ports[static_cast<uint32_t>(port)];
    return data && std::isfinite(*data) ? std::clamp(*data, minimum, maximum) : fallback;
}

float cv(const PortArray& ports, Port port, uint32_t frame)
{
    const float* data = ports[static_cast<uint32_t>(port)];
    return data && std::isfinite(data[frame]) ? std::clamp(data[frame], -2.0f, 2.0f) : 0.0f;
}

template <typename T> void constrain(T& x, T lo, T hi) { x = std::min(std::max(x, lo), hi); }

} // namespace

void Controls::reset()
{
    pot_raw_.fill(0); pot_lp_.fill(0);
    coarse_quantized_ = note_ = modulation_ = 0;
    gate_ = previous_gate_ = false;
    first_ = true;
}

// CvScaler::ReadPanelPots, with every pot read on every call instead of one
// per call (the knobs here are exact, not scanned).
void Controls::read_pots(const PortArray& ports)
{
    for (uint32_t i = 0; i < pot_count; ++i) {
        const auto& pot = kPots[i];
        switch (pot.law) {
        case linear:
            pot_raw_[i] = value(ports, pot.port, pot.fallback, 0.0f, 1.0f);
            break;
        case quadratic_bipolar: {
            const float x = value(ports, pot.port, pot.fallback, -1.0f, 1.0f);
            const float x2 = x * x * 3.3f;
            pot_raw_[i] = x < 0.0f ? -x2 : x2;
            break;
        }
        case quartic_bipolar: {
            const float x = value(ports, pot.port, pot.fallback, -1.0f, 1.0f);
            const float x2 = x * x;
            const float x4 = x2 * x2 * 3.3f;
            pot_raw_[i] = x < 0.0f ? -x4 : x4;
            break;
        }
        case quantized_note: {
            // 5 octaves.
            const float note = value(ports, pot.port, pot.fallback, 0.0f, 60.0f);
            if (first_) { pot_raw_[i] = note; }
            pot_raw_[i] += 0.5f * (note - pot_raw_[i]);
            const float n = pot_raw_[i];
            const float hysteresis = n - coarse_quantized_ > 0.0f ? -0.3f : +0.3f;
            coarse_quantized_ = static_cast<float>(static_cast<int32_t>(n + hysteresis + 0.5f));
            break;
        }
        }
    }
    // Low-pass filter all front-panel pots. The hardware starts them from 0 at
    // power-on; a plugin starts them where the patch has them.
    for (uint32_t i = 0; i < pot_count; ++i) {
        if (first_) { pot_lp_[i] = pot_raw_[i]; }
        pot_lp_[i] += 0.01f * (pot_raw_[i] - pot_lp_[i]);
    }
}

// CvScaler::Read. The hardware's calibration offsets are dropped: a CV of 0
// here is 0 V.
void Controls::read(const PortArray& ports, uint32_t frame, elements::Patch* patch,
                    elements::PerformanceState* state)
{
    read_pots(ports);

    patch->exciter_envelope_shape = pot_lp_[envelope_shape];
    patch->exciter_bow_level = pot_lp_[bow_level];
    patch->exciter_blow_level = pot_lp_[blow_level];
    patch->exciter_strike_level = pot_lp_[strike_level];

    const auto bind = [&](float& destination, Pot pot, Pot attenuverter, Port input,
                          float lp_coefficient, float min, float max) {
        float v = pot_lp_[pot] + pot_lp_[attenuverter] * kCvScale * cv(ports, input, frame);
        constrain(v, min, max);
        if (lp_coefficient != 1.0f && !first_) {
            destination += (v - destination) * lp_coefficient;
        } else {
            destination = v;
        }
    };
    bind(patch->exciter_bow_timbre, bow_timbre, bow_timbre_att, Port::bow_timbre_cv, 1.0f, 0.0f, 0.9995f);
    bind(patch->exciter_blow_meta, blow_meta, blow_meta_att, Port::blow_meta_cv, 0.05f, 0.0f, 0.9995f);
    bind(patch->exciter_blow_timbre, blow_timbre, blow_timbre_att, Port::blow_timbre_cv, 1.0f, 0.0f, 0.9995f);
    bind(patch->exciter_strike_meta, strike_meta, strike_meta_att, Port::strike_meta_cv, 0.05f, 0.0f, 0.9995f);
    bind(patch->exciter_strike_timbre, strike_timbre, strike_timbre_att, Port::strike_timbre_cv, 1.0f, 0.0f, 0.995f);
    bind(patch->resonator_geometry, geometry, geometry_att, Port::geometry_cv, 0.05f, 0.0f, 0.9995f);
    bind(patch->resonator_brightness, brightness, brightness_att, Port::brightness_cv, 1.0f, 0.0f, 0.9995f);
    bind(patch->resonator_damping, damping, damping_att, Port::damping_cv, 1.0f, 0.0f, 0.9995f);
    bind(patch->resonator_position, position, position_att, Port::position_cv, 0.01f, 0.0f, 0.9995f);
    bind(patch->space, space, space_att, Port::space_cv, 0.01f, 0.0f, 2.0f);

    // V/oct: 1 V per octave, 1.0 = 5 V.
    const float note = 60.0f * cv(ports, Port::voct_cv, frame);
    const float interval = note - note_;
    // When a pitch difference of more than 1 quartertone is observed, do
    // not attempt to slew-limit and jump straight to the right pitch.
    if (interval < -0.4f || interval > 0.4f) {
        note_ = note;
    } else {
        note_ += 0.1f * interval;
    }

    const float modulation = pot_lp_[fm_att] * 49.5f * kCvScale * cv(ports, Port::fm_cv, frame);
    if (first_) { modulation_ = modulation; }
    modulation_ += 0.5f * (modulation - modulation_);
    state->modulation = modulation_;

    state->note = note_;
    state->note += coarse_quantized_ + 19.0f;
    state->note += pot_lp_[fine] * (2.0f / 3.3f);
    // The hardware reads Strength from its CV input alone (normalled to about
    // half); a plugin can't tell an unpatched input, so knob plus CV.
    state->strength = value(ports, Port::strength, 0.5f, 0.0f, 1.0f) + cv(ports, Port::strength_cv, frame);

    constrain(state->strength, 0.0f, 1.0f);
    constrain(state->modulation, -60.0f, 60.0f);

    // Scan the gate input 1ms after the other CVs to prevent lag.
    state->gate = previous_gate_;
    previous_gate_ = gate_;
    gate_ = cv(ports, Port::gate_cv, frame) >= kGateThreshold;
    first_ = false;
}

} // namespace chamber
