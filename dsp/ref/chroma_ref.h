// C++ twin of dsp/ChromaCommon.v3's ChromaBench harness: runs the ORIGINAL
// beatmapper chroma algorithms (via beat_chroma_interval and the CHROMA_ALGOS
// registry) over a SynthSong and prints the same output as the Virgil
// Chroma* benchmarks.  Usage from a ref/main.cpp:
//   chroma_ref_main(argc, argv, a4, {{"name", algo_idx}, ...});
// argv[1] = seconds, argv[2] (any) = dump per-beat vectors as "V b a v0..v11".
#pragma once
#include "song_file.h"
#include "beat_chroma.h"
#include "chroma_algo.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <utility>

static const char* CHROMA_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static int chroma_argmax(const float* v) {
    int best = 0;
    for (int i = 1; i < 12; i++) if (v[i] > v[best]) best = i;
    return best;
}
static int chroma_triad(const float* v) {
    int best = 0; float bestScore = -1.0f;
    for (int q = 0; q < 2; q++) {
        int third = q == 0 ? 4 : 3;
        for (int r = 0; r < 12; r++) {
            float s = v[r] + v[(r + third) % 12] + v[(r + 7) % 12];
            if (s > bestScore) { bestScore = s; best = r + 12 * q; }
        }
    }
    return best;
}
static void chroma_template(int chord, float* t) {
    for (int i = 0; i < 12; i++) t[i] = 0.0f;
    int r = chord % 12;
    t[r] = 1.0f; t[(r + (chord >= 12 ? 3 : 4)) % 12] = 1.0f; t[(r + 7) % 12] = 1.0f;
}
static void chroma_put_chord(int c) { printf("%s%s", CHROMA_NAMES[c % 12], c >= 12 ? "m" : ""); }

struct ChromaRefAlgo { const char* name; int idx; int minRoot; int minTriad; };

static int  g_chroma_passes = 1;
static int  g_chroma_mask = -1;
static bool g_chroma_dump = false;

static bool chroma_check(const char* name, const char* what, int count, int n, int pct) {
    bool ok = (long)count * 100 >= (long)pct * n;
    printf("check %s %s >= %d%%: %s (%d/%d)\n", name, what, pct, ok ? "PASS" : "FAIL", count, n);
    return ok;
}

static bool chroma_ref_run(SynthSong& s, const std::vector<ChromaRefAlgo>& all) {
    std::vector<ChromaRefAlgo> algos;
    for (size_t i = 0; i < all.size(); i++) if (g_chroma_mask & (1 << i)) algos.push_back(all[i]);
    bool dump = g_chroma_dump;
    int passes = g_chroma_passes;
    AudioPcm a = {s.pcm.data(), (uint64_t)s.frames, (uint32_t)s.channels, (uint32_t)s.sampleRate};
    BeatChromaParams p;
    beat_chroma_params_defaults(&p);
    p.attack_ms = 60.0f; p.attack_frac = 0.15f;
    int nb = (int)s.beatTimes.size(), na = (int)algos.size();
    std::vector<int> rootOk(na), triadOk(na);
    std::vector<double> cosSum(na);
    std::vector<uint32_t> hash(na);
    std::vector<float> vs(na * 12);
    std::vector<int> am(na), tr(na);
    int printed = 0;
    for (int pass = 0; pass < passes; pass++) for (int b = 0; b < nb - 1; b++) {
        int truth = s.chordAt[b];
        float tmpl[12]; chroma_template(truth, tmpl);
        bool miss = false;
        for (int k = 0; k < na; k++) {
            float* v = &vs[k * 12];
            p.algo_idx = algos[k].idx;
            beat_chroma_interval(a, s.beatTimes[b], s.beatTimes[b + 1], p, v);
            am[k] = chroma_argmax(v); tr[k] = chroma_triad(v);
            if (am[k] == truth % 12) rootOk[k]++;
            if (tr[k] == truth) triadOk[k]++; else miss = true;
            cosSum[k] += chroma_cosine(v, tmpl);
            hash[k] = hash[k] * 31u + (uint32_t)dsp_checksum(v, 0, 12);
        }
        if (pass > 0) continue;
        if (b < 8 || (miss && printed < 32) || dump) {
            printed++;
            printf("%d ", b); chroma_put_chord(truth); printf(":");
            for (int k = 0; k < na; k++) { printf(" %s/", CHROMA_NAMES[am[k]]); chroma_put_chord(tr[k]); }
            printf("\n");
        }
        if (dump) for (int k = 0; k < na; k++) {
            printf("V %d %d", b, k);
            for (int i = 0; i < 12; i++) printf(" %.6f", vs[k * 12 + i]);
            printf("\n");
        }
    }
    int n = (nb - 1) * passes;
    for (int k = 0; k < na; k++)
        printf("%s: root %d/%d triad %d/%d cos %.3f checksum %d\n", algos[k].name,
               rootOk[k], n, triadOk[k], n, cosSum[k] / n, (int32_t)hash[k]);
    bool ok = true;
    for (int k = 0; k < na; k++) {
        if (!chroma_check(algos[k].name, "root accuracy", rootOk[k], n, algos[k].minRoot)) ok = false;
        if (!chroma_check(algos[k].name, "triad accuracy", triadOk[k], n, algos[k].minTriad)) ok = false;
    }
    return ok;
}

static SynthSong chroma_ref_song(int argc, char** argv, double a4) {
    if (argc < 3) { fprintf(stderr, "usage: <clip.wav> <tenths> [passes] [mask] [dump]\n"); exit(1); }
    if (argc > 3) g_chroma_passes = atoi(argv[3]);
    if (argc > 4 && argv[4][0] != '-') g_chroma_mask = atoi(argv[4]);
    g_chroma_dump = argc > 5;
    SynthSong s;
    s.a4 = a4;
    s.seconds = atoi(argv[2]) / 10.0;
    song_load_tiled(s, argv[1]);
    return s;
}
