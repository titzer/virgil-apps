#!/bin/bash
# Builds the C++ reference for OnsetShape from the original beatmapper sources.
HERE=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
SRC=${BEATMAPPER_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
c++ -O2 -std=c++17 -ffp-contract=off -I"$SRC" -o "$HERE/onset_shape_ref" \
    "$HERE/main.cpp" "$SRC/onset_shape.cpp" "$SRC/beat_spectral_flux.cpp" || exit 1
echo "built $HERE/onset_shape_ref"
