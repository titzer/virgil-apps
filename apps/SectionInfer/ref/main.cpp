// C++ reference for apps/SectionInfer: runs the ORIGINAL beatmapper section
// inference (extracted verbatim from complete_algo.cpp by build.sh) on the same
// synthetic per-beat features as SectionInfer.v3 and prints the same output.
// The generator uses only an integer LCG + basic double arithmetic, so the
// features are bit-identical to the Virgil ones.
#include "complete_algo.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

// ---- definitions the extracted code links against (verbatim from the sources) ----
const char* const SECTION_KIND_NAMES[SK_COUNT] = {
    "intro", "verse", "pre-chorus", "chorus", "post-chorus",
    "bridge", "breakdown", "instrumental", "solo",
    "interlude", "outro", "refrain"
};
float chroma_cosine(const float a[12], const float b[12]) {   // beat_chroma.cpp
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < 12; i++) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    if (na < 1e-12f || nb < 1e-12f) return 0.0f;
    return dot / sqrtf(na * nb);
}
int shape_onset_at(const ShapeAnalysis& sa, double t) {      // onset_shape.cpp
    return (int)(std::lower_bound(sa.onset_t.begin(), sa.onset_t.end(), t) - sa.onset_t.begin());
}
void shape_band_profile(const AudioPcm&, double, double, int, std::vector<float>*) {}  // unused (no audio)

static long long g_dpCells = 0;
static double g_dpBest = 0.0;
#include "gen_sections.inc"

// ---- DspOut twins (Virgil double.round is half-even: use nearbyint) ----
static void putl(long long v) {
    if (v < 0) { putchar('-'); v = -v; }
    if (v >= 10) putl(v / 10);
    putchar('0' + (int)(v % 10));
}
static void putd(double v, int digits) {
    if (v != v) { fputs("NaN", stdout); return; }
    if (v < 0) { putchar('-'); v = -v; }
    double scale = 1.0;
    for (int i = 0; i < digits; i++) scale *= 10.0;
    long long x = (long long)std::nearbyint(v * scale);
    long long ip = x / (long long)scale, fp = x % (long long)scale;
    putl(ip);
    if (digits > 0) {
        putchar('.');
        long long div = (long long)scale / 10;
        while (div > 0) { putchar('0' + (int)((fp / div) % 10)); div /= 10; }
    }
}
static int32_t checksum(const float* a, int len) {
    uint32_t h = 0;
    for (int i = 0; i < len; i++) h = h * 31u + (uint32_t)(int32_t)std::nearbyint((double)a[i] * 10000.0);
    return (int32_t)h;
}

// ---- synthetic song (twin of SongSynth in SectionInfer.v3) ----
static std::vector<int> chordsFor(int kind) {
    switch (kind) {
        case SK_VERSE:  return {0, 7, 21, 5};
        case SK_CHORUS: return {5, 0, 7, 21};
        case SK_BRIDGE: return {14, 16, 5, 7};
        default:        return {21, 5, 0, 7};
    }
}
static std::vector<int> drumsFor(int kind) {
    switch (kind) {
        case SK_VERSE:  return {0,0,0, 0,6,2, 1,0,1, 1,6,2, 2,0,0, 2,6,2, 3,0,1, 3,6,2};
        case SK_CHORUS: return {0,0,0, 0,3,2, 0,6,2, 0,9,2, 1,0,1, 1,3,2, 1,6,2, 1,9,2,
                                2,0,0, 2,3,2, 2,6,2, 2,9,2, 3,0,1, 3,3,2, 3,6,2, 3,9,2};
        case SK_BRIDGE: return {0,0,0, 0,6,2, 1,0,4, 2,0,0, 2,6,2, 3,0,3, 3,4,3, 3,8,3};
        default:        return {0,0,0, 1,0,2, 2,0,2, 3,0,2};
    }
}
static double gainFor(int kind) {
    switch (kind) { case SK_CHORUS: return 3.0; case SK_VERSE: return 0.0; case SK_BRIDGE: return 1.0; default: return -4.0; }
}
static const double SHAPE_DB[5] = {-10.0, -14.0, -26.0, -16.0, -20.0};
static const double SLOPES[12] = {-0.8, -0.2, 0.0, 0.5, 0.0, 0.1, 0.0, 0.0, 0.0, 0.0, -0.6, 0.0};

