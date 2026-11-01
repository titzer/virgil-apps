#!/bin/bash
# Regenerates the synthetic song clips in inputs/ listed in inputs/songs.list.
# Each non-comment line: <file.wav> [key=value ...]  (see dsp/gen/GenSong.v3)
set -e
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TMP=${TMPDIR:-/tmp}/$USER-gensong; mkdir -p "$TMP"
if [ -z "$V3C" ]; then
    for t in $("$ROOT/bin/sense_host"); do
        if command -v "v3c-$t" > /dev/null; then V3C="v3c-$t"; break; fi
    done
fi
$V3C -output="$TMP" -program-name=GenSong "$ROOT/dsp/gen/GenSong.v3" "$ROOT/dsp/DspMath.v3" \
    "$ROOT/dsp/Synth.v3" "$ROOT/dsp/SongFile.v3" "$ROOT/lib/math/Math.v3"
grep -v '^\s*#' "$ROOT/inputs/songs.list" | grep -v '^\s*$' | while read -r file rest; do
    "$TMP/GenSong" "$ROOT/inputs/$file" $rest
done
