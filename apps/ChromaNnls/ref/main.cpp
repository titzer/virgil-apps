// Reference harness: runs the ORIGINAL beatmapper C++ chroma code on the same
// input clip as apps/ChromaNnls and prints the same output.  See build.sh.
#include "chroma_ref.h"

int main(int argc, char** argv) {
    SynthSong s = chroma_ref_song(argc, argv, 440.0);
    bool ok = chroma_ref_run(s, { {"nnls", 3, 95, 95} });
    return ok ? 0 : 1;
}
