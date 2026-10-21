// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// controls: each knob and CV moves its own Patch or PerformanceState field
// and nothing else. pitch: V/oct steps are octaves. rate equivalence: a
// 48 kHz host plays exactly Elements' own 32 kHz output, resampled.
#include "host.hpp"
#include "../src/controls.hpp"
#include "../src/rate_bridge.hpp"

#include "elements/dsp/part.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <vector>

using chamber::Port;
using test::Instance;
using test::require;

namespace {

// --- controls ---------------------------------------------------------------

const char* const kFields[] = {
    "envelope_shape", "bow_level", "bow_timbre", "blow_level", "blow_meta", "blow_timbre",
    "strike_level", "strike_meta", "strike_timbre", "signature", "geometry", "brightness",
    "damping", "position", "modulation_frequency", "modulation_offset", "reverb_diffusion",
    "reverb_lp", "space", "patch_modulation_frequency", "note", "modulation", "strength", "gate",
};
static_assert(sizeof(elements::Patch) == 20 * sizeof(float), "Patch is 20 floats");

struct Rig {
    std::array<float, chamber::kPortCount> controls{};
    std::array<float, chamber::kPortCount> cvs{};
    chamber::PortArray ports{};
    Rig()
    {
        for (uint32_t p = 0; p < chamber::kPortCount; ++p) {
            ports[p] = chamber::is_signal(p) ? &cvs[p] : &controls[p];
        }
        set(Port::contour, 1); set(Port::bow_timbre, 0.5f); set(Port::blow_meta, 0.5f);
        set(Port::blow_timbre, 0.5f); set(Port::strike_level, 0.8f); set(Port::strike_meta, 0.5f);
        set(Port::strike_timbre, 0.5f); set(Port::geometry, 0.2f); set(Port::brightness, 0.5f);
        set(Port::damping, 0.25f); set(Port::position, 0.3f); set(Port::coarse, 30);
        set(Port::space, 0.5f); set(Port::strength, 0.5f);
    }
    void set(Port p, float v) { *ports[static_cast<uint32_t>(p)] = v; }
    // Fields after the filters settle.
    std::vector<float> read()
    {
        chamber::Controls controls_;
        controls_.reset();
        elements::Patch patch{};
        elements::PerformanceState state{};
        for (int i = 0; i < 4000; ++i) { controls_.read(ports, 0, &patch, &state); }
        std::vector<float> fields(20);
        std::memcpy(fields.data(), &patch, sizeof(patch));
        fields.push_back(state.note);
        fields.push_back(state.modulation);
        fields.push_back(state.strength);
        fields.push_back(state.gate ? 1.0f : 0.0f);
        return fields;
    }
};

std::set<std::string> changed(const std::vector<float>& a, const std::vector<float>& b)
{
    std::set<std::string> names;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > 1e-4f) { names.insert(kFields[i]); }
    }
    return names;
}

