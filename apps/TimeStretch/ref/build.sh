#!/bin/bash
# Build the C++ reference harness: beatmapper's ORIGINAL src/stretch.cpp
# (compiled unmodified, against stub miniaudio/Signalsmith headers) driven on
# the same input clip as the Virgil TimeStretch benchmark.  From the repo root:
#   apps/TimeStretch/ref/build.sh
#   apps/TimeStretch/ref/stretch_ref inputs/TimeStretch-song.wav 30 5
#   # compare with Virgil output dumps:
#   out/x86-64-darwin/TimeStretch.x86-64-darwin inputs/TimeStretch-song.wav 300 5 0 /tmp/v-
#   apps/TimeStretch/ref/stretch_ref inputs/TimeStretch-song.wav 300 5 0 /tmp/v-
set -e
HERE=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
BEATMAPPER_SRC=${BEATMAPPER_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
c++ -O2 -std=c++17 -ffp-contract=off -I"$HERE/stub" -I"$BEATMAPPER_SRC" -I"$HERE/../../../dsp/ref" \
    -o "$HERE/stretch_ref" "$HERE/main.cpp" "$BEATMAPPER_SRC/stretch.cpp"
