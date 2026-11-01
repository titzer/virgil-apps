#!/bin/bash
# Builds the C++ reference from the original beatmapper sources and, if the
# Virgil binary has been compiled, compares the two on the args-test, args-tiny
# and args-small inputs: (1) a full diff of the normal output, (2) chroma vectors
# within a tolerance and per-beat argmax/triad exactly (dump mode, first pass).
# BEATMAPPER_SRC overrides the beatmapper source location.
set -e
HERE=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
APP=$(basename $(dirname $HERE))
ROOT=$(cd $HERE/../../.. && pwd)
SRC=${BEATMAPPER_SRC:-$HOME/RiffHound/RiffHound/beatmapper/src}
TMP=${TMPDIR:-/tmp}/chroma-ref-$APP
mkdir -p $TMP
# Expose the whitened chroma's per-call tuning estimates (only change to the original).
sed -e 's/^static const int WL_N /double g_whitened_tune_sum = 0.0; int g_whitened_tune_count = 0; static const int WL_N /' \
    -e 's/^\(        if (tune < -1.5f) tune += 3.0f;\)/\1 g_whitened_tune_sum += tune; g_whitened_tune_count++;/' \
    $SRC/chroma_whitened.cpp > $TMP/chroma_whitened.cpp
FILES="$SRC/chroma_algo.cpp $SRC/beat_chroma.cpp $SRC/chroma_goertzel.cpp $SRC/chroma_hps.cpp \
  $SRC/chroma_peaks.cpp $SRC/chroma_nnls.cpp $SRC/chroma_resonate.cpp $TMP/chroma_whitened.cpp"
c++ -O2 -std=c++17 -I$SRC -I$ROOT/dsp/ref -o $TMP/ref $HERE/main.cpp $FILES
echo "built $TMP/ref"
VIRGIL=$ROOT/out/x86-64-darwin/$APP.x86-64-darwin
if [ -x "$VIRGIL" ]; then
    cd $ROOT   # args files hold inputs/... paths relative to the repo root
    for size in test tiny small; do
        args=($(cat apps/$APP/args-$size))
        clip=${args[0]} tenths=${args[1]} passes=${args[2]:-1} mask=${args[3]:--1}
        echo "== $APP args-$size (${args[*]})"
        $VIRGIL ${args[*]} > $TMP/virgil-$size.txt || true
        $TMP/ref ${args[*]} > $TMP/ref-$size.txt || true
        if diff $TMP/virgil-$size.txt $TMP/ref-$size.txt > $TMP/diff-$size.txt; then
            echo "full output identical ($(wc -l < $TMP/virgil-$size.txt) lines)"
        else
            echo "full output differs only in:"; grep '^[<>]' $TMP/diff-$size.txt | sed 's/^/  /'
        fi
        $VIRGIL $clip $tenths 1 $mask dump > $TMP/virgil-$size.out || true
        $TMP/ref $clip $tenths 1 $mask dump > $TMP/ref-$size.out || true
        python3 $ROOT/dsp/ref/chroma_compare.py $TMP/virgil-$size.out $TMP/ref-$size.out || true
    done
fi