void test_controls()
{
    const auto base = Rig().read();
    struct Case {
        std::vector<std::pair<Port, float>> settings;
        std::set<std::string> fields;
    };
    const std::vector<Case> cases = {
        {{{Port::contour, 0.3f}}, {"envelope_shape"}},
        {{{Port::bow_level, 0.7f}}, {"bow_level"}},
        {{{Port::bow_timbre, 0.9f}}, {"bow_timbre"}},
        {{{Port::blow_level, 0.7f}}, {"blow_level"}},
        {{{Port::blow_meta, 0.9f}}, {"blow_meta"}},
        {{{Port::blow_timbre, 0.9f}}, {"blow_timbre"}},
        {{{Port::strike_level, 0.2f}}, {"strike_level"}},
        {{{Port::strike_meta, 0.9f}}, {"strike_meta"}},
        {{{Port::strike_timbre, 0.9f}}, {"strike_timbre"}},
        {{{Port::geometry, 0.9f}}, {"geometry"}},
        {{{Port::brightness, 0.9f}}, {"brightness"}},
        {{{Port::damping, 0.9f}}, {"damping"}},
        {{{Port::position, 0.9f}}, {"position"}},
        {{{Port::space, 0.9f}}, {"space"}},
        {{{Port::coarse, 42}}, {"note"}},
        {{{Port::fine, 0.5f}}, {"note"}},
        {{{Port::strength, 0.9f}}, {"strength"}},
        {{{Port::voct_cv, 0.2f}}, {"note"}},
        {{{Port::strength_cv, 0.3f}}, {"strength"}},
        {{{Port::gate_cv, 1.0f}}, {"gate"}},
        {{{Port::fm_cv, 0.5f}}, {}},
        {{{Port::fm_amount, 1.0f}}, {}},
        {{{Port::fm_amount, 1.0f}, {Port::fm_cv, 0.5f}}, {"modulation"}},
        {{{Port::bow_timbre_amount, 0.5f}, {Port::bow_timbre_cv, 0.4f}}, {"bow_timbre"}},
        {{{Port::blow_meta_amount, 0.5f}, {Port::blow_meta_cv, 0.4f}}, {"blow_meta"}},
        {{{Port::blow_timbre_amount, 0.5f}, {Port::blow_timbre_cv, 0.4f}}, {"blow_timbre"}},
        {{{Port::strike_meta_amount, 0.5f}, {Port::strike_meta_cv, 0.4f}}, {"strike_meta"}},
        {{{Port::strike_timbre_amount, 0.5f}, {Port::strike_timbre_cv, 0.4f}}, {"strike_timbre"}},
        {{{Port::geometry_amount, 0.5f}, {Port::geometry_cv, 0.4f}}, {"geometry"}},
        {{{Port::brightness_amount, 0.5f}, {Port::brightness_cv, 0.4f}}, {"brightness"}},
        {{{Port::damping_amount, 0.5f}, {Port::damping_cv, 0.4f}}, {"damping"}},
        {{{Port::position_amount, 0.5f}, {Port::position_cv, 0.4f}}, {"position"}},
        {{{Port::space_amount, 0.5f}, {Port::space_cv, 0.4f}}, {"space"}},
        // An amount without CV, or CV without an amount, changes nothing.
        {{{Port::geometry_amount, 1.0f}}, {}},
        {{{Port::geometry_cv, 1.0f}}, {}},
    };
    for (const auto& c : cases) {
        Rig rig;
        std::string what;
        for (const auto& [port, value] : c.settings) { rig.set(port, value); what += std::to_string(static_cast<int>(port)) + " "; }
        const auto got = changed(base, rig.read());
        std::string list;
        for (const auto& name : got) { list += name + " "; }
        require(got == c.fields, "ports " + what + "changed: " + list);
    }
    // Hardware ranges: knob plus CV is clamped where BIND clamps it.
    Rig rig;
    rig.set(Port::space, 1); rig.set(Port::space_amount, 1); rig.set(Port::space_cv, 2);
    require(std::fabs(rig.read()[18] - 2.0f) < 1e-4f, "Space with CV reaches 2 (freeze above 1.75)");
    std::cout << "PASS controls: " << cases.size() << " cases, each moves only its own field\n";
}

// --- pitch -------------------------------------------------------------------

// Fundamental: the strongest Hann-windowed spectral peak within 60 cents of
// the expected note, by a fine Goertzel-style scan and parabolic refinement.
// The string model drifts slightly by design (its seeded modulation), so
// this measures the average.
double fundamental(const std::vector<float>& x, double rate, double expected)
{
    const size_t n = x.size();
    auto power = [&](double f) {
        const double w = 2.0 * M_PI * f / rate;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
            re += hann * x[i] * std::cos(w * i);
            im -= hann * x[i] * std::sin(w * i);
        }
        return re * re + im * im;
    };
    double best = expected, best_power = -1;
    for (double c = -60; c <= 60; c += 0.5) {
        const double f = expected * std::pow(2.0, c / 1200.0);
        const double p = power(f);
        if (p > best_power) { best_power = p; best = f; }
    }
    const double step = std::pow(2.0, 0.5 / 1200.0);
    const double a = power(best / step), b = best_power, d = power(best * step);
    const double offset = 0.5 * (a - d) / (a - 2 * b + d);  // in half-cent steps
    return best * std::pow(2.0, offset * 0.5 / 1200.0);
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

std::vector<test::Stereo> render_note(const LV2_Descriptor* d, double rate, float voct, float model, double seconds)
{
    Instance instance(d, rate, 128);
    instance.control(Port::model) = model;
    instance.control(Port::coarse) = 36;
    instance.control(Port::damping) = 0.15f;
    instance.control(Port::space) = 0;      // no reverb, no stereo spread
    return instance.render(static_cast<uint64_t>(rate * seconds), [&](Instance& in, uint32_t i, uint64_t) {
        in.signal(Port::voct_cv, i) = voct;
        in.signal(Port::gate_cv, i) = 1;
    });
}

