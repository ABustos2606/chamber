<!-- SPDX-License-Identifier: MIT -->
# Milestone 2 validation

**In progress.** Recorded 21 October 2026. Done: guitar input at Strike in and Blow in, input levels, the `external_input` test, model and Ominous changes without clicks, the Ominous voice in the timing test, the Dry/wet decision. To do: the Ominous voice's CPU on the Pi (keep or hide it), then `v0.2.0`. Milestone 1's own Pi check is also still open.

| Check | Result |
| --- | --- |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS; GCC 14.2; no warnings |
| `make test SAN=1` | PASS |
| `make test SAN=thread` (under `setarch -R`) | PASS |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"` | PASS; Debian 12 root, GCC 12.2, QEMU user emulation |
| `lv2lint` | 0.17.151: no errors; author-email warning as before |
| `make test-hosts`, jalv and Ingen 1 and 4 threads, 128-frame period | PASS; Model, Ominous and controls save and restore |
| **Pi 5: Ominous voice CPU and xruns** | **Not done** |

## Decisions

- **No Dry/wet** (with the user, 21 October 2026). It is not in Elements; Peepo's mixer modules blend the dry guitar. No port added.
- **Model and Ominous changes fade**, 4 ms out and 4 ms in (plan section 4.4).

## Offline results

| Test | Result |
| --- | --- |
| `external_input`, Strike in, no gate, exciter levels 0 | A plucked 110 Hz guitar-like note (12 harmonics, sharp attack, 33 ms decay); resonator at note 55 (196 Hz), Damping 0.7, Space 0. Half a second after the pluck has died the resonator still rings: modal -53 dB at 196.00 Hz (0.0 cents), string -42 dB at +2.3 cents, strings -42 dB at +6.9 cents (its chord's strings are detuned). The note is 44 to 85 dB over the guitar's 110 and 220 Hz |
| `external_input`, no input | silent (below -120 dB): the sound is the guitar's |
| `external_input`, Blow in | -41 dB with the gate held; silent without it, and silent with the gate alone |
| `external_input`, levels | Strike in level and Blow in level at -12 dB: output -12.48 dB. The extra 0.48 dB is Elements' input noise gate on the quieter signal's decay |
| `switches` | before the fade, a change while a note rang made a click 48 (modal to string), 123 (strings to modal) and 760 (Ominous on) times the ringing note's largest second difference. With it: modal to string and strings to modal stay below the ringing note's own; Ominous on and off stay below the Ominous voice's own. The next note plays on the new voice |
| `match_elements` | still bit for bit at 32 kHz: the first block applies the patch's model and voice without a fade |
| `test-timing`, x86-64 desktop, 48 kHz, 128 frames, worst-case settings | Strings: mean 31 us per period (1.2% load), 99.9% 88 to 107 us. Ominous: mean 57 us (2.1%), 99.9% 160 to 191 us. Worst single periods 0.4 to 2.2 ms across three runs, in both cases: WSL scheduling, not the plugin. Says nothing about the Pi beyond the ratio: the Ominous voice costs about 1.8 times the worst modal case |

## Findings

- **Blow in needs a gate.** `voice.cc` adds Blow in to the blow exciter before the exciter envelope (`blow_buffer_[i] * e`), so it is heard only while Play or a gate is held, shaped by Contour. Strike in is added after the envelope and needs none. The spec said no input needed a gate; `design.md` (Guitar use), the README and the plugin's description are corrected. No code change: this is Elements' behaviour.
- **Damping is sustain.** Higher Damping rings longer: at 0.2 the output died as fast as the pluck itself (7 dB per 100 ms); at 0.7 the modal voice loses about 1.5 dB per 100 ms, and at 0.9 under 1 dB. For a guitar effect the default 0.25 is short; **tune** in milestone 3.
- **`set_resonator_model` and `set_easter_egg` reset nothing**: each voice keeps its own state, the old one stops dead and a voice switched back to resumes. The fade removes the click; the resumed tail is upstream behaviour and is kept.
- **The Ominous voice** is two oversampled feedback-FM oscillators through multimode filters, not a resonator. With it on, Blow in and Strike in frequency-modulate the oscillators, and nothing sounds without a gate. At default settings it is about 17 dB louder than the modal voice (gated renders: -16.8 against -34.3 dBFS RMS); peaks stayed below 0.92 with every level at 1. Part does not soft-limit it. Listen before describing it to users (spec).

## Pi check (to do)

On the Pi, at the commit to be tagged, as in milestone 1:

```sh
make OPT="-O2 -mcpu=cortex-a76" && make test OPT="-O2 -mcpu=cortex-a76"
make test-timing OPT="-O2 -mcpu=cortex-a76"     # Strings, then Ominous
sudo make install LV2DIR=/opt/peepo/lib/lv2 OPT="-O2 -mcpu=cortex-a76"
```

Then the typical patch (input -> Chamber -> amp -> output, plus a Mist), 30 minutes with the Ominous voice on, a gate from an LFO and a guitar or looped DI at Blow in and Strike in. Record JACK xruns, DSP load and RSS in Peepo's `docs/evidence/`. If it causes xruns or leaves too little room for the rest of a patch, `ominous` stays declared and does nothing, and Peepo's overlay hides it (spec, Plugins).

## Listening (to do)

`tools/render --in guitar.wav --into strike strike_level=0` for each model, Damping 0.5 to 0.9, and input levels around 0 dB, to set the input levels' defaults (plan section 3, **tune**). Then `ominous=1` with a gate, to describe the Ominous voice in Peepo's overlay.
