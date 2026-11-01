#!/bin/bash
# Build the C++ reference for LyricAlign from the original beatmapper sources
# (lyric_align.cpp, unmodified) and the same synthetic vocal track.
# Usage: ref/build.sh && ref/lyricalign_ref <lines>
HERE=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
BM_SRC=${BM_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
c++ -O2 -std=c++17 -ffp-contract=off -I"$BM_SRC" \
    -o "$HERE/lyricalign_ref" "$HERE/main.cpp" "$BM_SRC/lyric_align.cpp"
