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
| `latency` | control out | Frames of latency (`lv2:reportsLatency`), section 4.2 |

Indices were assigned in milestone 0 in this order, 0 to 52; `src/ports.hpp` is the record and `tests/ttl_check.py` holds them fixed. Peepo's overlay groups them into pages.

Settled in milestone 0 (21 October 2026):

- `coarse` is an integer, 0 to 60 semitones, default 30: the hardware's 5-octave quantized knob (`LAW_QUANTIZED_NOTE`). The note is `coarse + 19 + fine + 12 * V/oct volts`, as `CvScaler::Read`.
- Bipolar controls (`fine`, `fm_amount`, the `*_amount` attenuverters) are knob positions, -1 to 1; the hardware's laws (`x * |x| * 3.3`, quartic for FM) are applied inside, so they feel like the panel.
- `blow_meta` and `strike_meta` are named "Blow flow" and "Strike mallet", the panel's words for them.
- Input levels are -24 to +24 dB, default 0 (**tune** with a guitar in milestone 2).
- Unipolar defaults are `Part::Init`'s values (contour 1, strike 0.8, geometry 0.2, damping 0.25, position 0.3, space 0.5); **tune** in milestone 3.

## 4. The glue

### 4.1 Instantiate

Allocate the reverb buffer (32,768 `uint16_t`, as `elements.cc`), call `Part::Init(reverb_buffer)` and `Part::Seed`. The hardware seeds from the chip's unique ID (`0x1fff7a10`); Chamber seeds from a fixed value, so renders repeat, set up the resampler for the host rate, allocate scratch buffers from the maximum block length.

`Part` is allocated zeroed: its constructor is empty and `Part::Init` does not set `panic_` or `easter_egg_`, which on the hardware are zero because `Part` is a global. `activate()` zeroes and rebuilds it before `Init`, because `Init` alone does not clear all voice state (found in milestone 0: a re-activated instance did not repeat its first render).

### 4.2 Rate and blocks

Host rate -> 32 kHz: the two audio inputs are downsampled, `Part::Process` runs on 16-frame blocks (`kMaxBlockSize`) at 32 kHz, and the two outputs are upsampled back. Latency is reported through an `lv2:reportsLatency` output port so hosts can compensate.

Built in milestone 1 (21 October 2026), `src/resampler.hpp` and `src/rate_bridge.hpp`:

- One polyphase windowed-sinc design serves every host rate. The ratio is kept as an exact integer fraction, so the clocks never drift. 192 phases: 48 and 96 kHz land on exact rows; other rates interpolate linearly between rows.
- Kaiser window for 80 dB; transition 14 to 18 kHz on the 32 kHz side. Measured: passband flat within 0.002 dB to 14 kHz, aliases and images at -83 dB, 19-21 kHz inputs at -86 dB.
- Delays: 31 host frames down, 22 voice samples up (rounded up from 21 so the total is whole), plus the 16-sample staging. **Latency at 48 kHz: 88 frames, 1.83 ms**, not the spec's guess of about 1 ms. 44.1 kHz: 79 (78.99, rounded); 96 kHz: 172. At 32 kHz both resamplers are bypassed: 16.
- Only audio inputs pass the downsampler. A note started by Play or a gate leaves after the upsampler and staging alone (57 frames at 48 kHz), 31 frames before the audio inputs' path would. Accepted: 0.65 ms.
- At 48 kHz, Chamber's output with silent inputs is Elements' own 32 kHz output through the upsampler (`rate equivalence` test): pitch, envelopes and reverb are unchanged.

Resampling was chosen on 20 October 2026. The alternative in the spec (tables regenerated at 48 kHz) would change only this section, and only as a later optimisation.

### 4.3 Controls

`controls.cpp` copies `cv_scaler.cc`'s `Read`: each pot plus its attenuverted CV into `Patch`; pitch from Coarse, Fine, FM and V/oct; `PerformanceState.gate` from `play` OR `gate_cv`; `strength` is the control plus `strength_cv`, clamped, because a plugin can't tell an unpatched input from 0 V. Calibration offsets from the hardware are dropped. CV scale: 1.0 = 5 V; V/oct is `cv * 60` semitones.

It runs once per 16-frame block, as `Read` runs once per codec block, with the same smoothing (0.01 per block on every pot, the per-parameter `BIND` coefficients, the V/oct slew, the gate read two blocks late). Two differences: every knob is read on every block (the hardware scans one pot per block), and the first block after `activate()` starts the filters at the knob values instead of at zero, so a loaded patch does not sweep. CVs are sampled at the frame that completes the block.

- **CV gain, tune.** Firmware does not state the attenuverted inputs' volts-to-ADC gain. Chamber uses 1 V = 1/19.8 of the knob range, from the 0 V offset constant in `CvScaler::Init` (`10 V * 20k / 120k / 3.3 V`). With an amount of 1 (x 3.3), a CV of 1.0 (5 V) moves a parameter by 0.83. Check by ear in milestone 1.
- **Gate threshold:** 0.4 (2 V), as Poly's Rings port; Peepo's gates are 0 or 1.
- **Input conditioning** copies `elements.cc` `FillBuffer`: each input is clipped to the codec's full scale, then its level follower and noise gate apply (at 32 kHz once resampling is in).

### 4.4 Model and Ominous voice

`set_resonator_model` and `set_easter_egg` are called when `model` or `ominous` changes, at a block boundary. Check what they reset; fade around a change if it clicks.

