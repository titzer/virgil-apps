#!/bin/bash
# Build the C++ reference for Spectrogram from the original beatmapper sources
# (GL and ImGui replaced by no-op stubs in ref/stubs).
# Usage (from the repo root): apps/Spectrogram/ref/build.sh && apps/Spectrogram/ref/spectrogram_ref $(cat apps/Spectrogram/args-test)
HERE=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
BM_SRC=${BM_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
c++ -O2 -std=c++17 -ffp-contract=off -I"$HERE/stubs" -I"$BM_SRC" -I"$HERE/../../../dsp/ref" -o "$HERE/spectrogram_ref" "$HERE/main.cpp"
