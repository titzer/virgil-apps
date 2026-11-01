// Reference harness for apps/BeatTrack: runs the ORIGINAL beatmapper
// beat_spectral_flux.cpp on bit-identical input (the same tiled WAV clip, via
// dsp/ref/song_file.h) and prints exactly the same lines as BeatTrack.v3.
// The original .cpp is #included so its static helpers (compute_flux) are reachable.
// Usage: beattrack_ref <clip.wav> <seconds>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <math.h>
#ifndef REF_SYSTEM_LIBM
// Use correctly-rounded float elementary functions (double libm, rounded to
// float), as Virgil's DspMath does (float.roundd(cos(x)) etc.).  macOS cosf
// is off by 1 ulp on some arguments, which perturbs the Hann window; with
// these the reference output is bit-identical to BeatTrack.v3.  Build with
// -DREF_SYSTEM_LIBM to use the platform's float libm instead.
static inline float ref_cosf(float x) { return (float)cos((double)x); }
static inline float ref_sinf(float x) { return (float)sin((double)x); }
static inline float ref_logf(float x) { return (float)log((double)x); }
static inline float ref_expf(float x) { return (float)exp((double)x); }
static inline float ref_log2f(float x) { return (float)(log((double)x) / 0.69314718055994530942); }
#define cosf ref_cosf
#define sinf ref_sinf
#define logf ref_logf
#define expf ref_expf
#define log2f ref_log2f
#endif
#include "beat_spectral_flux.cpp"
#include "synth_song.h"
#include "song_file.h"