Checked in milestone 2 (21 October 2026): neither resets anything. Each only chooses what `Part::Process` renders; the modal resonator, the strings and the Ominous voice keep their own state. The voice that was sounding stops dead (a click 50 to 760 times the ringing note's second difference, `switches` test), and one switched back to resumes from where it stopped. Chamber therefore fades: on a change, the voice's output ramps to zero over 128 samples at 32 kHz (4 ms), both settings are applied together, and it ramps back over 4 ms. The reverb is inside `Part`, so its tail dips with the rest for those 8 ms. The first block after `activate()` applies the patch's settings at once, so `match_elements` stays bit for bit. The resumed state is upstream behaviour and is kept.

## 5. Real-time rules, checked

- No allocation, lock or I/O in `run()` (allocation hook in the test host).
- Output finite: NaN or infinity at an input is zeroed per channel.
- `rt_timing` at the worst case: 64 modes, Strings model, reverb full.

## 6. Tests

### 6.1 Offline (`make test`)

| Test | Check |
| --- | --- |
| `ttl_check` | every port matches `ports.hpp`; names ASCII <= 16 characters |
| `scaffold` | descriptor, rates, unconnected ports, Play and Gate strike a note, instances alone/interleaved/threaded keep their own sequence, `activate` restarts, models differ, non-finite input |
| `slicing` | same output for random host slice lengths |
| `match_elements` | at a 32 kHz host rate (no resampling), output matches the vendored `Part` driven directly, bit for bit |
| `resampler` | 48, 44.1 and 96 kHz, through the plugin's own bridge with an identity voice: level within 0.05 dB to 14 kHz; everything but the input sine below -70 dB; 19 and 21 kHz below -70 dB; latency equals the reported value (exact at 48 and 96 kHz, within half a frame at 44.1 kHz) |
| `pitch` | the note handed to Elements moves exactly 12 semitones per 0.2 of V/oct; the modal voice's measured fundamental stays within 5 cents of that note over 5 octaves. The first draft asked for 1 cent, but Elements' own 32 kHz output is up to 2.8 cents off its note (its resonator's fast frequency approximations, kept as written), and Chamber adds nothing to that: see `rate equivalence` |
| `rate equivalence` | at 48 kHz, each model's output is the 32 kHz output through the same upsampler, within 1e-6 of peak |
| `controls` | each knob and CV changes the matching `Patch` field and nothing else |
| `external_input` | guitar-like input at Strike in, exciter levels 0, no gate: each model rings at the resonator's note after the pluck has died, 20 dB over the guitar's notes; silent with no input; Blow in silent without a gate and heard with one; `-12 dB` input level gives 12 dB less output (within 0.5 dB; the noise gate takes the rest) |
| `switches` | model and Ominous changes while a note rings: no click (the largest second difference after the change at most twice that of the steady sound on either side); the new voice plays its next note |
| `nonfinite` | NaN input: output finite, recovers |
| `rt_timing` | `make test-timing`: 60 simulated seconds at 48 kHz, worst case (Strings, full reverb, every exciter, moving Geometry), then the same with the Ominous voice: mean, 99.9th percentile and worst `run()` time per period. Informative only off the Pi |
| `render` | `tools/render` writes a WAV (smoke-tested by `make test`) |

### 6.2 Desktop hosts

`lv2lint`, `jalv`, Carla. `tools/render` with a guitar DI for listening. `make test-hosts` (`tests/host_smoke.py`, from Discipline) runs jalv and Ingen on a private dummy JACK server.

### 6.3 Ingen under WSL

With Peepo's `tools/ingen-spike/env.sh`: load, gate from a Peepo LFO, guitar test signal into Strike in, save and reload, one and four threads.

### 6.4 On the Pi

Typical patch (input -> Chamber -> amp -> output, plus a Mist) for 30 minutes with guitar or a looped DI, every model. Record JACK xruns, DSP load and RSS into Peepo's `docs/evidence/`. If the load is too high, reduce the number of modes (`kMaxModes`) and record the change.

## 7. Changes to vendored files

One, found in milestone 0: `stmlib::Random` keeps its state in a static, written every sample by the exciter and the string model. Two instances on two Ingen threads would race on it. `tools/patches/stmlib-random-thread-local.patch` makes it `thread_local`, and the glue saves and restores each instance's state around `Part::Process`, so each instance has its own repeatable sequence; the numbers drawn are unchanged. It is built with `-ftls-model=initial-exec`, because a `dlopen`ed library's ordinary `thread_local` is allocated with `malloc` on each thread's first use, inside `run()`. `tools/vendor.sh` applies the patch; `third_party/eurorack/README.md` lists it. The `instances` test and ThreadSanitizer check it.

The 48 kHz option would change `dsp/dsp.h` and regenerate `resources.cc`, listed the same way.

## 8. Milestones

Each has an end check. "Done" means the offline tests pass on x86-64 and arm64, plus the stated host checks.

- **Milestone 0, scaffolding.** `tools/vendor.sh`, vendored files, Makefile, descriptor, `.ttl` with the full port list, `ports.hpp`, `ttl_check`. Play strikes a note at 32 kHz host rate. **Done when** `make`, `make install DESTDIR=...` and `lv2lint` are clean and Chamber loads in jalv and in Ingen under WSL. Tag `v0.0.1`.
- **Milestone 1, the voice.** The resampler, every control and CV input, the three models, `match_elements`, `resampler`, `pitch`, `rt_timing`. Pi CPU and xrun check. Tag `v0.1.0`; Peepo adds its overlay entry and pinned build.
- **Milestone 2, guitar.** Blow in and Strike in levels, `external_input`, the Ominous voice measured on the Pi and kept only if it fits (otherwise `ominous` stays inert and hidden in Peepo), Dry/wet if wanted (not wanted, 21 October 2026). Model and Ominous changes fade (section 4.4, `switches`). Tag `v0.2.0`. Record: `docs/milestone-2-validation.md`.
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
