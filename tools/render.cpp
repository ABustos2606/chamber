// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// Offline renderer for listening: loads chamber.so in the test host, plays
// an optional WAV into Strike in or Blow in, and writes a stereo 32-bit
// float WAV.
//
//   render chamber.so out.wav [--rate 48000] [--seconds 8] [--in guitar.wav]
//          [--into strike|blow] [--gate PERIOD,LENGTH] [symbol=value ...]
//
// --gate 2,0.5 holds the gate for 0.5 s every 2 s (default: one 0.5 s note
// at the start; --gate 0 for none). Controls are set by port symbol; a CV
// port given a value holds it, e.g. voct_cv=0.2.
#include "../tests/host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using chamber::Port;

namespace {

// Port symbols in index order; tests/ttl_check.py holds the enum and the
// Turtle together, and the count here is checked against the enum.
const char* const kSymbols[] = {
    "blow_in", "strike_in", "out_l", "out_r", "contour", "bow_level", "bow_timbre", "blow_level",
    "blow_meta", "blow_timbre", "strike_level", "strike_meta", "strike_timbre", "geometry",
    "brightness", "damping", "position", "model", "coarse", "fine", "fm_amount", "space", "play",
    "strength", "blow_in_level", "strike_in_level", "ominous", "output", "bow_timbre_amount",
    "blow_meta_amount", "blow_timbre_amount", "strike_meta_amount", "strike_timbre_amount",
    "geometry_amount", "brightness_amount", "damping_amount", "position_amount", "space_amount",
    "voct_cv", "fm_cv", "strength_cv", "gate_cv", "bow_timbre_cv", "blow_meta_cv", "blow_timbre_cv",
    "strike_meta_cv", "strike_timbre_cv", "geometry_cv", "brightness_cv", "damping_cv",
    "position_cv", "space_cv", "latency",
};
static_assert(sizeof(kSymbols) / sizeof(kSymbols[0]) == chamber::kPortCount, "one symbol per port");

uint32_t port_index(const std::string& symbol)
{
    for (uint32_t i = 0; i < chamber::kPortCount; ++i) {
        if (symbol == kSymbols[i]) { return i; }
    }
    throw std::runtime_error("unknown port symbol: " + symbol);
}

template <typename T> T read_le(std::istream& in)
{
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}

// Mono mix of a PCM 16/24/32-bit or float 32-bit WAV. Its rate must match.
std::vector<float> read_wav(const std::string& path, uint32_t rate)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { throw std::runtime_error("cannot open " + path); }
    char id[4];
    in.read(id, 4); read_le<uint32_t>(in); in.read(id + 0, 4);
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t file_rate = 0;
    std::vector<float> mono;
    while (in.read(id, 4)) {
        const auto size = read_le<uint32_t>(in);
        const std::string chunk(id, 4);
        if (chunk == "fmt ") {
            format = read_le<uint16_t>(in); channels = read_le<uint16_t>(in); file_rate = read_le<uint32_t>(in);
            read_le<uint32_t>(in); read_le<uint16_t>(in); bits = read_le<uint16_t>(in);
            in.seekg(size - 16 + (size & 1), std::ios::cur);
            if (format == 0xFFFE) { format = bits == 32 ? 3 : 1; } // extensible: assume PCM or float by width
        } else if (chunk == "data") {
            if (!channels || !bits) { throw std::runtime_error(path + ": data before fmt"); }
            if (file_rate != rate) { throw std::runtime_error(path + ": rate " + std::to_string(file_rate) + ", not " + std::to_string(rate)); }
            std::vector<char> data(size);
            in.read(data.data(), size);
            const uint32_t width = bits / 8, frames = size / (width * channels);
            mono.resize(frames);
            for (uint32_t f = 0; f < frames; ++f) {
                double sum = 0;
                for (uint32_t c = 0; c < channels; ++c) {
                    const char* p = &data[(static_cast<size_t>(f) * channels + c) * width];
                    if (format == 3 && bits == 32) { float v; std::memcpy(&v, p, 4); sum += v; }
                    else if (bits == 16) { int16_t v; std::memcpy(&v, p, 2); sum += v / 32768.0; }
                    else if (bits == 24) {
                        int32_t v = (static_cast<uint8_t>(p[0]) | static_cast<uint8_t>(p[1]) << 8 | static_cast<int8_t>(p[2]) * 65536);
                        sum += v / 8388608.0;
                    } else if (bits == 32) { int32_t v; std::memcpy(&v, p, 4); sum += v / 2147483648.0; }
                    else { throw std::runtime_error(path + ": unsupported sample format"); }
                }
                mono[f] = static_cast<float>(sum / channels);
            }
            return mono;
        } else {
            in.seekg(size + (size & 1), std::ios::cur);
        }
    }
    throw std::runtime_error(path + ": no data chunk");
}

