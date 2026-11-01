// Reference harness for apps/ChordDecode: runs the ORIGINAL beatmapper chord
// decoder (verbatim extract of complete_algo.cpp in chord_path.inc) on beat
// chroma from the ORIGINAL beat_chroma.cpp + chroma_goertzel.cpp, over the same
// synthetic song as ChordDecode.v3, and prints the same summary.
// Build: ./build.sh   Run (from the repo root): apps/ChordDecode/ref/chord_decode_ref $(cat apps/ChordDecode/args-test)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include "complete_algo.h"
#include "chroma_algo.h"
#include "chord_model.h"
#include "../../../dsp/ref/song_file.h"

// Stand-in for chroma_algo.cpp's registry: only Goertzel/Hann (index 0).
const ChromaAlgoDesc CHROMA_ALGOS[] = { { "Goertzel / Hann", "", chroma_goertzel_hann } };
const int CHROMA_ALGO_COUNT = 1;

#include "chord_path.inc"


struct DecodeRun {
    const char* name;
    std::vector<ChordModel> models;
    CompleteParams params;
    const MiscMap* cm;
    int i0, i1;
    std::vector<int> labels;
    double scoreSum = 0;
    int32_t labelHash = 0;
    int decodedBeats = 0;
};

static void run_reps(DecodeRun& r, const BeatMap* bm, const BeatChromaCache* cache, int reps) {
    CompleteInputs in = {};
    in.beatmap = bm; in.chordmap = r.cm;
    int shifts = std::max(1, std::min(16, (r.i1 - r.i0) / 2));
    std::vector<int> tmp;
    for (int rep = 0; rep < reps; rep++) {
        int start = r.i0 + rep % shifts;
        decode_chords(in, r.params, cache, r.models, start, r.i1, 4, 0, &tmp);
        float sc = (start < r.i1 && !r.models.empty()) ? g_ref_best_score : 0.0f;
        r.scoreSum += sc;
        for (int i = 0; i < r.i1 - start; i++) r.labelHash = (int32_t)((uint32_t)r.labelHash * 31u + (uint32_t)tmp[i]);
        r.decodedBeats += r.i1 - start;
        if (rep == 0) r.labels = tmp;
    }
}

static int report(const DecodeRun& r, const std::vector<int>& truth) {
    int n = r.i1 - r.i0, correct = 0;
    for (int i = 0; i < n; i++) {
        int root; bool minor;
        if (parse_chord(r.models[r.labels[i]].text, &root, &minor) && root + (minor ? 12 : 0) == truth[r.i0 + i]) correct++;
    }
    std::vector<float> mv(r.models.size() * 12);
    for (size_t m = 0; m < r.models.size(); m++) for (int c = 0; c < 12; c++) mv[m * 12 + c] = r.models[m].v[c];
    printf("%s: models=%d beats=%d..%d correct=%d/%d\n", r.name, (int)r.models.size(), r.i0, r.i1, correct, n);
    printf("  models: %d transition=%.4f decoded=%d\n", dsp_checksum(mv.data(), 0, (int)mv.size()), r.params.chord_transition, r.decodedBeats);
    printf("  score sum: %.3f path hash: %d\n", r.scoreSum, r.labelHash);
    printf("  chart:");
    int k = 0, runs = 0;
    while (k < n) {
        int e = k;
        while (e < n && r.labels[e] == r.labels[k]) e++;
        if (runs < 12) printf(" %s/%d", r.models[r.labels[k]].text, e - k);
        runs++;
        k = e;
    }
    printf(" (%d runs)\n", runs);
    return correct;
}

static std::string chord_name(int root, bool minor) { return std::string(ROOT_NAMES[root]) + (minor ? "m" : ""); }

