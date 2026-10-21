# Chamber

An LV2 modal synthesis voice, written for Peepo and other LV2 hosts. MIT
licensed, Copyright (c) 2026 Alejandro Bustos.

**Status, 21 October 2026: milestone 0.** The plugin builds, loads in jalv and
Ingen, and Play or a gate strikes a note. All ports are declared; the voice
still runs at the host's rate, so it is only in tune at 32 kHz until the
resampler (milestone 1). See
[docs/milestone-0-validation.md](docs/milestone-0-validation.md).

Chamber models a resonating body played by bowing, blowing or striking it.
Three exciters, shaped by one envelope, drive a resonator of up to 64 modes:
Geometry picks the kind of body (plates, bars, strings, tubes), Brightness,
Damping and Position shape its sound, and Space adds stereo width and reverb.
Play or a gate starts a note; V/oct sets its pitch. Two audio inputs let a
guitar excite the resonator directly, so Chamber also works as an effect.

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
`third_party/eurorack/` keep their copyright notices. Chamber is not
affiliated with or endorsed by Mutable Instruments, a registered trademark.
