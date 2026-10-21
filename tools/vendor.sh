#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos
#
# Copy the upstream files Chamber needs into third_party/eurorack/, at the
# pinned commits. Run by hand (it needs network access); the build never runs
# it. Usage: tools/vendor.sh [scratch-dir]
set -eu

EURORACK_URL=https://github.com/pichenettes/eurorack.git
EURORACK_COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
# The stmlib submodule commit recorded in eurorack's tree at EURORACK_COMMIT.
STMLIB_URL=https://github.com/pichenettes/stmlib.git
STMLIB_COMMIT=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec

# Elements' dsp/ and resources, and the stmlib files they include
# (elements/drivers/debug_pin.h is included by dsp/resonator.cc; with -DTEST
# it is empty).
ELEMENTS_FILES="
elements/drivers/debug_pin.h
elements/dsp/dsp.h
elements/dsp/exciter.cc
elements/dsp/exciter.h
elements/dsp/fx/diffuser.h
elements/dsp/fx/fx_engine.h
elements/dsp/fx/reverb.h
elements/dsp/multistage_envelope.cc
elements/dsp/multistage_envelope.h
elements/dsp/ominous_voice.cc
elements/dsp/ominous_voice.h
elements/dsp/part.cc
elements/dsp/part.h
elements/dsp/patch.h
elements/dsp/resonator.cc
elements/dsp/resonator.h
elements/dsp/string.cc
elements/dsp/string.h
elements/dsp/tube.cc
elements/dsp/tube.h
elements/dsp/voice.cc
elements/dsp/voice.h
elements/resources.cc
elements/resources.h
"
STMLIB_FILES="
LICENSE
stmlib.h
dsp/cosine_oscillator.h
dsp/delay_line.h
dsp/dsp.h
dsp/filter.h
dsp/parameter_interpolator.h
dsp/units.cc
dsp/units.h
utils/random.cc
utils/random.h
"

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=${1:-$ROOT/tmp/vendor}
DEST=$ROOT/third_party/eurorack

fetch() { # url commit dir
    rm -rf "$3"; mkdir -p "$3"
    git -C "$3" init -q
    git -C "$3" fetch -q --depth 1 "$1" "$2"
    git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

fetch "$EURORACK_URL" "$EURORACK_COMMIT" "$SCRATCH/eurorack"
fetch "$STMLIB_URL" "$STMLIB_COMMIT" "$SCRATCH/stmlib"
test "$(git -C "$SCRATCH/eurorack" ls-tree HEAD stmlib | awk '{print $3}')" = "$STMLIB_COMMIT" ||
    { echo "stmlib commit does not match eurorack's submodule" >&2; exit 1; }

rm -rf "$DEST/elements" "$DEST/stmlib"
for f in $ELEMENTS_FILES; do
    mkdir -p "$DEST/$(dirname "$f")"; cp "$SCRATCH/eurorack/$f" "$DEST/$f"
done
for f in $STMLIB_FILES; do
    mkdir -p "$DEST/stmlib/$(dirname "$f")"; cp "$SCRATCH/stmlib/$f" "$DEST/stmlib/$f"
done
# Chamber's local changes, listed in third_party/eurorack/README.md.
for p in "$ROOT"/tools/patches/*.patch; do
    patch -s -p1 -d "$DEST" < "$p"
done
echo "vendored eurorack $EURORACK_COMMIT and stmlib $STMLIB_COMMIT into $DEST"
