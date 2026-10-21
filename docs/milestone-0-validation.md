<!-- SPDX-License-Identifier: MIT -->
# Milestone 0 validation

Recorded 21 October 2026. The scaffold is built: vendored code, Makefile, LV2 glue around `elements::Part`, controls ported from `CvScaler::Read`, and Turtle with all 53 ports. These checks cover its ABI, build, lifecycle and the voice at a 32 kHz host rate. They do not cover the resampler (milestone 1) or performance on a Pi.

| Check | Result |
| --- | --- |
| `tools/vendor.sh` | eurorack `f4cfa769`, stmlib `e3bd7c9c` (eurorack's submodule at that commit); 35 files; LF; a second run gives identical files |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS; GCC 14.2, LV2 1.18.10; no warnings from Chamber's code |
| `make test SAN=1` | PASS; AddressSanitizer and UndefinedBehaviorSanitizer |
| `make test SAN=thread` | PASS (run under `setarch -R`: TSan cannot map memory with WSL's default ASLR) |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"` | PASS; Debian 12 root `/opt/peepo-arm64`, GCC 12.2, QEMU user emulation |
| `make install DESTDIR=build/stage PREFIX=/opt/peepo` | PASS; `chamber.so`, both Turtle files and `LICENSE` |
| `lv2info` on the staged bundle | PASS; 53 ports, latency reported by port 52 |
| `nm -D --defined-only chamber.so` | Only `lv2_descriptor` |
| `make test-hosts`, jalv 1.6.8 | Loads; silent until the Gate CV goes high, then strikes a note |
| `make test-hosts`, Peepo's Ingen, 1 and 4 threads, 1024- and 128-frame periods | Two Chambers in one graph; a gate from a CV source strikes notes; model, Ominous voice, coarse, geometry and output save and restore; no LV2 state saved |
| `lv2lint` 0.17.151 (`9661902c`, built from https://git.ventosus.ch/lv2lint/; online, ELF and X11 tests disabled) | No errors; one warning, below |

## What the offline tests cover

- `ttl_check`: indices, symbols and kinds match `src/ports.hpp`; the 53 symbols are frozen in order; names ASCII and at most 16 characters, with no upstream names; bounds and defaults; enumeration labels; toggles; the latency port; license; no required features.
- `scaffold`: one descriptor; invalid rates rejected; 32, 44.1, 48 and 96 kHz accepted; unconnected ports, unknown indices and zero-frame runs; silent until played; Play and Gate each strike a decaying note; latency port reports 16; identical output for whole periods, random slices and a 37-frame period, at 32 and 48 kHz; two instances keep their own random sequence alone, interleaved on one thread and on two threads; `activate` restarts the voice exactly; the three models and the Ominous voice sound and differ; NaN, infinite and huge inputs and controls leave the output finite; no `malloc` or `calloc` inside `run()` on a new thread.
- `match_elements`: at 32 kHz the plugin's output equals the vendored `Part` driven the way `elements.cc` drives it, bit for bit, one block late, for the modal, string and strings models with V/oct and Damping CV moving and Play pulsing.

## Found and fixed during the milestone

- `stmlib::Random`'s state is one static for the process. Made `thread_local` (`tools/patches/stmlib-random-thread-local.patch`); the glue keeps each instance's state. See `third_party/eurorack/README.md`.
- `lv2lint` first failed **Plugin Run**: `malloc` in `run()`. The `thread_local` Random state, in a `dlopen`ed library, is allocated on each thread's first use. Built with `-ftls-model=initial-exec` (static TLS, `STATIC_TLS` in the dynamic section); the `allocation` test counts `malloc` and `calloc` in `run()` on a new thread, and fails without the flag.
- `Part::Init` leaves `panic_` and `easter_egg_` unset and does not clear all voice state. `Part` lives in zeroed memory and `activate()` rebuilds it.

`lv2lint` warns **Plugin Author Email**: no `foaf:mbox` is given. The name and GitHub homepage are there; an email address is not invented or published (as Discipline).

## Not in this milestone

- **The voice runs at the host's rate.** At 48 kHz it is a fifth sharp and its envelopes and reverb run 1.5 times fast. The resampler is milestone 1.
- The CV gain is inferred, not measured (plan section 4.3, **tune**).
- `make test-timing` exists, but desktop and QEMU numbers say nothing about the Pi. Measured on x86-64 at the host rate (48 kHz, Strings, full reverb, every exciter): 1.2% load. The Pi measurement is milestone 1.

## Release

Tagged `v0.0.1` locally on the commit that adds this record. Not pushed.
