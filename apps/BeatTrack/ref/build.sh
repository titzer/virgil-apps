#!/bin/bash
# Build the C++ reference for BeatTrack from the original beatmapper sources.
# Usage (from the repo root): apps/BeatTrack/ref/build.sh && apps/BeatTrack/ref/beattrack_ref inputs/BeatTrack-100bpm.wav <seconds>
HERE=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
BM_SRC=${BM_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
c++ -O2 -std=c++17 -ffp-contract=off -I"$BM_SRC" -I"$HERE/../../../dsp/ref" -o "$HERE/beattrack_ref" "$HERE/main.cpp"
