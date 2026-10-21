<!-- SPDX-License-Identifier: MIT -->
# Vendored upstream code

Elements' DSP code by Emilie Gillet, and the parts of her `stmlib` it needs. MIT licensed: the notice is at the top of every Elements file, and in `stmlib/LICENSE`. Copied by `tools/vendor.sh`; do not edit by hand.

| Source | Commit |
| --- | --- |
| https://github.com/pichenettes/eurorack | `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026) |
| https://github.com/pichenettes/stmlib (eurorack's `stmlib` submodule at that commit) | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` |

## Files

- `elements/dsp/`: all of it, including `fx/`.
- `elements/resources.cc`, `elements/resources.h`: lookup tables and exciter samples, made for 32 kHz.
- `elements/drivers/debug_pin.h`: included by `dsp/resonator.cc`; empty when built with `-DTEST`.
- `stmlib/`: `LICENSE`, `stmlib.h`, `dsp/{cosine_oscillator,delay_line,dsp,filter,parameter_interpolator,units}.h`, `dsp/units.cc`, `utils/random.{h,cc}`.

Chamber compiles these with `-DTEST`, as upstream's own `elements/test/makefile` does, which selects the portable C++ paths in `stmlib/dsp/dsp.h` instead of the Cortex-M4 assembly.

## Local changes

One, applied by `tools/vendor.sh` from `tools/patches/`:

- `stmlib-random-thread-local.patch`: `stmlib::Random` keeps its state in one static variable, written every sample by the exciter (`dsp/exciter.cc`) and the string model (`dsp/string.cc`). On the hardware there is one voice; in Ingen two Chamber instances can run at the same moment on different threads and would race on it. The state is made `thread_local`, and Chamber saves and restores each instance's own state around every `Part::Process` call, so every instance draws its own repeatable sequence. The Makefile builds with `-ftls-model=initial-exec`, so the state lives in the static TLS block set up when the library is loaded: otherwise glibc allocates it with `malloc` the first time each host thread touches it, inside `run()` (found by `lv2lint`; the `allocation` test checks it). Values drawn are unchanged: a single instance starting from upstream's seed (`0x21`) produces the same numbers as the hardware.
