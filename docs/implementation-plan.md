# Chamber implementation plan

**Written:** 20 October 2026. **Follows:** [design.md](design.md), the spec. Where this plan and the spec disagree, the spec wins and this plan is fixed. **Checked against:** Elements' `elements.cc`, `cv_scaler.h/.cc`, `ui.cc`, `drivers/cv_adc.h`, `drivers/pots_adc.h`, `drivers/codec.h`, `dsp/dsp.h`, `dsp/part.h`, `dsp/patch.h`, `dsp/voice.h`, `dsp/resonator.h` and `resources/lookup_tables.py` at upstream commit `f4cfa7697095a28d292bff758beb51deb49b5a86`; Poly's `polyrings.cpp` in Peepo's `reference/upstream/lenticular_lv2/`; Discipline's implementation plan, whose Ingen findings apply unchanged.

This plan says how to build Chamber: code layout, ports, the resampler, the glue around `elements::Part`, real-time rules, tests and milestones. Values that only listening can settle are given a starting value and marked **tune**.

## 1. What Ingen's source changes

From Discipline's plan (section 1), unchanged:

1. **Engine bypass does not run the plugin.** A bypassed Chamber stops; its reverb tail is cut. Accepted unless listening says otherwise.
2. **`run()` is called in slices.** Ingen splits a period at every control change. The resampler and the 16-frame block staging handle any slice length (section 4).
3. **Blocks run on several threads.** Instances share nothing.
4. **Block length** comes from `bufsz:nominalBlockLength` / `maxBlockLength` through `options:options`. Chamber needs it only to size scratch buffers.

## 2. Technology and build

- **Plain LV2 C API in C++17**, no DPF. One shared object, `chamber.so`, in `chamber.lv2/` beside `manifest.ttl` and `chamber.ttl`.
- **License:** MIT. **Dependencies:** `lv2-dev` headers only. The resampler is Chamber's own (a polyphase 3:2 filter), not libsamplerate, so nothing new is linked.
- **Makefile**, no configure step, no network: `make`, `make install` (`DESTDIR`, `PREFIX`, `LV2DIR`), `make test`, `make lint`, `OPT` and `SAN=1`, as in Discipline. The Pi build passes `OPT="-O2 -mcpu=cortex-a76"`.
- **Flags:** `-std=c++17 -fPIC -fvisibility=hidden -Wall -Wextra`. The vendored code's warnings are silenced for `third_party/` only. Only `lv2_descriptor` is exported.
- **Releases** are git tags (`v0.1.0` ...). Peepo pins a commit.

### Repository layout

```
Makefile
LICENSE                      MIT
README.md                    what it is, build, install, credit to Elements and Emilie Gillet
chamber.lv2/
    manifest.ttl
    chamber.ttl
third_party/eurorack/        vendored at f4cfa769, unchanged unless section 7 says
    README.md                upstream commit, file list, local changes
    elements/dsp/...
    elements/resources.cc/.h
    stmlib/...               only the files elements/dsp includes
src/
    lv2_entry.cpp
    ports.hpp                port index enums, the single source in C++
    chamber.hpp/.cpp         instantiate, activate, run, cleanup
    controls.hpp/.cpp        ports -> Patch and PerformanceState (from cv_scaler.cc Read)
    resampler.hpp            48 <-> 32 kHz polyphase, any host rate to 32 kHz
    block_buffer.hpp         16-frame staging for Part::Process
tools/
    vendor.sh                copies the listed upstream files at the pinned commit (run by hand)
    render.cpp               WAV in (guitar), settings, WAV out, for listening
tests/
    host.hpp/.cpp            in-process LV2 host with random slicing (from Discipline)
    test_*.cpp
    ttl_check.py
docs/
    design.md, implementation-plan.md
```

## 3. Ports

Port **symbols and URIs are permanent** once a patch has been saved with them. All ports are declared in milestone 0; until a port does something it is inert. Ports added later are only appended, with `lv2:minorVersion` raised. Names are short plain ASCII, at most 16 characters.

