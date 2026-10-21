<!-- SPDX-License-Identifier: MIT -->
# Milestone 3 validation

**In progress.** Recorded 21 October 2026. Done: release `v0.2.0` (plugin class, version, notices), Peepo's side of plan section 9, installation on the Pi, the user's guitar test (passed), and `v0.2.1` with guitar-friendly defaults. To do: tags, the pin, and the Pi checks that need a sound power supply (below).

## Chamber, release `v0.2.0`

| Change | Why |
| --- | --- |
| `lv2:GeneratorPlugin` instead of `lv2:InstrumentPlugin` | Peepo's exporter withheld Chamber as "Played by MIDI notes; not yet checked with a MIDI controller", which the overlay cannot undo. Chamber has no MIDI input. `ttl_check` now requires the generator class. No URI or port changed, so saved patches load unchanged |
| `lv2:minorVersion 2`, `lv2:microVersion 0` | `v0.2.0`, decided with the user; `v0.1.0` is skipped |
| `THIRD_PARTY_NOTICES`, installed in the bundle | `chamber.so` contains Emilie Gillet's MIT code, and the bundle carried only Chamber's own `LICENSE`. MIT asks for her notice in every copy |

| Check | Result |
| --- | --- |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"`, Debian 12 root under QEMU | PASS |
| `make test-hosts`, jalv and Ingen 1 and 4 threads, 128-frame period | PASS |
| `lv2lint` 0.17.151 | No errors; author-email warning as before |
| Staged `make install` | `chamber.so`, `manifest.ttl`, `chamber.ttl`, `LICENSE`, `THIRD_PARTY_NOTICES` |

No DSP code changed since milestone 2 (`05d9eb0`); `match_elements` and the other renders are unchanged.

## Peepo

Edited in `C:\Pedal PI`, not committed:

| File | Change |
| --- | --- |
| `tools/ingen-spike/14-more-plugins.sh` | `third chamber ... make -j"$JOBS" OPT="$DISCIPLINE_OPT" LV2DIR="$LV2DIR" install`, after Loo-pi. Pinned at `05d9eb0` (milestone 2), which still declares the instrument class: **move it to the `v0.2.0` commit** |
| `config/catalogue/inventory.txt`, `tools/catalogue-inventory.py` | Chamber's URI among Peepo's own plugins. `inventory.txt` edited by hand: it already lists midi-cv and midi-pc, which the script lacks, so regenerating it would drop them |
| `config/catalogue/overlay.json` | Name Chamber, category Resonator, a description, `"output": "output"`, `ominous` hidden, eight pages |
| `THIRD_PARTY_NOTICES.md` | A row for Chamber and its vendored notice |
| `docs/38-eurorack-ports-roadmap.md` | Chamber's checklist: what is done, what waits for the Pi |

Checks, in Peepo's development environment under WSL:

| Check | Result |
| --- | --- |
| The build step's commands by hand: clone from GitHub, check out `05d9eb0`, `make install` into `/opt/peepo/lib/lv2` | Builds with no warnings; `ldd` clean |
| `peepo_catalogue_export --only config/catalogue/inventory.txt`, with `05d9eb0` installed | Chamber **withheld**: "Played by MIDI notes" (the instrument class) |
| The same with the `v0.2.0` tree installed | Chamber offered: Sound source by class, Resonator by the overlay; 18 ports, 34 controls |
| `peepo_catalogue_tests` on the committed catalogue plus Chamber's exported entry, with the new overlay | PASS (the overlay is refused if a page names a control Chamber does not show, and every offered module needs a written ASCII description) |
| The overlay as Peepo applies it | Chamber, Resonator, Output is Chamber's own `output`; 33 shown controls, every one in a page; `ominous` and `latency` not shown |

The pages, as Peepo shows them:

| Page | Controls |
| --- | --- |
| Resonator | Model, Geometry, Brightness, Damping, Position, Space |
| Input and output | Strike in level, Blow in level, Output |
| Play | Play, Strength, Contour |
| Strike | Strike level, Strike mallet, Strike timbre |
| Bow and blow | Bow level, Bow timbre, Blow level, Blow flow, Blow timbre |
| Pitch | Coarse, Fine, FM amount |
| Exciter CV amounts | Bow timbre amt, Blow flow amt, Blow timbre amt, Strk mallet amt, Strk timbre amt |
| Resonator CV amounts | Geometry amt, Brightness amt, Damping amt, Position amt, Space amt |

Not done on the PC: `config/catalogue/wsl-debian.json` was not re-exported. This WSL environment no longer matches it (about 110 of its plugins are not installed here, and many entries differ), so a fresh export would rewrite far more than Chamber. Run `15-compat.sh` after a full `14-more-plugins.sh`, as for any new plugin.

## On the Pi, 21 October 2026

