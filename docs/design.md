# Chamber design

Chamber is a modal synthesis voice for Peepo, a pedal built on a Raspberry Pi 5 running the Ingen modular host. Bow, blow and strike exciters drive a resonator, followed by a reverb; external audio, such as a guitar, can excite the resonator in place of the built-in exciters. It is an LV2 plugin made from the open-source code of Elements, a eurorack module by Emilie Gillet. It must also work as a normal LV2 plugin in Carla or Ardour.

**Written:** 20 October 2026, from Peepo's `docs/38-eurorack-ports-roadmap.md` and a reading of Elements' source at `pichenettes/eurorack` commit `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026). **Status:** milestone 0 done, `v0.0.1` (21 October 2026); see `docs/milestone-0-validation.md`. Milestones 1 and 2 in progress: their offline work is done, the Pi checks are not; see `docs/milestone-1-validation.md` and `docs/milestone-2-validation.md`.

## Why Elements

- It is a complete physical-modelling voice, and the only one of its kind in Peepo. Peepo's Bell Resonator (Poly's Rings) resonates, but has no bow or blow exciters and no envelope.
- Guitar through a modal resonator is a strong pedal effect: the strings excite bells, plates and tubes.
- Its source is MIT licensed. No LV2 port exists; Poly's `lenticular_lv2` has none.
- Its `dsp/` code has no hardware-specific instructions and already runs on a desktop in its own `test/elements_test.cc`.

## Where it lives

Chamber has its own repository (https://github.com/ABustos2606/chamber), outside Peepo:

- Peepo builds every plugin from another repository at a pinned commit. Chamber is built the same way.
- It is a normal LV2 bundle, so it can be tested on a desktop without Ingen or the Pi.
- Its build is a plain Makefile.
- Its license is **MIT**, the same as Elements' code. Peepo (GPL-3.0-or-later) can build and ship it.
- Tagged releases let Peepo update to a new version on purpose.

What stays in Peepo:

- its entries in `config/catalogue/overlay.json` and `config/catalogue/inventory.txt`;
- the build step that fetches and builds it at a pinned commit;
- patches that use it, xrun measurements and test notes.

The plugin URI is `https://github.com/ABustos2606/chamber#chamber`, not Peepo's namespace. Fix it before the first saved patch, because changing it later breaks patches.

## Name

"Chamber": a resonating body. Mutable Instruments' guidelines ask derivative works not to use the trademark and not to keep the module's name (Peepo `docs/37-module-names.md`). "Elements" appears only in credits.

## What Elements does

From the source (`dsp/part.h`, `dsp/patch.h`, `cv_scaler.*`, `ui.cc`, `elements.cc`):

- **Exciter.** An envelope (Contour) shapes three sources: **bow** (level, timbre), **blow** (level, meta, timbre) and **strike** (level, meta, timbre). Meta morphs between kinds of excitation; timbre colours them. External audio enters at **blow in** (through the blow filter) and **strike in** (directly into the resonator).
- **Resonator.** Up to 64 modes. Geometry, Brightness, Damping and Position; pitch from Coarse, Fine, FM and V/oct. Three models, cycled by a hidden gesture: **modal** (the original), **string** and **strings**.
- **Space.** Stereo width and reverb amount in one knob.
- **Play.** A button and a gate input start the exciter envelope; Strength (pressure CV) sets velocity.
- **Ominous voice.** A hidden mode (`set_easter_egg`) that replaces the modal voice with a different synthesis voice (`dsp/ominous_voice.cc`); listen before describing it to users. From the source: two feedback-FM oscillators, run oversampled, through two multimode filters and a VCA on the exciter envelope, each moved around the stereo field. The panel's knobs take new jobs (exciter levels set the oscillator levels and detune, meta and timbre set ratio and FM amount, Geometry the filter shape, Position the movement). It is silent without a gate. Audio at Blow in and Strike in frequency-modulates the two oscillators instead of exciting a resonator. At the default settings it is about 17 dB louder than the modal voice (milestone 2).
- **Attenuverters** on most parameters, scaling their CV inputs.
- **Outputs:** main and aux, a stereo pair (`elements.cc` writes main to the right output and aux to the left).

## Plugins

One plugin: Chamber, `https://github.com/ABustos2606/chamber#chamber`. Stereo audio out; two audio inputs (blow and strike).

The resonator model and the Ominous voice are switched while running on the hardware (`set_resonator_model`, `set_easter_egg`), so they are controls, not separate plugins. The Ominous voice is offered **only if CPU allows** (decided with the user on 20 October 2026): measured on the Pi in milestone 2. If it doesn't fit, the `ominous` port stays declared and inert, and Peepo's overlay hides it.

## Controls