struct SongSynth {
    uint32_t seed = 777u;
    int K = 5, TB = 24;
    std::vector<double> beatTimes;
    std::vector<float> chroma, timbre;
    ShapeAnalysis shapes;
    std::vector<Section> truth;
    int nmapped = 0;
    double duration = 0;

    double rand_() { seed = seed * 1664525u + 1013904223u; return (double)(seed >> 8) / 16777216.0; }
    void generate(int reps) {
        std::vector<std::pair<int, int>> plan;
        plan.push_back({SK_INTRO, 4});
        for (int r = 0; r < reps; r++) { plan.push_back({SK_VERSE, 8}); plan.push_back({SK_CHORUS, 8}); }
        plan.push_back({SK_BRIDGE, 8});
        plan.push_back({SK_CHORUS, 10});
        plan.push_back({SK_OUTRO, 4});
        int n = 0;
        for (auto& pl : plan) n += pl.second * 4;
        beatTimes.assign(n + 1, 0.0);
        for (int b = 0; b < n + 1; b++) beatTimes[b] = 0.2 + b * 0.5 + 0.006 * (rand_() - 0.5);
        std::vector<double> base(SK_COUNT * TB);
        for (int kd = 0; kd < SK_COUNT; kd++)
            for (int j = 0; j < TB; j++) base[kd * TB + j] = SLOPES[kd] * (j - 11.5) / 12.0 + 0.5 * rand_();
        chroma.assign(n * 12, 0.0f);
        timbre.assign(n * TB, 0.0f);
        int b = 0;
        int counts[SK_COUNT] = {0};
        for (size_t si = 0; si < plan.size(); si++) {
            int kind = plan[si].first, bars = plan[si].second;
            int b0 = b;
            std::vector<int> chords = chordsFor(kind), drums = drumsFor(kind);
            double gain = gainFor(kind);
            for (int bar = 0; bar < bars; bar++) {
                int chord = chords[bar % 4];
                int root = chord % 12; bool minor = chord >= 12;
                for (int beat = 0; beat < 4; beat++) {
                    double v[12];
                    for (int k = 0; k < 12; k++) v[k] = 0.04 + 0.12 * rand_();
                    v[root] += 1.0 * (0.8 + 0.2 * rand_());
                    v[(root + (minor ? 3 : 4)) % 12] += 0.7 * (0.8 + 0.2 * rand_());
                    v[(root + 7) % 12] += 0.8 * (0.8 + 0.2 * rand_());
                    double mx = 0.0;
                    for (int k = 0; k < 12; k++) if (v[k] > mx) mx = v[k];
                    for (int k = 0; k < 12; k++) chroma[b * 12 + k] = (float)(v[k] / mx);
                    std::vector<double> tv(TB); double mean = 0.0;
                    for (int j = 0; j < TB; j++) { tv[j] = base[kind * TB + j] + 0.25 * rand_(); mean += tv[j]; }
                    mean /= TB;
                    for (int j = 0; j < TB; j++) timbre[b * TB + j] = (float)(tv[j] - mean);
                    double t = beatTimes[b], ibi = beatTimes[b + 1] - beatTimes[b];
                    for (size_t q = 0; q < drums.size(); q += 3) {
                        if (drums[q] != beat) continue;
                        int pos = drums[q + 1], shape = drums[q + 2];
                        shapes.onset_t.push_back(t + pos / 12.0 * ibi + 0.002 * rand_());
                        shapes.onset_energy.push_back((float)(SHAPE_DB[shape] + gain + 2.0 * rand_()));
                        double main_ = 0.55 + 0.3 * rand_();
                        for (int c = 0; c < K; c++) {
                            double x = c == shape ? main_ : (1.0 - main_) * 0.25 * (0.5 + rand_());
                            shapes.onset_soft.push_back((float)x);
                        }
                    }
                    b++;
                }
            }
            counts[kind]++;
            Section sec = {};
            sec.t_start = beatTimes[b0]; sec.t_end = beatTimes[b];
            sec.kind = (SectionKind)kind;
            sec.label[0] = (char)('0' + counts[kind] % 10); sec.label[1] = 0;
            sec.ts_num = 4; sec.ts_den = 4;
            truth.push_back(sec);
            if (si <= 2) nmapped++;
        }
        shapes.vocab.valid = true;
        shapes.vocab.k = K;
        duration = beatTimes[n] + 0.4;
    }
};