Chamber `b15b3dc` (Milestone 3, not tagged) on the Pi 5 (`patchbox`, Debian 12, kernel 6.6.74), built as Peepo's build step does: cloned from GitHub into `/opt/peepo/src/more-plugins/chamber`, `make OPT="-O2 -mcpu=cortex-a76"` (4 s with `-j3`, no warnings), installed into `/opt/peepo/lib/lv2`. Peepo's pin moved to `b15b3dc`.

| Check | Result |
| --- | --- |
| `make test` on the Pi | PASS, every test, natively on arm64 |
| `make test-timing`, three runs, worst-case settings, 48 kHz, 128 frames (2,667 us) | Strings: mean 77 to 80 us (2.9 to 3.0% of a core), 99.9% 303 to 455 us. Ominous: mean 132 to 140 us (4.9 to 5.3%), 99.9% 418 to 556 us. One run of each had a single period near or over 2.67 ms (2.8 and 3.4 ms). The test is an ordinary process beside the running instrument, not a JACK client at real-time priority, so those spikes say nothing yet about xruns |
| Catalogue | The Pi's overlay and inventory were Peepo's `66e4330`; they now include exactly Chamber's lines from `588d6f2`. Re-exported with `tools/pi/export-catalogue.sh`: 335 -> 336 modules, Chamber added, nothing else changed. Chamber offered, category Resonator. Backups in `/opt/peepo/share/peepo/pre-chamber-20261021/` |
| Engine and interface restarted (`peepo-engine`, `peepo-route`, `peepo-ui`; JACK kept running) | All active; the saved startup patch reloaded clean (`dirty=0`); guitar and speakers wired back; the installed catalogue and overlay load (`peepo_effect_live show`: PASS) |
| xruns | Before any of this, with only the Clouds patch: 489 in 70 minutes (4 to 12 a minute); the Pi reports past under-voltage and throttling (`get_throttled` 0x50000). 54 in the minute of the build and timing test. After the restart, 2 to 4 a minute. Chamber is in no patch yet |

Not done on the Pi: Chamber in a patch with guitar, `measure-effect.py`, a 30-minute xrun run with and without the Ominous voice, listening. The mean costs (3% and 5% of a core) leave room; whether the Ominous voice is unhidden waits for the xrun run, on a Pi without the under-voltage.

## Listening and defaults, `v0.2.1`

The user played Chamber with a guitar on the Pi (21 October 2026), in a showcase patch of three Chambers, one per model, built for the purpose (`Chamber showcase` on the Pi). **Their test passed**; their favourite is the Modal "Bells" voice. Findings while playing:

- The Strings voice rang on the guitar notes it is tuned to (E and B), which on the user's guitar, tuned three semitones down, are the G and D shapes. Offline, per note from E2 to E4: Strings rings within 3 dB on E and B, 16 to 20 dB lower on G and D.
- Every click heard was a JACK xrun, and every xrun came within 4 ms of the kernel's "Undervoltage detected!" (20 of 20 in 15 minutes): the Pi's power supply, not Chamber. It clicked as often before Chamber was installed.

So `v0.2.1` makes a new Chamber that Bells voice (design, Decisions; plan section 3): every exciter off, Geometry 0.72, Brightness 0.68, Damping 0.82, Space 0.88, Coarse 33 (E), Strike in level -9 dB. Saved patches keep their own values.

| Check | Result |
| --- | --- |
| `defaults` test | silent alone; Play and a gate alone silent; guitar E notes into the E bell peak at 0.82, and 0.5 to 1.5 s after the guitar is muted it rings at -8.6 dBFS |
| `ttl_check` | Turtle defaults, C++ fallbacks and the test host's `plugin_defaults()` agree for every control |
| x86-64 `make test`; `make test-hosts` (jalv and Ingen raise Strike level for their gate checks); `lv2lint` | PASS; no errors |
| `make test SAN=1`, `SAN=thread` | PASS |
| arm64 under QEMU | PASS |

The voice tests still use Elements' panel values (`tests/host.cpp`), so `match_elements`, `pitch` and the rest are unchanged.

## To do

1. Committed and pushed as `b15b3dc`; the `v0.2.0` tag is not created yet.
2. Done: Peepo pins `b15b3dc` (uncommitted in Peepo). If a `v0.2.0` tag lands on another commit, move the pin there.
3. On the Pi (Peepo's roadmap, Chamber section): the build step, `make test-timing` (Strings, then Ominous), `tools/pi/measure-effect.py` alone and in a typical patch for 30 minutes, with and without the Ominous voice; JACK xruns, DSP load and RSS into Peepo's `docs/evidence/`. Unhide `ominous` in the overlay only if it fits.
4. Pi catalogue exported; `docs/33-module-list.md` regenerated with `tools/document-catalogue.py`.
5. Done: listened to with a guitar on the Pi; defaults tuned in `v0.2.1` (above). Tag `v0.2.1` and move Peepo's pin to it; then install it on the Pi.
