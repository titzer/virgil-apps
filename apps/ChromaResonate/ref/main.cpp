// Reference harness: runs the ORIGINAL beatmapper C++ chroma code on the same
// input clip as apps/ChromaResonate and prints the same output.  See build.sh.
#include "chroma_ref.h"

int main(int argc, char** argv) {
    SynthSong s = chroma_ref_song(argc, argv, 440.0);
    bool ok = chroma_ref_run(s, { {"resonate", 5, 95, 95} });
    return ok ? 0 : 1;
}
