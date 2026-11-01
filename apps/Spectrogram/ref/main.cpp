// Reference harness for apps/Spectrogram: runs the ORIGINAL beatmapper
// spectrogram_compute_pixels() (src/spectrogram.cpp, GL/ImGui stubbed) on
// bit-identical input (the same tiled WAV clips, via dsp/ref/song_file.h) and
// prints exactly the same lines as Spectrogram.v3.
// Usage: spectrogram_ref <millis> <clip-a.wav> <clip-b.wav> <clip-c.wav>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <math.h>
#include <algorithm>
#include <vector>
#ifndef REF_SYSTEM_LIBM
// Use correctly-rounded float elementary functions (double libm rounded to
// float), as Virgil's DspMath does (float.roundd(cos(x)), log(x)/LN10, ...).
// macOS cosf is off by 1 ulp on some arguments, which perturbs the Hann window
// and hence a few pixels; with these the output is bit-identical to
// Spectrogram.v3.  Build with -DREF_SYSTEM_LIBM to use the platform's float libm.
static inline float ref_cosf(float x) { return (float)cos((double)x); }
static inline float ref_sinf(float x) { return (float)sin((double)x); }
static inline float ref_log10f(float x) { return (float)(log((double)x) / 2.30258509299404568402); }
static inline float ref_logf(float x) { return (float)log((double)x); }
static inline float ref_expf(float x) { return (float)exp((double)x); }
#define cosf ref_cosf
#define sinf ref_sinf
#define log10f ref_log10f
#define logf ref_logf
#define expf ref_expf
#endif
#include "spectrogram.cpp"
#include "synth_song.h"
#include "song_file.h"

