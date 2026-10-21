<!-- SPDX-License-Identifier: MIT -->
# Milestone 1 validation

**In progress.** Recorded 21 October 2026. Done: the resampler, every control and CV input, the three models, and their offline tests. To do: the Pi CPU and xrun check (below), listening to settle the CV gain, then `v0.1.0`.

| Check | Result |
| --- | --- |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS; GCC 14.2; no warnings |
| `make test SAN=1` | PASS |
| `make test SAN=thread` (under `setarch -R`) | PASS |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"` | PASS; Debian 12 root, GCC 12.2, QEMU user emulation |
| `lv2lint` 0.17.151 | No errors; author-email warning as in milestone 0 |
| `make test-hosts`, jalv and Ingen 1 and 4 threads, 128-frame period | PASS |
| **Pi 5: CPU and xruns** | **Not done** |

## Offline results

| Test | Result |
| --- | --- |
| `resampler`, 48 kHz | latency 88 frames, exact; passband within 0.0014 dB to 14 kHz; aliases and images -82.9 dB; 19-21 kHz -86.9 dB |
| `resampler`, 44.1 kHz | latency 79 frames (78.99); 0.0003 dB; -85.5 dB; -86.2 dB |
| `resampler`, 96 kHz | latency 172 frames, exact; 0.0006 dB; -85.9 dB; -85.2 dB |
| `rate equivalence` | at 48 kHz, modal, string and strings output equals the 32 kHz output through the same upsampler |
| `pitch` | the note moves exactly 12 semitones per 0.2 V/oct; modal fundamental within 2.8 cents of the note from 49 to 784 Hz (Elements' own tuning: -1.0, +0.8, +1.0, +2.2, +2.8 cents). The pitch estimator is checked to 0.1 cent on synthetic tones |
| `controls` | 35 cases: every knob, CV and attenuverter pair moves only its own field; an amount without CV, or CV without an amount, moves nothing; Space with CV reaches 2 (freeze) |
| `match_elements` | still bit for bit at 32 kHz (resamplers bypassed) |
| `test-timing`, x86-64 desktop, 48 kHz, 128 frames, worst-case settings | mean 31 us per period (1.2% load), 99.9% 81 us, worst 467 us. Says nothing about the Pi |

## Findings

- The plan's pitch target (1 cent over 5 octaves) is below Elements' own accuracy. The test now checks Chamber's part exactly (the note handed to Elements) and bounds the measured pitch at 5 cents; plan section 6.1 records why.
- Latency is 88 frames at 48 kHz, not about 1 ms: 31 frames of downsampling, 22 samples of upsampling and the 16-sample block, at 80 dB. A shorter filter would trade aliasing for latency; not needed unless listening says so.

## Pi check (to do)

On the Pi, at the commit to be tagged:

```sh
make OPT="-O2 -mcpu=cortex-a76" && make test OPT="-O2 -mcpu=cortex-a76"
make test-timing OPT="-O2 -mcpu=cortex-a76"
sudo make install LV2DIR=/opt/peepo/lib/lv2 OPT="-O2 -mcpu=cortex-a76"
```

Then a typical patch in Peepo (input -> Chamber -> amp -> output, plus a Mist) for 30 minutes with guitar or a looped DI, each model in turn, Space high, a gate from an LFO. Record JACK xruns, DSP load and RSS in Peepo's `docs/evidence/`. If the load is too high, reduce `kMaxModes` and record it (plan section 6.4).

## Listening (to do)

`tools/render` (`make render`) with V/oct, Geometry and Damping CVs at amounts of 0.5 and 1, to settle the CV gain (plan section 4.3, **tune**).
