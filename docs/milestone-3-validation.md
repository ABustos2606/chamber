<!-- SPDX-License-Identifier: MIT -->
# Milestone 3 validation

**In progress.** Recorded 21 October 2026. Done on the PC: release `v0.2.0` prepared (plugin class, version, notices), and Peepo's side of plan section 9 written and checked against Peepo's own catalogue code. To do: commit and tag `v0.2.0`, move Peepo's pin to that commit, then the Pi checks and listening with a guitar (below).

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

## To do

1. Commit and tag `v0.2.0` in Chamber; push.
2. In Peepo, replace `05d9eb0` in `14-more-plugins.sh` with the `v0.2.0` commit.
3. On the Pi (Peepo's roadmap, Chamber section): the build step, `make test-timing` (Strings, then Ominous), `tools/pi/measure-effect.py` alone and in a typical patch for 30 minutes, with and without the Ominous voice; JACK xruns, DSP load and RSS into Peepo's `docs/evidence/`. Unhide `ominous` in the overlay only if it fits.
4. Pi catalogue exported; `docs/33-module-list.md` regenerated with `tools/document-catalogue.py`.
5. Listening with a guitar on the Pi: Damping (0.25 rings briefly; 0.7 rings on), Strike in level, then `v0.2.x` tags of tuned defaults.