void test_pitch(const LV2_Descriptor* d)
{
    // The estimator itself, on decaying harmonic tones of known pitch.
    for (double f : {49.0, 196.0, 785.25}) {
        std::vector<float> x(static_cast<size_t>(32000 * 1.0));
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = i / 32000.0;
            x[i] = static_cast<float>(std::exp(-2 * t) * (std::sin(2 * M_PI * f * t) + 0.5 * std::sin(4 * M_PI * f * t + 1)));
        }
        const double error = cents(fundamental(x, 32000, f * 1.01), f);
        require(std::fabs(error) < 0.1, "pitch estimator within 0.1 cent: " + std::to_string(error));
    }
    // The note handed to Elements: exactly 12 semitones per 0.2 (1 V).
    for (int step = -2; step <= 2; ++step) {
        Rig rig;
        rig.set(Port::coarse, 36);
        rig.set(Port::voct_cv, 0.2f * step);
        const float note = rig.read()[20];
        require(note == 36 + 19 + 12.0f * step, "V/oct note " + std::to_string(note));
    }
    // What Elements makes of it. Its resonator's fast frequency
    // approximations leave a few cents, kept as upstream wrote them.
    double worst = 0;
    for (int step = -2; step <= 2; ++step) {
        // MIDI note: coarse + 19 + 12 per volt.
        const double expected = 440.0 * std::pow(2.0, (36 + 19 + 12 * step - 69) / 12.0);
        const auto out = render_note(d, 32000, 0.2f * step, 0, 1.2);
        std::vector<float> right;
        for (size_t i = 6400; i < out.size(); ++i) { right.push_back(out[i].r); }
        const double f = fundamental(right, 32000, expected);
        std::printf("pitch V/oct %+.1f: %.3f Hz, note %.2f Hz (%+.2f cents)\n", 0.2f * step, f, expected, cents(f, expected));
        worst = std::max(worst, std::fabs(cents(f, expected)));
    }
    require(worst < 5.0, "modal pitch within 5 cents of the note");
    std::cout << "PASS pitch: V/oct exact in the note; modal pitch within " << worst << " cents of it over 5 octaves\n";
}

// With silent audio inputs and steady CVs the voice inside a 48 kHz host gets
// exactly what it gets at 32 kHz, so the 48 kHz output must be the 32 kHz
// output through the same upsampler: same pitch, envelopes and timing.
void test_rate_equivalence(const LV2_Descriptor* d)
{
    for (float model : {0.0f, 1.0f, 2.0f}) {
        const auto voice = render_note(d, 32000, 0.1f, model, 2.0);
        const auto host = render_note(d, 48000, 0.1f, model, 2.0);
        // The 32 kHz render through a 32 -> 48 kHz resampler with the
        // plugin's upsampler delay. Only audio inputs pass the downsampler;
        // a note struck by a gate leaves through the upsampler alone.
        chamber::Resampler<2> up;
        require(up.configure(32000, 48000), "up");
        uint32_t delay = up.delay();
        while ((static_cast<uint64_t>(delay + chamber::BlockBuffer::kSize) * 48000) % 32000) { ++delay; }
        require(up.configure(32000, 48000, delay), "up delay");
        std::vector<test::Stereo> expected;
        size_t k = 0;
        while (expected.size() < host.size()) {
            while (!up.ready()) {
                const float in[2] = {voice[k].l, voice[k].r};
                up.push(in);
                ++k;
            }
            float y[2];
            up.next(y);
            expected.push_back({y[0], y[1]});
        }
        double worst = 0, peak = 0;
        for (size_t i = 0; i < host.size(); ++i) {
            worst = std::max({worst, static_cast<double>(std::fabs(host[i].l - expected[i].l)),
                              static_cast<double>(std::fabs(host[i].r - expected[i].r))});
            peak = std::max({peak, static_cast<double>(std::fabs(host[i].l)), static_cast<double>(std::fabs(host[i].r))});
        }
        require(peak > 0.01, "rate equivalence test makes sound");
        require(worst <= 1e-6 * peak, "48 kHz output is the 32 kHz voice upsampled, model " +
                std::to_string(model) + ": error " + std::to_string(worst));
    }
    std::cout << "PASS rate equivalence: at 48 kHz each model is Elements' 32 kHz output, upsampled\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: test_voice chamber.so\n"; return 1; }
    try {
        test_controls();
        test::Library library(argv[1]);
        test_pitch(library.descriptor);
        test_rate_equivalence(library.descriptor);
    } catch (const std::exception& e) {
        std::cerr << "FAIL voice: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