// As DspOut.checksum, but rounding half-to-even like Virgil's double.round.
// (dsp/ref/synth_song.h's dsp_checksum uses std::round, half-away-from-zero,
// which differs from Virgil on exact .5 ties.)
static int32_t checksum(const float* a, int start, int len) {
    uint32_t h = 0;
    for (int i = start; i < start + len; i++) h = h * 31u + (uint32_t)(int32_t)std::nearbyint((double)a[i] * 10000.0);
    return (int32_t)h;
}
static void putd(double v, int digits) { printf("%.*f", digits, v); }
static int failures = 0;
static void check(const char* desc, bool ok) {
    printf("check %s: %s\n", desc, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}
static double nearest_offset(double t, const std::vector<double>& gt) {
    double best = 1e9;
    for (double g : gt) { double d = t - g; if (d * d < best * best) best = d; }
    return best;
}
static float median_offset(const double* times, int n, const std::vector<double>& gt) {
    if (n == 0) return 0.0f;
    std::vector<float> offs(n);
    for (int i = 0; i < n; i++) offs[i] = (float)nearest_offset(times[i], gt);
    return array_median(offs.data(), n);
}
static int count_on_grid(const double* times, int n, const std::vector<double>& gt, double off) {
    int m = 0;
    for (int i = 0; i < n; i++) { double d = nearest_offset(times[i] - off, gt); if (d < 0.035 && d > -0.035) m++; }
    return m;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("Usage: beattrack_ref <clip.wav> <seconds>\n"); return 1; }
    int seconds = atoi(argv[2]);
    SynthSong song;
    song.sampleRate = 22050;
    song.channels = 1;
    song.bpm = 100.0;
    song.roots = {0, 9, 5, 7, 2};
    song.minors = {false, true, false, false, true};
    song.seconds = seconds;
    song_load_tiled(song, argv[1]);

    // Intermediate ODFs and tau (recomputed here exactly as beat_spectral_flux does).
    int n = 0; float* low = nullptr;
    float* flux = compute_flux(song.pcm.data(), song.frames, song.channels, song.sampleRate,
                               0.0, seconds, &n, &low);
    float tau = estimate_period(flux, low, n, 60.0f, 200.0f, song.sampleRate);

    BeatAlgoParams params = {60.0f, 200.0f, 1.5f, 50.0f, 30.0f, nullptr, 0};
    static AutoBeatList out;
    beat_spectral_flux(song.pcm.data(), song.frames, song.channels, song.sampleRate,
                       0.0, seconds, &params, &out);
    const std::vector<double>& gt = song.beatTimes;

    printf("odf-frames: %d\n", n);
    printf("flux-sum: %d\n", checksum(flux, 0, n));
    printf("low-sum: %d\n", checksum(low, 0, n));
    double fsum = 0, lsum = 0;
    for (int i = 0; i < n; i++) { fsum += flux[i]; lsum += low[i]; }
    printf("flux-total: "); putd(fsum, 3); printf("\n");
    printf("low-total: "); putd(lsum, 3); printf("\n");
    printf("tau: "); putd(tau, 2); printf("\n");
    printf("bpm: "); putd(out.estimated_bpm, 2); printf("\n");
    printf("onsets: %d\n", out.onset_count);
    std::vector<float> ot(out.onset_count);
    for (int i = 0; i < out.onset_count; i++) ot[i] = (float)out.onset_times[i];
    printf("onset-sum: %d\n", checksum(ot.data(), 0, out.onset_count));
    printf("beats: %d\n", out.beat_count);
    std::vector<float> bt(out.beat_count);
    for (int i = 0; i < out.beat_count; i++) bt[i] = (float)out.beat_times[i];
    printf("beat-sum: %d\n", checksum(bt.data(), 0, out.beat_count));
    printf("first: ");
    for (int i = 0; i < out.beat_count && i < 8; i++) { if (i > 0) printf(" "); putd(out.beat_times[i], 3); }
    printf("\n");
    float ibi = 0.0f;
    if (out.beat_count >= 2) {
        std::vector<float> ibis(out.beat_count - 1);
        for (int i = 0; i + 1 < out.beat_count; i++) ibis[i] = (float)(out.beat_times[i + 1] - out.beat_times[i]);
        ibi = array_median(ibis.data(), (int)ibis.size());
    }
    printf("median-ibi: "); putd(ibi, 4); printf("\n");

    float bOff = median_offset(out.beat_times, out.beat_count, gt);
    int bOn = count_on_grid(out.beat_times, out.beat_count, gt, bOff);
    float oOff = median_offset(out.onset_times, out.onset_count, gt);
    int oOn = count_on_grid(out.onset_times, out.onset_count, gt, oOff);
    double period = 60.0 / song.bpm;
    printf("true-bpm: "); putd(song.bpm, 2); printf("\n");
    printf("true-beats: %zu\n", gt.size());
    printf("beat-offset-ms: "); putd(1000.0 * bOff, 1); printf("\n");
    printf("beats-on-grid: %d/%d\n", bOn, out.beat_count);
    printf("onset-offset-ms: "); putd(1000.0 * oOff, 1); printf("\n");
    printf("onsets-on-grid: %d/%d\n", oOn, out.onset_count);

    if (gt.size() < 16) {
        printf("checks skipped: input shorter than 16 beats\n");
        return 0;
    }
    double bpmErr = out.estimated_bpm - song.bpm;
    check("bpm within 1 of true", bpmErr <= 1.0 && bpmErr >= -1.0);
    int cntErr = out.beat_count - std::min((int)gt.size(), MAX_BEAT_CANDS);
    check("beat count within 2 of min(true, 2048)", cntErr <= 2 && cntErr >= -2);
    double ibiErr = (ibi - period) / period;
    check("median ibi within 2% of true period", ibiErr <= 0.02 && ibiErr >= -0.02);
    check("beat latency within 100 ms", bOff <= 0.1 && bOff >= -0.1);
    check("beats on grid >= 95%", bOn * 100 >= 95 * out.beat_count && out.beat_count > 0);
    check("onsets on grid >= 90%", oOn * 100 >= 90 * out.onset_count && out.onset_count > 0);
    free(flux); free(low);
    return failures == 0 ? 0 : 1;
}
