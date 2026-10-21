# Chamber

Chamber is an LV2 modal synthesis voice: bow, blow and strike exciters drive a resonator, followed by a reverb, and external audio (a guitar) can excite it. It is ported from the open-source code of Elements, a eurorack module by Emilie Gillet. It is a plugin written for **Peepo**, and Peepo is the host that matters. Read `docs/design.md` before changing behaviour or ports: it is the spec. `docs/implementation-plan.md` says how to build it.

## Peepo

Peepo is a guitar pedal built on a Raspberry Pi 5 (2 GB, Debian 12 arm64) running the **Ingen** modular host on JACK, with an LVGL touch UI. Users patch LV2 modules together, including CV modules (LFOs, envelopes, random sources). It has two encoders and three momentary stomps. It was called "Pedal PI" before; old names (`pedalpi`, `urn:pedalpi:ns#`) still turn up.

- Repository: `C:\Pedal PI` (https://github.com/ABustos2606/Peepo). It is readable from here (see `.claude/settings.json`).
- Docs worth reading there:
    - `docs/38-eurorack-ports-roadmap.md`: why Drizzle, Chamber, Slope and Reel exist, their order and the Peepo-side steps;
    - `docs/37-module-names.md`: why the original module names are not used;
    - `docs/22-plugin-catalogue.md` and `config/catalogue/overlay.json`: how plugins are named, grouped and described in the UI;
    - `reference/upstream/lenticular_lv2/src/polyrings.cpp`: Poly's port of Rings (Peepo's Bell Resonator), the closest existing plugin: block splitting, V/oct and strum handling;
    - `reference/upstream/ingen/`: Ingen's source.
- `C:\LV2-own-plugins\discipline` and `C:\LV2-own-plugins\loo-pi` are sister projects: same host limits, same kind of build, tests and milestones. Copy their patterns (LV2 glue, `ttl_check`, in-process test host, timing test, renderer) rather than reinventing them. `drizzle`, `slope` and `reel` are the other three Gillet ports, built the same way.
- Peepo builds every third-party plugin from its own repository at a pinned commit (`tools/ingen-spike/14-more-plugins.sh`, `reference/manifest.json`). Chamber will be built the same way, so tag releases and keep the build plain (`make`, no network access, works natively on arm64).

## What is kept and what is not

- **Kept, unchanged:** Elements' `dsp/`, `resources.cc/.h` and the `stmlib` files they need, vendored from `pichenettes/eurorack` at commit `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026) into `third_party/eurorack/`, with their MIT notices.
- **Ported, not vendored:** the parts of `elements.cc`, `cv_scaler.cc` and `ui.cc` that turn knobs, CV and the Play button into `Patch` and `PerformanceState`. Copy their rules exactly, naming the source function; don't reinvent them.
- **Not used:** drivers, bootloader, calibration, the hardware design.
- New code lives in `src/` (LV2 glue, resampler, parameter mapping, port enums), `tools/` and `chamber.lv2/` (Turtle). Change vendored files only where the plan says, and keep those changes small and commented.

## Host limits that shape the code

- **Elements runs at 32 kHz.** `kSampleRate` is a compile-time constant in `dsp/dsp.h`, and the lookup tables and exciter samples in `resources.cc` were made for 32 kHz. Peepo runs at 48 kHz. How Chamber bridges that is the first decision in the spec.
- **xruns are the limit, not average CPU.** Ingen runs the whole patch as one JACK client. The resonator has up to 64 modes; Chamber may be the most expensive module in a patch. In `run()`: no allocation, no locks, no file or console I/O. Allocate the reverb buffer (32,768 `uint16_t`) in `instantiate`.
- **`run()` arrives in slices.** Ingen splits a period at every control change, so `n_samples` varies. Elements processes in blocks of up to 16 frames; the glue must not assume one `run()` per period.
- **Blocks run on several threads.** Two Chamber instances may run at the same moment; they share nothing.
- **Controls arrive once per audio block.** Elements smooths its own parameters internally; anything the glue adds is smoothed per sample.
- **Unpatched inputs read silence.** A plugin can't tell whether a port is connected; don't guess.
- **No LV2 state.** Everything a user sets (resonator model, the Ominous voice) is a control port, so patches save it.

## Rules

- Plugin URI: `https://github.com/ABustos2606/chamber#chamber`. Never use Peepo's namespace. Don't change a URI or port symbol once a patch has been saved with it.
- Never use "Mutable Instruments" or "Elements" in plugin names, port names, descriptions, URIs or new code identifiers. Vendored upstream code keeps its own names (`namespace elements`). Credit Emilie Gillet and Elements in the README and `doap:` metadata only.
- Port names and descriptions appear on Peepo's small screen: short, plain ASCII (its font draws other characters as boxes), and they say what the control does, never who made it.
- License: MIT, `Copyright (c) 2026 Alejandro Bustos`. Every new source file, header, Makefile and `.ttl` file starts with `SPDX-License-Identifier: MIT`; the plugin declares `doap:license <https://spdx.org/licenses/MIT>`. Vendored files keep Emilie Gillet's copyright and MIT notice unchanged.
- Line endings are LF. Files go to Linux and the Pi.
- Use Peepo's name, never "Pedal PI", in new text.

## Testing

- Desktop: any LV2 host (Carla, Ardour, `jalv`), and `lv2lint` for the `.ttl` files.
- Elements' own `test/elements_test.cc` renders at 32 kHz; compare Chamber's output against it.
- Peepo's development engine runs under WSL (Debian) with Ingen in `/opt/peepo`; Peepo's `tools/ingen-spike/env.sh` starts it.
- On the Pi: measure xruns and DSP load with Chamber in a typical patch before calling a milestone done.