void write_wav(const std::string& path, const std::vector<test::Stereo>& audio, uint32_t rate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) { throw std::runtime_error("cannot write " + path); }
    const uint32_t bytes = static_cast<uint32_t>(audio.size() * 8);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    out.write("data", 4); u32(bytes);
    for (const auto& f : audio) { out.write(reinterpret_cast<const char*>(&f.l), 4); out.write(reinterpret_cast<const char*>(&f.r), 4); }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 3) {
            std::cerr << "usage: render chamber.so out.wav [--rate R] [--seconds S] [--in file.wav]"
                         " [--into strike|blow] [--gate PERIOD,LENGTH] [symbol=value ...]\n";
            return 1;
        }
        uint32_t rate = 48000;
        double seconds = 8, gate_period = 0, gate_length = 0.5;
        std::string input, into = "strike";
        std::vector<std::pair<uint32_t, float>> settings;
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) { throw std::runtime_error(arg + " needs a value"); }
                return argv[++i];
            };
            if (arg == "--rate") { rate = static_cast<uint32_t>(std::stoul(value())); }
            else if (arg == "--seconds") { seconds = std::stod(value()); }
            else if (arg == "--in") { input = value(); }
            else if (arg == "--into") { into = value(); }
            else if (arg == "--gate") {
                const std::string v = value();
                const auto comma = v.find(',');
                gate_period = std::stod(v.substr(0, comma));
                if (comma != std::string::npos) { gate_length = std::stod(v.substr(comma + 1)); }
                if (gate_period == 0) { gate_length = 0; }
            } else {
                const auto eq = arg.find('=');
                if (eq == std::string::npos) { throw std::runtime_error("unknown argument " + arg); }
                settings.emplace_back(port_index(arg.substr(0, eq)), std::stof(arg.substr(eq + 1)));
            }
        }
        if (into != "strike" && into != "blow") { throw std::runtime_error("--into strike or blow"); }
        const std::vector<float> audio = input.empty() ? std::vector<float>{} : read_wav(input, rate);
        const Port target = into == "strike" ? Port::strike_in : Port::blow_in;

        test::Library library(argv[1]);
        test::Instance instance(library.descriptor, rate, 128);
        std::vector<std::pair<uint32_t, float>> held_cvs;
        for (const auto& [index, value] : settings) {
            if (chamber::is_signal(index)) { held_cvs.emplace_back(index, value); }
            else { instance.control(static_cast<Port>(index)) = value; }
        }
        const auto out = instance.render(static_cast<uint64_t>(seconds * rate), [&](test::Instance& in, uint32_t i, uint64_t frame) {
            const double t = static_cast<double>(frame) / rate;
            const bool gate = gate_period > 0 ? std::fmod(t, gate_period) < gate_length : t < gate_length;
            in.signal(Port::gate_cv, i) = gate ? 1.0f : 0.0f;
            in.signal(target, i) = frame < audio.size() ? audio[frame] : 0.0f;
            for (const auto& [index, value] : held_cvs) { in.signal(static_cast<Port>(index), i) = value; }
        });
        write_wav(argv[2], out, rate);
        float peak = 0;
        for (const auto& f : out) { peak = std::max({peak, std::fabs(f.l), std::fabs(f.r)}); }
        std::printf("wrote %s: %.2f s at %u Hz, peak %.3f, latency %.0f frames\n", argv[2], seconds, rate,
                    peak, instance.control(Port::latency));
    } catch (const std::exception& e) {
        std::cerr << "render: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