Draft. Symbols are fixed in milestone 0 and then never change.

| Group | Controls |
| --- | --- |
| Exciter | Contour; Bow level, Bow timbre; Blow level, Blow meta, Blow timbre; Strike level, Strike meta, Strike timbre |
| Resonator | Geometry, Brightness, Damping, Position; Model (Modal, String, Strings) |
| Pitch | Coarse (semitones), Fine, FM amount |
| Space and output | Space, Output (dB) |
| Play | Play (momentary), Strength |
| Input | Blow in level, Strike in level (for guitar) |
| Voice | Ominous (switch) |
| CV amounts | One per CV input below, -1 to 1 (the hardware's attenuverters) |

CV inputs: V/oct (Poly's scale: 1.0 = 5 V, so 0.2 per octave), FM, Strength, Gate, and one each for Bow timbre, Blow meta, Blow timbre, Strike meta, Strike timbre, Geometry, Brightness, Damping, Position, Space (the hardware's 13 CV inputs plus the gate).

## Sample rate

Elements is built for 32 kHz: `kSampleRate` in `dsp/dsp.h`, the lookup tables generated by `resources/lookup_tables.py` (`SAMPLE_RATE = 32000.0`), and the exciter samples in `resources/samples.py`. Three options:

1. **Resample** 48 -> 32 -> 48 kHz around the voice. Unchanged sound; adds latency (measured: 88 frames, 1.83 ms at 48 kHz) and the resampler's CPU. **Chosen** (20 October 2026).
2. **Regenerate the tables at 48 kHz** with upstream's generator (Python 2.5; port it to Python 3), set `kSampleRate` to 48 kHz, and resample only the exciter samples. Saves the resampler but costs 1.5 times the DSP per second.
3. **Set `kSampleRate` to 48 kHz only.** Wrong tuning and envelopes; not an option.

Chamber resamples (decided with the user on 20 October 2026). Option 2 stays a possible later optimisation if the Pi's CPU budget needs it; it would be a new decision, checked against `elements_test` renders.

## Guitar use

The guitar enters at Strike in or Blow in. With the exciter levels at zero, Chamber is a resonator effect: strings and plates that ring with the guitar.

- **Strike in** goes straight into the resonator. No gate is needed: a guitar alone makes it ring, at the resonator's pitch, not the guitar's.
- **Blow in** is added to the blow exciter, then passes the diffuser and the exciter envelope (`voice.cc`: `blow_buffer_ * e`). It is heard only while Play or a gate is held, shaped by Contour. Found in milestone 2; the first draft of this section said no input needed a gate.
- Play and the gate start the envelope for the internal exciters, and for Blow in.
- With the Ominous voice on, both inputs frequency-modulate its oscillators instead (see above), and it needs a gate.

There is no Dry/wet control (decided with the user on 21 October 2026): it is not in Elements, and Peepo's mixer modules blend the dry guitar.

## Memory

About 64 KB of reverb per instance, plus the resampler. No buffers are saved with patches.

## Decisions

Settled with the user on 20 October 2026:

- **Resample** 48 -> 32 -> 48 kHz around the unchanged 32 kHz voice.
- **The Ominous voice only if CPU allows**, measured on the Pi in milestone 2.

Settled in milestone 1 (21 October 2026): **other host rates** (44.1, 96 kHz and any integer rate) go through the same resampler, tested at 44.1, 48 and 96 kHz.

Settled in milestone 2 (21 October 2026):

- **No Dry/wet** (with the user). Peepo's mixer modules blend the dry guitar.
- **Model and Ominous changes fade**: 4 ms out, switch, 4 ms in. Elements switches at once, and the voice that was sounding stops dead, which clicks.

## Open questions

| Question | Why it matters | When to settle |
| --- | --- | --- |
| CPU on the Pi | 64 modes and the reverb at 32 kHz, plus resampling | Milestone 1, measured; reduce the mode count if needed |
| Ominous voice CPU | Kept only if it fits on the Pi. On a desktop it costs 1.8 times the worst modal case | Milestone 2, measured on the Pi |

## Milestones

- [x] Milestone 0: vendored code, Makefile, LV2 glue and `.ttl` with the full port list. Chamber plays a struck note on Play. Loads in jalv and in Ingen under WSL.
- [ ] Milestone 1: the resampler, every control and CV input, the three models. Renders match `elements_test`. Pi CPU and xrun check.
- [ ] Milestone 2: guitar into Blow in and Strike in, levels, the Ominous voice if kept, Dry/wet if wanted (not wanted). Offline work done; Pi measurement of the Ominous voice to do.
- [ ] Milestone 3: Peepo integration: overlay entry, pinned build, listening with guitar on the Pi. Tag `v0.1.0`.
