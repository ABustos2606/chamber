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
#include <memory>
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

// --- external input ------------------------------------------------------------

// Power of x at frequency f (Hann window), as in fundamental().
double power_at(const std::vector<float>& x, double rate, double f)
{
    const size_t n = x.size();
    const double w = 2.0 * M_PI * f / rate;
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        re += hann * x[i] * std::cos(w * i);
        im -= hann * x[i] * std::sin(w * i);
    }
    return re * re + im * im;
}

double rms(const std::vector<test::Stereo>& x, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i) { sum += x[i].l * x[i].l + x[i].r * x[i].r; }
    return std::sqrt(sum / (2.0 * (to - from)));
}

// A plucked guitar note: harmonics of 110 Hz, picked sharply, decaying.
float pluck(double t, float amplitude)
{
    if (t < 0) { return 0.0f; }
    double y = 0;
    for (int h = 1; h <= 12; ++h) { y += std::sin(2 * M_PI * 110.0 * h * t + h) / h; }
    return static_cast<float>(amplitude * 0.5 * std::exp(-30 * t) * y);
}

struct External {
    Port into = Port::strike_in;
    float model = 0;
    float level_db = 0;
    float amplitude = 0.3f;
    bool gate = false;
    bool silent = false;
};

std::vector<test::Stereo> render_external(const LV2_Descriptor* d, const External& e)
{
    Instance instance(d, 48000, 128);
    instance.control(Port::model) = e.model;
    instance.control(Port::coarse) = 36;     // note 55, 196 Hz
    instance.control(Port::damping) = 0.7f;  // long ring (higher is longer)
    instance.control(Port::space) = 0;
    instance.control(Port::strike_level) = 0; // no internal exciter
    instance.control(Port::blow_in_level) = instance.control(Port::strike_in_level) = e.level_db;
    return instance.render(48000 * 3 / 2, [&](Instance& in, uint32_t i, uint64_t frame) {
        const double t = frame / 48000.0 - 0.1;
        in.signal(e.into, i) = e.silent ? 0.0f : pluck(t, e.amplitude);
        in.signal(Port::gate_cv, i) = e.gate ? 1.0f : 0.0f;
    });
}

void test_external_input(const LV2_Descriptor* d)
{
    const double note = 440.0 * std::pow(2.0, (36 + 19 - 69) / 12.0);
    // Nothing comes out without the input: the sound is the guitar's.
    {
        External e;
        e.silent = true;
        const auto out = render_external(d, e);
        require(rms(out, 0, out.size()) < 1e-6, "silent without input");
    }
    // Strike in goes straight into the resonator, no gate needed. Once the
    // pluck has died away the resonator still rings at its own note, not at
    // the guitar's 110 Hz.
    for (float model : {0.0f, 1.0f, 2.0f}) {
        External e;
        e.model = model;
        const auto out = render_external(d, e);
        std::vector<float> tail;
        for (size_t i = 48000; i < out.size(); ++i) { tail.push_back(out[i].r); }
        const double f = fundamental(tail, 48000, note);
        const double ring = power_at(tail, 48000, f);
        const double guitar = std::max(power_at(tail, 48000, 110.0), power_at(tail, 48000, 220.0));
        std::printf("external strike in, model %.0f: tail %.1f dB, rings at %.2f Hz (%+.2f cents), "
                    "%.1f dB over the guitar's notes\n", model, 20 * std::log10(rms(out, 48000, out.size())),
                    f, cents(f, note), 10 * std::log10(ring / guitar));
        require(rms(out, 48000, out.size()) > 1e-3, "strike in rings after the pluck");
        require(std::fabs(cents(f, note)) < 10, "strike in rings at the resonator's note");
        require(ring > 100 * guitar, "resonator's note 20 dB over the guitar's");
    }
    // Blow in passes the exciter envelope (voice.cc: blow_buffer_ * e): it
    // needs a gate or Play.
    {
        External e;
        e.into = Port::blow_in;
        const double closed = rms(render_external(d, e), 0, 48000 * 3 / 2);
        e.gate = true;
        const auto open = render_external(d, e);
        // The gate alone, with no exciter level, makes no sound.
        e.silent = true;
        const double gate_only = rms(render_external(d, e), 0, 48000 * 3 / 2);
        std::printf("external blow in: %.1f dB gated, %.1f dB without gate, gate alone %.1f dB\n",
                    20 * std::log10(rms(open, 0, open.size())), 20 * std::log10(closed + 1e-12),
                    20 * std::log10(gate_only + 1e-12));
        require(rms(open, 0, open.size()) > 1e-3, "blow in sounds with the gate held");
        require(closed < 1e-6 && gate_only < 1e-6, "blow in is silent without a gate");
    }
    // Input levels are gains: -12 dB in, 12 dB less out (the resonator is
    // linear at these levels; the soft limiter barely touches them).
    for (Port into : {Port::strike_in, Port::blow_in}) {
        External e;
        e.into = into;
        e.gate = into == Port::blow_in;
        e.amplitude = 0.1f;
        const double full = rms(render_external(d, e), 4800, 48000);
        e.level_db = -12;
        const double low = rms(render_external(d, e), 4800, 48000);
        const double change = 20 * std::log10(low / full);
        std::printf("external %s level -12 dB: output %+.2f dB\n", into == Port::strike_in ? "strike" : "blow", change);
        require(std::fabs(change + 12) < 0.5, "input level is a gain in dB");
    }
    std::cout << "PASS external input: Strike in rings each model at its note; Blow in needs a gate; levels in dB\n";
}