### Chamber: `https://github.com/ABustos2606/chamber#chamber`

| Symbols | Kind | Notes |
| --- | --- | --- |
| `blow_in`, `strike_in` | audio in | External excitation |
| `out_l`, `out_r` | audio out | `aux` and `main`, as `elements.cc` |
| `contour` | control 0-1 | Exciter envelope shape |
| `bow_level`, `bow_timbre` | control 0-1 | |
| `blow_level`, `blow_meta`, `blow_timbre` | control 0-1 | |
| `strike_level`, `strike_meta`, `strike_timbre` | control 0-1 | |
| `geometry`, `brightness`, `damping`, `position` | control 0-1 | |
| `model` | enumeration | Modal, String, Strings |
| `coarse` | control, integer | semitones, range **tune** from `cv_scaler.cc` |
| `fine`, `fm_amount` | control | |
| `space` | control 0-1 | |
| `play` | momentary | The Play button |
| `strength` | control 0-1 | Used when no Strength CV is patched |
| `blow_in_level`, `strike_in_level` | control | Input gains, dB |
| `ominous` | toggle | The Ominous voice |
| `output` | control | dB, -24 to 12, 0 |
| 10 `*_amount` | control -1 to 1 | Attenuverters for the 10 CV inputs that have one on the hardware |
| `voct_cv`, `fm_cv`, `strength_cv`, `gate_cv` | CV in | |
| `bow_timbre_cv`, `blow_meta_cv`, `blow_timbre_cv`, `strike_meta_cv`, `strike_timbre_cv`, `geometry_cv`, `brightness_cv`, `damping_cv`, `position_cv`, `space_cv` | CV in | |

Indices are assigned in milestone 0 in this order. Peepo's overlay groups them into pages.

## 4. The glue

### 4.1 Instantiate

Allocate the reverb buffer (32,768 `uint16_t`, as `elements.cc`), call `Part::Init(reverb_buffer)` and `Part::Seed`. The hardware seeds from the chip's unique ID (`0x1fff7a10`); Chamber seeds from a fixed value, so renders repeat, set up the resampler for the host rate, allocate scratch buffers from the maximum block length.

### 4.2 Rate and blocks

Host rate -> 32 kHz: the two audio inputs are downsampled, `Part::Process` runs on 16-frame blocks (`kMaxBlockSize`) at 32 kHz, and the two outputs are upsampled back. At 48 kHz that is 3:2 polyphase; other host rates use a general windowed-sinc resampler. Latency is reported through an `lv2:reportsLatency` output port so hosts can compensate. Filter length and passband **tune** (against CPU and aliasing).

Resampling was chosen on 20 October 2026. The alternative in the spec (tables regenerated at 48 kHz) would change only this section, and only as a later optimisation.

### 4.3 Controls

`controls.cpp` copies `cv_scaler.cc`'s `Read`: each pot plus its attenuverted CV into `Patch`; pitch from Coarse, Fine, FM and V/oct; `PerformanceState.gate` from `play` OR `gate_cv`; `strength` is the control plus `strength_cv`, clamped, because a plugin can't tell an unpatched input from 0 V. Calibration offsets from the hardware are dropped. CV scale: 1.0 = 5 V; V/oct is `cv * 60` semitones.

### 4.4 Model and Ominous voice

`set_resonator_model` and `set_easter_egg` are called when `model` or `ominous` changes, at a block boundary. Check what they reset; fade around a change if it clicks.

## 5. Real-time rules, checked

- No allocation, lock or I/O in `run()` (allocation hook in the test host).
- Output finite: NaN or infinity at an input is zeroed per channel.
- `rt_timing` at the worst case: 64 modes, Strings model, reverb full.

## 6. Tests

### 6.1 Offline (`make test`)

