# Chamber

An LV2 modal synthesis voice, written for Peepo and other LV2 hosts. MIT
licensed, Copyright (c) 2026 Alejandro Bustos.

**Status, 21 October 2026: milestones 1 to 3 in progress; this is `v0.2.0`,
the first release Peepo builds.** `v0.0.1` is the
scaffold. Since then the voice runs at 32 kHz at any host rate, through
Chamber's own resampler (88 frames of latency at 48 kHz), every control and
CV input is tested, a guitar at Strike in rings each resonator model, and
changing the model or the Ominous voice no longer clicks. Peepo's build step,
inventory and overlay entry are written. The Pi CPU and xrun checks, including
whether the Ominous voice fits, and listening with a guitar are still to do.
See [docs/milestone-1-validation.md](docs/milestone-1-validation.md),
[docs/milestone-2-validation.md](docs/milestone-2-validation.md) and
[docs/milestone-3-validation.md](docs/milestone-3-validation.md).

Chamber models a resonating body played by bowing, blowing or striking it.
Three exciters, shaped by one envelope, drive a resonator of up to 64 modes:
Geometry picks the kind of body (plates, bars, strings, tubes), Brightness,
Damping and Position shape its sound, and Space adds stereo width and reverb.
Play or a gate starts a note; V/oct sets its pitch. A guitar at Strike in
excites the resonator directly, with no gate, so Chamber also works as an
effect; Blow in passes the exciter envelope and is heard while Play or a gate
is held.

| Plugin | URI |
| --- | --- |
| Chamber | `https://github.com/ABustos2606/chamber#chamber` |

Build on Linux with a C++17 compiler, GNU Make, pkg-config and LV2 headers
(`build-essential pkg-config lv2-dev` on Debian). The build needs no network:

```sh
make
make install PREFIX=/usr/local
make install LV2DIR=/opt/peepo/lib/lv2     # where Peepo finds plugins
make OPT="-O2 -mcpu=cortex-a76"            # native Pi build
make test                                  # offline tests; needs python3-rdflib
make test SAN=1                            # the same with ASan and UBSan
make test-hosts                            # jalv and Ingen; needs jackd, Ingen,
                                           # python3-jack-client and numpy
make test-timing                           # run() time, worst-case settings
make render                                # build/render, for listening:
build/render build/chamber.lv2/chamber.so out.wav --in guitar.wav \
    strike_level=0 model=2 geometry=0.6    # see tools/render.cpp
```

`tools/vendor.sh` refreshes `third_party/eurorack/` from upstream at the pinned
commits (by hand, with network access); the build never runs it.

The behaviour specification is [docs/design.md](docs/design.md); the build
and milestone requirements are in
[docs/implementation-plan.md](docs/implementation-plan.md). Do not rename
plugin URIs or port symbols after patches use them. Peepo's catalogue overlay,
pinned build integration and hardware evidence belong in Peepo's repository.

## Credits

Chamber's sound engine is the code of Elements, a eurorack module designed by
Emilie Gillet, published under the MIT license at
https://github.com/pichenettes/eurorack. The vendored files in
`third_party/eurorack/` keep their copyright notices, and `make install`
puts that notice in the bundle as `THIRD_PARTY_NOTICES`. Chamber is not
affiliated with or endorsed by Mutable Instruments, a registered trademark.
