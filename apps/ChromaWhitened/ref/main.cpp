// Reference harness: runs the ORIGINAL beatmapper C++ chroma code on the same
// input clip as apps/ChromaWhitened and prints the same output.  See build.sh.
#include "chroma_ref.h"

extern double g_whitened_tune_sum;   // added to chroma_whitened.cpp by build.sh
extern int    g_whitened_tune_count;

int main(int argc, char** argv) {
    SynthSong s = chroma_ref_song(argc, argv, 443.0);
    bool ok = chroma_ref_run(s, { {"whitened", 7, 95, 95}, {"whitened_hps_peaks", 8, 95, 95} });
    double t = g_whitened_tune_count > 0 ? g_whitened_tune_sum / g_whitened_tune_count : 0.0;
    double cents = t * 100.0 / 3;
    printf("tuning: %.4f bins = %.2f cents (%d estimates)\n", t, cents, g_whitened_tune_count);
    bool tok = cents >= 3.0 && cents <= 20.0;
    printf("check tuning estimate in [3, 20] cents (true +11.76): %s\n", tok ? "PASS" : "FAIL");
    return ok && tok ? 0 : 1;
}
