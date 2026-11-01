// Reference harness: runs the ORIGINAL beatmapper C++ chroma code on the same
// input clip as apps/ChromaFft and prints the same output.  See build.sh.
#include "chroma_ref.h"

int main(int argc, char** argv) {
    SynthSong s = chroma_ref_song(argc, argv, 440.0);
    bool ok = chroma_ref_run(s, { {"hps", 2, 95, 60}, {"peaks", 4, 95, 20}, {"hps_peaks", 6, 95, 60} });
    return ok ? 0 : 1;
}