int main(int argc, char** argv) {
    if (argc < 4) { printf("Usage: chord_decode_ref <clip.wav> <seconds> <reps> [t]\n"); return 1; }
    int reps = atoi(argv[3]);
    bool timing = argc > 4;
    auto now = [] { return std::chrono::steady_clock::now(); };
    auto tl = now();
    SynthSong song; song.seconds = atoi(argv[2]);
    song.bpm = 240; song.beatsPerChord = 2; song.seed = 4242;   // the clip's parameters
    song.roots = {0, 9, 5, 7, 2, 4};
    song.minors = {false, true, false, false, true, true};
    song_load_tiled(song, argv[1]);
    AudioPcm a = { song.pcm.data(), (uint64_t)song.frames, (uint32_t)song.channels, (uint32_t)song.sampleRate };
    int nb = (int)song.beatTimes.size(), n_int = nb - 1, bpc = song.beatsPerChord;
    std::vector<Beat> beats(nb);
    for (int i = 0; i < nb; i++) beats[i] = { song.beatTimes[i], false, false };
    BeatMap bm = {}; bm.beats = beats.data(); bm.count = nb; bm.capacity = nb;

    auto t0 = now();
    BeatChromaParams bcp; beat_chroma_params_defaults(&bcp);
    bcp.algo_idx = 0;   // Goertzel / Hann
    BeatChromaCache cache;
    beat_chroma_ensure(&cache, a, song.beatTimes.data(), nb, bcp);
    auto t1 = now();

    int mapped = std::min(2 * (int)song.roots.size(), (n_int + bpc) / (2 * bpc));
    std::vector<MiscAnnotation> ann(mapped);
    for (int c = 0; c < mapped; c++) {
        int b = c * bpc, ch = song.chordAt[b];
        memset(&ann[c], 0, sizeof(ann[c]));
        ann[c].t_start = song.beatTimes[b]; ann[c].t_end = song.beatTimes[b + bpc];
        snprintf(ann[c].text, sizeof(ann[c].text), "%s", chord_name(ch % 12, ch >= 12).c_str());
    }
    MiscMap cm = {}; cm.entries = ann.data(); cm.count = mapped; cm.capacity = mapped;
    MiscMap empty = {};

    // The chord fields of complete_params_defaults
    CompleteParams p = {};
    p.chord_transition = 0.10f; p.chord_measure_bonus = 0.75f; p.chord_unseen_penalty = 0.10f;
    p.chord_prior_beats = 4.0f; p.chord_learn_rate = true; p.chord_external = false;
    p.chord_external_blend = 0.1f; p.chord_external_blend_cold = 0.25f;
    p.chord_corpus_weight = 0.0f; p.chord_corpus_weight_cold = 0.2f; p.chord_key_bonus = 0.05f;

    CompleteInputs in = {};
    in.beatmap = &bm; in.chordmap = &cm;
    DecodeRun warm; warm.name = "warm"; warm.cm = &cm;
    build_chord_models(in, p, &cache, &warm.models);
    warm.params = p;
    warm.params.chord_external_blend = 0.0f;   // no external recogniser
    if (p.chord_learn_rate && cm.count >= 3) {   // as chords_from_models
        std::vector<double> lens;
        for (int i = 0; i < cm.count; i++) {
            int bs = nearest_beat(&bm, cm.entries[i].t_start), be = nearest_beat(&bm, cm.entries[i].t_end);
            if (be > bs) lens.push_back(be - bs);
        }
        if (!lens.empty()) {
            std::sort(lens.begin(), lens.end());
            double med = lens[lens.size() / 2];
            warm.params.chord_transition = p.chord_transition * (float)(med / 4.0);
        }
    }
    int first_free = n_int;
    for (int i = 0; i < n_int; i++) {
        double b0 = beats[i].time, b1 = beats[i + 1].time;
        if (chords_in(&cm, b0, b1) > 0) continue;
        bool clash = false;
        for (int c = 0; c < cm.count; c++) if (cm.entries[c].t_start < b0 + 1e-3 && cm.entries[c].t_end > b0 + 1e-3) clash = true;
        if (!clash) { first_free = i; break; }
    }
    warm.i0 = first_free; warm.i1 = n_int;

    CompleteInputs in0 = {};
    in0.beatmap = &bm; in0.chordmap = &empty;
    DecodeRun cold; cold.name = "cold"; cold.cm = &empty;
    build_chord_models(in0, p, &cache, &cold.models);
    cold.params = p;
    cold.i0 = 0; cold.i1 = n_int;

    run_reps(warm, &bm, &cache, reps);
    run_reps(cold, &bm, &cache, reps);
    auto t2 = now();

    printf("beats: %d\n", nb);
    printf("mapped chords: %d\n", mapped);
    std::vector<float> flat((size_t)n_int * 12);
    double csum = 0;
    for (int i = 0; i < n_int; i++) for (int c = 0; c < 12; c++) { flat[i * 12 + c] = cache.entries[i].v[c]; csum += cache.entries[i].v[c]; }
    printf("chroma: %d\n", dsp_checksum(flat.data(), 0, (int)flat.size()));
    printf("chroma sum: %.3f\n", csum);
    int tonic = 0; bool kmin = false;
    bool key_ok = estimate_key(in0, &cache, &tonic, &kmin);
    printf("key (KS): %s\n", chord_name(tonic, kmin).c_str());
    int wc = report(warm, song.chordAt);
    int cc = report(cold, song.chordAt);
    if (timing) {
        auto us = [](auto d) { return (long)std::chrono::duration_cast<std::chrono::microseconds>(d).count(); };
        long tc = us(t1 - t0), td = us(t2 - t1);
        printf("time(us): load=%ld chroma=%ld decode=%ld (decode share %ld%%)\n", us(t0 - tl), tc, td, tc + td > 0 ? 100 * td / (tc + td) : 0);
    }
    auto check = [](const char* what, int num, int den, int pct) {
        bool pass = den > 0 && num * 100 >= pct * den;
        printf("check %s (%d/%d): %s\n", what, num, den, pass ? "PASS" : "FAIL");
        return pass;
    };
    auto checkBool = [](const char* what, bool pass) {
        printf("check %s: %s\n", what, pass ? "PASS" : "FAIL");
        return pass;
    };
    bool learned = false;
    for (const ChordModel& m : warm.models) if (m.seen) learned = true;
    bool ok = true;
    ok &= checkBool("warm models learned from mapped chords", mapped > 0 && learned);
    ok &= checkBool("key estimate (Krumhansl-Schmuckler) is C major", key_ok && tonic == 0 && !kmin);
    ok &= check("warm decode accuracy >= 95%", wc, warm.i1 - warm.i0, 95);
    ok &= check("cold decode accuracy >= 90%", cc, cold.i1 - cold.i0, 90);
    return ok ? 0 : 1;
}
