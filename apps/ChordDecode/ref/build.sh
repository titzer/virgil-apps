#!/bin/bash
# Builds the C++ reference for ChordDecode from the original beatmapper sources.
HERE=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
SRC=${BEATMAPPER_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
c++ -O2 -std=c++17 -ffp-contract=off -I"$SRC" -o "$HERE/chord_decode_ref" \
    "$HERE/main.cpp" "$SRC/beat_chroma.cpp" "$SRC/chroma_goertzel.cpp" || exit 1
echo "built $HERE/chord_decode_ref"