// --- model and voice switches --------------------------------------------------

// Largest second difference: a click shows as a spike far above a ringing
// note's.
double roughness(const std::vector<test::Stereo>& x, size_t from, size_t to)
{
    double worst = 0;
    for (size_t i = std::max<size_t>(from, 2); i < to; ++i) {
        worst = std::max({worst, std::fabs(x[i].l - 2.0 * x[i - 1].l + x[i - 2].l),
                          std::fabs(x[i].r - 2.0 * x[i - 1].r + x[i - 2].r)});
    }
    return worst;
}

void test_switches(const LV2_Descriptor* d)
{
    struct Switch {
        Port port;
        float from, to;
        const char* name;
    };
    for (const Switch& s : {Switch{Port::model, 0, 1, "modal to string"}, Switch{Port::model, 2, 0, "strings to modal"},
                            Switch{Port::ominous, 0, 1, "Ominous on"}, Switch{Port::ominous, 1, 0, "Ominous off"}}) {
        const uint64_t at = 48000;
        Instance instance(d, 48000, 128);
        instance.random_slices = false;
        instance.control(Port::coarse) = 36;
        instance.control(Port::damping) = 0.7f;
        instance.control(Port::space) = 0;
        instance.control(s.port) = s.from;
        const auto out = instance.render(2 * at, [&](Instance& in, uint32_t i, uint64_t frame) {
            // A note before the switch, and one after it for the new voice.
            const bool second = frame >= at + 12000 && frame < at + 16800;
            in.signal(Port::gate_cv, i) = frame < 4800 || second ? 1.0f : 0.0f;
            in.control(s.port) = frame < at ? s.from : s.to;
        });
        // The fade takes 8 ms (384 frames at 48 kHz) plus the latency; judge
        // the 50 ms after the change against the steady sound either side.
        const double before = roughness(out, at - 9600, at);
        const double during = roughness(out, at, at + 2400);
        const double after = roughness(out, at + 2400, at + 12000);
        std::printf("switch %s: roughness %.2g before, %.2g at the switch, %.2g after\n", s.name, before, during, after);
        require(during <= 2 * std::max(before, after), std::string("no click: ") + s.name);
        require(rms(out, at + 14400, at + 24000) > 1e-3, std::string("new voice plays: ") + s.name);
    }
    std::cout << "PASS switches: model and Ominous changes fade, no click\n";
}

// --- defaults ------------------------------------------------------------------

// A newly added Chamber is a guitar effect (v0.2.0): every exciter is off, so
// it is silent until something plays it; Play alone stays silent; a guitar at
// Strike in rings it as a long bell with reverb, without clipping.
void test_defaults(const LV2_Descriptor* d)
{
    auto fresh = [&] {
        auto instance = std::make_unique<Instance>(d, 48000, 128);
        instance->plugin_defaults();
        return instance;
    };
    {
        auto instance = fresh();
        const auto out = instance->render(48000, [](Instance& in, uint32_t i, uint64_t) {
            in.control(Port::play) = 1;
            in.signal(Port::gate_cv, i) = 1;
        });
        require(rms(out, 0, out.size()) < 1e-6, "defaults: Play and a gate alone are silent");
    }
    auto instance = fresh();
    // The loudest case: the notes the bell is tuned to (E, Coarse 33), E2 then
    // E3, each sustained at a guitar's level (13 harmonics, peaks about 0.5,
    // fading), muted at 1.5 s; then the ring.
    const auto out = instance->render(48000 * 3, [](Instance& in, uint32_t i, uint64_t frame) {
        const double t = frame / 48000.0;
        double y = 0;
        for (const auto& [start, f] : {std::pair{0.0, 82.41}, std::pair{0.6, 164.81}}) {
            const double u = t - start;
            if (u < 0 || t >= 1.5) { continue; }
            for (int h = 1; h <= 13; ++h) { y += 0.25 * std::exp(-3 * u) * std::sin(2 * M_PI * f * h * u + h) / h; }
        }
        in.signal(Port::strike_in, i) = static_cast<float>(y);
    });
    double peak = 0;
    for (const auto& s : out) { peak = std::max({peak, static_cast<double>(std::fabs(s.l)), static_cast<double>(std::fabs(s.r))}); }
    const double ring = rms(out, 48000 * 2, 48000 * 3);
    std::printf("defaults: guitar E notes into the E bell peak at %.2f; 0.5 to 1.5 s after muting it rings at %.1f dBFS\n", peak, 20 * std::log10(ring));
    require(peak < 1.0, "defaults: a guitar at Strike in does not clip");
    require(ring > 1e-3, "defaults: the bell still rings after the guitar is muted");
    std::cout << "PASS defaults: silent alone, Play alone silent, a guitar rings a long bell\n";
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
        test_external_input(library.descriptor);
        test_switches(library.descriptor);
        test_defaults(library.descriptor);
    } catch (const std::exception& e) {
        std::cerr << "FAIL voice: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