static void section_str(const Section& s) {
    printf("%s", SECTION_KIND_NAMES[s.kind]);
    if (s.label[0]) printf(" %s", s.label);
}

static int failures = 0;
static void check(const char* what, bool ok) {
    printf("  check %s%s\n", what, ok ? ": PASS" : ": FAIL");
    if (!ok) failures++;
}

static void report(const char* name, const std::vector<CompleteCand>& kept, const BeatMap* bm,
                   const std::vector<Section>& truth, int nmapped, bool verbose) {
    printf("%s: ", name); putl((long long)kept.size()); printf(" candidates\n");
    std::vector<CompleteCand> sorted = kept;   // time order (std::sort ties are unordered)
    std::stable_sort(sorted.begin(), sorted.end(), [](const CompleteCand& x, const CompleteCand& y) {
        return x.t0 < y.t0 || (x.t0 == y.t0 && (x.t1 < y.t1 || (x.t1 == y.t1 && x.sec_kind < y.sec_kind)));
    });
    uint32_t h = 0; float simSum = 0.0f;
    for (const CompleteCand& c : sorted) {
        int b0 = nearest_beat(bm, c.t0), b1 = nearest_beat(bm, c.t1);
        h = h * 31u + (uint32_t)b0; h = h * 31u + (uint32_t)b1; h = h * 31u + (uint32_t)c.sec_kind;
        h = h * 31u + (uint32_t)(int32_t)std::nearbyint((double)(c.sim * 10000.0f));
        simSum += c.sim;
        if (verbose) {
            printf("  "); putd(c.t0, 2); printf("-"); putd(c.t1, 2);
            printf(" beats "); putl(b0); printf("-"); putl(b1);
            printf(" %s %s from \"%s\" sim ", SECTION_KIND_NAMES[c.sec_kind], c.label, c.source);
            putd(c.sim, 4);
            if (!c.selected) printf(" (weak)");
            printf("\n");
        }
    }
    int unm = 0, edges = 0, kinds = 0;
    for (size_t i = nmapped; i < truth.size(); i++) {
        const Section& s = truth[i];
        unm++;
        for (const CompleteCand& c : kept) {
            if (fabs(c.t0 - s.t_start) < 0.3 && fabs(c.t1 - s.t_end) < 0.3) {
                edges++;
                if (c.sec_kind == s.kind) kinds++;
                break;
            }
        }
    }
    printf("  truth: "); putl(unm); printf(" unmapped sections, ");
    putl(edges); printf(" found exactly, ");
    putl(kinds); printf(" with the right kind; hash "); putl((int32_t)h);
    printf(" simsum "); putd(simSum, 4);
    printf("\n");

    int vc = 0, vcOk = 0;
    for (size_t i = nmapped; i < truth.size(); i++) {
        const Section& s = truth[i];
        if (s.kind != SK_VERSE && s.kind != SK_CHORUS) continue;
        vc++;
        for (const CompleteCand& c : kept)
            if (fabs(c.t0 - s.t_start) < 0.3 && fabs(c.t1 - s.t_end) < 0.3 && c.sec_kind == s.kind) { vcOk++; break; }
    }
    bool overMapped = false, overEach = false;
    for (size_t i = 0; i < kept.size(); i++) {
        const CompleteCand& c = kept[i];
        for (int j = 0; j < nmapped; j++) {
            const Section& m = truth[j];
            if (overlap_len(c.t0, c.t1, m.t_start, m.t_end) > 0.10 * (c.t1 - c.t0) + 1e-9) overMapped = true;
        }
        for (size_t j = 0; j < i; j++) {
            const CompleteCand& d = kept[j];
            double ov = overlap_len(c.t0, c.t1, d.t0, d.t1);
            if (ov > 0.25 * (c.t1 - c.t0) + 1e-9 || ov > 0.25 * (d.t1 - d.t0) + 1e-9) overEach = true;
        }
    }
    printf("  unmapped verses/choruses found exactly with the right kind: "); putl(vcOk); printf("/"); putl(vc);
    printf("\n");
    check("unmapped verses/choruses found exactly with the right kind >= 75%", vcOk * 100 >= vc * 75);
    check("no candidate overlaps a mapped section by > 10%", !overMapped);
    check("no two candidates overlap by > 25%", !overEach);
}