| Test | Check |
| --- | --- |
| `ttl_check` | every port matches `ports.hpp`; names ASCII <= 16 characters |
| `slicing` | same output for random host slice lengths |
| `match_elements` | at a 32 kHz host rate (no resampling), output matches the vendored `Part` driven directly, bit for bit |
| `resampler` | 48 kHz: a sine sweep keeps its level within **tune** dB to 14 kHz; aliasing below **tune** dB; latency equals the reported value |
| `pitch` | V/oct 0.2 per octave: measured fundamental within 1 cent over 5 octaves |
| `controls` | each knob and CV changes the matching `Patch` field and nothing else |
| `external_input` | guitar-like input at Strike in, exciter levels 0: output rings at the resonator's pitch |
| `nonfinite` | NaN input: output finite, recovers |
| `rt_timing` | 10 simulated minutes of random controls: worst and 99.9th-percentile `run()` time |

### 6.2 Desktop hosts

`lv2lint`, `jalv`, Carla. `tools/render` with a guitar DI for listening.

### 6.3 Ingen under WSL

With Peepo's `tools/ingen-spike/env.sh`: load, gate from a Peepo LFO, guitar test signal into Strike in, save and reload, one and four threads.

### 6.4 On the Pi

Typical patch (input -> Chamber -> amp -> output, plus a Mist) for 30 minutes with guitar or a looped DI, every model. Record JACK xruns, DSP load and RSS into Peepo's `docs/evidence/`. If the load is too high, reduce the number of modes (`kMaxModes`) and record the change.

## 7. Changes to vendored files

None planned for the resampling option. The 48 kHz option changes `dsp/dsp.h` and regenerates `resources.cc`; both would be listed in `third_party/eurorack/README.md`.

## 8. Milestones

Each has an end check. "Done" means the offline tests pass on x86-64 and arm64, plus the stated host checks.

- **Milestone 0, scaffolding.** `tools/vendor.sh`, vendored files, Makefile, descriptor, `.ttl` with the full port list, `ports.hpp`, `ttl_check`. Play strikes a note at 32 kHz host rate. **Done when** `make`, `make install DESTDIR=...` and `lv2lint` are clean and Chamber loads in jalv and in Ingen under WSL. Tag `v0.0.1`.
- **Milestone 1, the voice.** The resampler, every control and CV input, the three models, `match_elements`, `resampler`, `pitch`, `rt_timing`. Pi CPU and xrun check. Tag `v0.1.0`; Peepo adds its overlay entry and pinned build.
- **Milestone 2, guitar.** Blow in and Strike in levels, `external_input`, the Ominous voice measured on the Pi and kept only if it fits (otherwise `ominous` stays inert and hidden in Peepo), Dry/wet if wanted. Tag `v0.2.0`.
- **Milestone 3, listening.** Guitar on the Pi; defaults tuned for guitar use. `v0.2.x` tags of tuning.

## 9. Peepo-side work

In Peepo's repository, not here:

1. Build step in `tools/ingen-spike/14-more-plugins.sh`: `third chamber https://github.com/ABustos2606/chamber.git <commit> "make" make -j"$JOBS" OPT=... LV2DIR="$LV2DIR" install`.
2. URIs in `config/catalogue/inventory.txt` and `tools/catalogue-inventory.py`.
3. Overlay entry: name "Chamber", category Resonator, a description that says what it does, `"output": "output"`, groups (**Exciter**, **Resonator**, **Pitch**, **Play and input**, **CV amounts**).
4. `docs/37-module-names.md` and `docs/33-module-list.md` regenerated.

## 10. Risks

| Item | Why it matters |
| --- | --- |
| CPU on the Pi | 64 modes plus reverb plus resampling, on one core |
| Resampler quality | Aliasing or dull top end on guitar |
| Strength with no CV | The hardware's normalled behaviour can't be reproduced; control + CV instead |
| Model switch clicks | `set_resonator_model` may reset the resonator |