static int32_t byte_sum(const uint8_t* a, size_t n) {
    uint32_t h = 0;
    for (size_t i = 0; i < n; i++) h = h * 31u + a[i];
    return (int32_t)h;
}
static void putd(double v, int digits) { printf("%.*f", digits, v); }
static int failures = 0;
static void check(const char* desc, bool ok) {
    printf("check %s: %s\n", desc, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

struct ClipSpec { double bpm, a4; int bpc; std::vector<int> roots; std::vector<bool> minors; };
static const ClipSpec CLIPS[3] = {
    {120.0, 440.0, 4, {0, 9, 5, 7}, {false, true, false, false}},
    {96.0, 440.0, 4, {2, 11, 7, 9}, {false, true, false, false}},
    {80.0, 437.0, 3, {7, 4, 0, 2}, {false, true, false, false}},
};

static void calibrate() {
    const int CAL_BIN = 93;
    int n = FFT_N + 3 * HOP, sr = 22050;
    std::vector<float> sig(n);
    double w = 6.28318530717958647692 * CAL_BIN / FFT_N;
    for (int i = 0; i < n; i++) sig[i] = (float)std::sin(w * i);
    SpectrogramPixels px;
    spectrogram_compute_pixels(&px, sig.data(), n, sr);
    int col = 0, peakBin = 0, peak = 0, far = 0;
    for (int bin = 0; bin < px.tex_h; bin++) {
        int v = px.intensity[(px.tex_h - 1 - bin) * px.tex_w + col];
        if (v > peak) { peak = v; peakBin = bin; }
        int d = bin - CAL_BIN;
        if ((d > 1 || d < -1) && v > far) far = v;
    }
    printf("calibration %dx%d peak-bin=%d peak=%d sidelobe-max=%d rgba-sum=%d\n", px.tex_w, px.tex_h,
           peakBin, peak, far, byte_sum(px.rgba, (size_t)px.tex_w * px.tex_h * 4));
    check("calibration peak at bin 93", peakBin == CAL_BIN);
    check("calibration peak intensity 236 +- 1 (-6.02 dB)", peak >= 235 && peak <= 237);
    check("calibration off-peak bins below -60 dB", far < 64);
    spectrogram_pixels_free(&px);
}

static double nearest_tone(SynthSong& song, double hz) {
    double best = 0.0, bestD = 1e9;
    for (size_t c = 0; c < song.roots.size(); c++) {
        int r = song.roots[c]; bool m = song.minors[c];
        int notes[4] = {60 + r, 60 + r + (m ? 3 : 4), 60 + r + 7, 36 + r};
        for (int n : notes) {
            double f = song.midiFreq(n);
            for (int hh = 1; hh <= 4; hh++) {
                double d = std::fabs(hz - f * hh);
                if (d < bestD) { bestD = d; best = f * hh; }
            }
        }
    }
    return best;
}

int main(int argc, char** argv) {
    if (argc < 5) { printf("Usage: Spectrogram <millis> <clip-a.wav> <clip-b.wav> <clip-c.wav>\n"); return 1; }
    calibrate();
    int remaining = atoi(argv[1]);
    uint32_t total = 0;
    for (int k = 0; remaining > 0; k++) {
        int ms = std::min(remaining, 20000);
        remaining -= ms;
        int ci = k % 3; const ClipSpec& spec = CLIPS[ci];
        SynthSong song;
        song.sampleRate = 22050;
        song.channels = 1;
        song.bpm = spec.bpm;
        song.a4 = spec.a4;
        song.beatsPerChord = spec.bpc;
        song.roots = spec.roots;
        song.minors = spec.minors;
        song.seconds = (double)ms / 1000.0;
        song_load_tiled(song, argv[2 + ci]);

        SpectrogramPixels px;
        if (!spectrogram_compute_pixels(&px, song.pcm.data(), song.frames, song.sampleRate)) { printf("failed\n"); return 1; }
        int w = px.tex_w, h = px.tex_h;
        int32_t rs = byte_sum(px.rgba, (size_t)w * h * 4), is = byte_sum(px.intensity, (size_t)w * h);
        printf("track %d bpm=%.0f %dx%d dur=%.2f\n", k, song.bpm, w, h, px.duration);
        printf("  rgba-sum: %d\n  inten-sum: %d\n", rs, is);
        int64_t totalI = 0, bestSum = -1, lowI = 0, highI = 0; int bestRow = 0, lowN = 0, highN = 0;
        double binHz = (double)px.sample_rate / FFT_N;
        for (int row = 0; row < h; row++) {
            int64_t s = 0;
            for (int col = 0; col < w; col++) s += px.intensity[row * w + col];
            totalI += s;
            int bin = (h - 1) - row;
            if (bin >= 2 && s > bestSum) { bestSum = s; bestRow = row; }
            double hz = bin * binHz;
            if (bin >= 2 && hz < 1000.0) { lowI += s; lowN++; }
            if (hz > 8000.0) { highI += s; highN++; }
        }
        int peakBin = (h - 1) - bestRow;
        double peakHz = (double)peakBin * binHz;
        double lowMean = (double)lowI / ((double)lowN * w), highMean = (double)highI / ((double)highN * w);
        printf("  mean-inten: "); putd((double)totalI / ((double)w * h), 3); printf("\n");
        printf("  band-means: "); putd(lowMean, 3); printf(" (<1k) "); putd(highMean, 3); printf(" (>8k)\n");
        double tone = nearest_tone(song, peakHz);
        printf("  peak-hz: "); putd(peakHz, 1); printf(" (nearest chord harmonic "); putd(tone, 1); printf(")\n");
        int expectW = std::min((song.frames - FFT_N) / HOP + 1, MAX_TEXW);
        check("width = STFT frames", w == expectW && h == BINS);
        double d = peakHz - tone;
        check("peak row within 1 bin of a chord-tone harmonic", d <= binHz && d >= -binHz);
        check("band <1 kHz brighter than >8 kHz", lowMean > highMean);
        total = total * 31u + (uint32_t)rs + (uint32_t)is;
        spectrogram_pixels_free(&px);
    }
    printf("total: %d\n", (int32_t)total);
    return failures == 0 ? 0 : 1;
}