int main(int argc, char** argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 2;
    SongSynth syn; syn.generate(reps);
    std::vector<Beat> bv(syn.beatTimes.size());
    for (size_t i = 0; i < bv.size(); i++) { bv[i].time = syn.beatTimes[i]; bv[i].selected = false; bv[i].interp = false; }
    BeatMap bm = {}; bm.beats = bv.data(); bm.count = (int)bv.size(); bm.capacity = bm.count;
    std::vector<Section> mappedv(syn.truth.begin(), syn.truth.begin() + syn.nmapped);
    SectionMap sm = {}; sm.sections = mappedv.data(); sm.count = (int)mappedv.size(); sm.capacity = sm.count;
    MiscMap cm = {}; cm.count = 0;
    BeatChromaCache cache;
    int nint = bm.count - 1;
    cache.entries.resize(nint);
    for (int i = 0; i < nint; i++) {
        cache.entries[i].t0 = syn.beatTimes[i]; cache.entries[i].t1 = syn.beatTimes[i + 1];
        memcpy(cache.entries[i].v, &syn.chroma[i * 12], sizeof(float) * 12);
    }

    // complete_params_defaults (the fields section inference reads)
    CompleteParams p = {};
    p.gap_factor = 1.8f;
    p.section_granularity = 0;
    p.section_sim_threshold = 0.75f;
    p.section_max_overlap = 0.10f;
    p.section_rhythm_weight = 0.4f;
    p.section_discover = true;
    p.section_partition = true;
    p.section_block_penalty = 1.0f;
    p.section_ext_penalty = 0.01f;
    p.section_trunc_penalty = 0.04f;
    p.section_prior_weight = 0.5f;
    p.section_phase_lock = true;
    p.section_timbre_weight = 0.45f;
    p.section_timbre_bands = 24;
    p.section_min_measures = 4;
    p.section_start_margin = -1.0f;
    p.chord_sim_threshold = 0.70f;
    const int slots = 12;   // ShapeParams.slots_per_beat default

    BeatFeatures f;
    build_beat_features(&bm, &cache, &syn.shapes, slots, &f);
    f.tdim = syn.TB; f.tw = p.section_timbre_weight; f.timbre = syn.timbre;

    CompleteInputs in = {};
    in.beatmap = &bm; in.sectionmap = &sm; in.chordmap = &cm; in.duration = syn.duration;
    double gap_thresh = p.gap_factor * median_ibi(&bm);

    bool verbose = reps <= 4;
    printf("beats "); putl(bm.count);
    printf(" onsets "); putl((long long)syn.shapes.onset_t.size());
    printf(" rdim "); putl(f.rdim);
    printf(" tdim "); putl(f.tdim);
    printf("\n");
    printf("checksums chroma "); putl(checksum(syn.chroma.data(), (int)syn.chroma.size()));
    printf(" rhythm "); putl(checksum(f.rhythm.data(), (int)f.rhythm.size()));
    printf(" timbre "); putl(checksum(f.timbre.data(), (int)f.timbre.size()));
    printf("\n");
    if (verbose) {
        printf("truth:");
        for (const Section& s : syn.truth) { printf(" "); section_str(s); printf("@"); putl(nearest_beat(&bm, s.t_start)); }
        printf("\n");
    }

    CompleteProposal out = {};
    g_dpCells = 0; g_dpBest = 0.0;
    infer_sections(in, p, f, gap_thresh, &out);
    report("partition", out.cands, &bm, syn.truth, syn.nmapped, verbose);
    printf("  dp states "); putl(g_dpCells); printf(" best "); putd(g_dpBest, 3); printf("\n");

    p.section_partition = false;
    CompleteProposal out2 = {};
    infer_sections(in, p, f, gap_thresh, &out2);
    report("templates", out2.cands, &bm, syn.truth, syn.nmapped, verbose);
    return failures > 0 ? 1 : 0;
}
