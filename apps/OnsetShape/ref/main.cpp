// Reference harness for apps/OnsetShape: runs the ORIGINAL beatmapper
// onset_shape.cpp (+ beat_spectral_flux.cpp as the onset detector) on the same
// synthetic song as OnsetShape.v3 and prints the same summary.
// Build: ./build.sh   Run (from the repo root): apps/OnsetShape/ref/onset_shape_ref $(cat apps/OnsetShape/args-test)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include "beat_algo.h"
#include "beatmap.h"
#include "onset_shape.h"
#include "../../../dsp/ref/song_file.h"

// Stand-ins for beat_algo.cpp's registry: only the spectral-flux detector.
void autobeat_init(AutoBeatList* ab) { memset(ab, 0, sizeof(*ab)); }
const BeatAlgoDesc BEAT_ALGOS[] = { { "Spectral Flux", "", beat_spectral_flux } };
const int BEAT_ALGO_COUNT = 1;

static void putd(double v, int digits) { printf("%.*f", digits, v); }

int main(int argc, char** argv) {
    if (argc < 5) { printf("Usage: onset_shape_ref <clip.wav> <milliseconds> <k> <profile-ms>\n"); return 1; }
    SynthSong song; song.seconds = atoi(argv[2]) / 1000.0;
    song.beatsPerChord = 1;   // the clip's parameters (inputs/songs.list)
    song_load_tiled(song, argv[1]);
    AudioPcm a = { song.pcm.data(), (uint64_t)song.frames, (uint32_t)song.channels, (uint32_t)song.sampleRate };
    double duration = (double)song.frames / song.sampleRate;

    std::vector<Beat> beats(song.beatTimes.size());
    for (size_t i = 0; i < beats.size(); i++) beats[i] = { song.beatTimes[i], false, false };
    BeatMap bm = {}; bm.beats = beats.data(); bm.count = (int)beats.size(); bm.capacity = bm.count;

    ShapeParams p; shape_params_defaults(&p);
    p.onset_threshold = 0.1f;   // same as OnsetShape.v3: harvest the quiet hats too
    p.k = atoi(argv[3]);
    ShapeAnalysis sa;
    shape_analysis_ensure(&sa, a, &bm, duration, p, 0);
    const ShapeVocab& v = sa.vocab;
    int n = (int)sa.onset_t.size(), dim = v.dim;

    // trained-on count, as in shape_analysis_ensure
    int trained = 0;
    double med = 0.5;
    { std::vector<double> d; for (int i = 1; i < bm.count; i++) d.push_back(beats[i].time - beats[i-1].time);
      std::sort(d.begin(), d.end()); med = d[d.size() / 2]; }
    for (int i = 0; i < n; i++) {
        double t = sa.onset_t[i];
        int j = (int)(std::lower_bound(song.beatTimes.begin(), song.beatTimes.end(), t) - song.beatTimes.begin());
        if (j > 0 && j < bm.count && beats[j].time - beats[j-1].time <= 1.8 * med) trained++;
    }
    if (trained < p.k * 4) trained = n;

    std::vector<float> prof;
    shape_band_profile(a, 0.0, std::min(duration, atoi(argv[4]) / 1000.0), p.bands, &prof);

    // soft memberships padded to SHAPE_MAX_K per onset, as the Virgil port stores them
    std::vector<float> softAll((size_t)n * SHAPE_MAX_K, 0.0f);
    double confSum = 0;
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < v.k; c++) softAll[(size_t)i * SHAPE_MAX_K + c] = sa.onset_soft[(size_t)i * v.k + c];
        if (sa.onset_shape[i] >= 0) confSum += sa.onset_soft[(size_t)i * v.k + sa.onset_shape[i]];
    }

    printf("onsets: %d\n", n);
    printf("window: "); putd(sa.win, 4); printf("\n");
    printf("dim: %d\n", dim);
    printf("trained on: %d\n", trained);
    printf("scale: "); putd(v.scale, 3); printf("\n");
    for (int c = 0; c < v.k; c++) {
        printf("shape %d: count=%d energy=", c, v.count[c]); putd(v.energy[c], 2);
        printf(" cent=%d\n", dsp_checksum(v.cent.data(), c * dim, dim));
    }
    printf("mean conf: "); putd(n ? confSum / n : 0.0, 3); printf("\n");
    double tsum = 0, fsum = 0, psum = 0;
    for (double t : sa.onset_t) tsum += t;
    for (float f : sa.onset_feat) fsum += f;
    for (float f : prof) psum += fabs(f);
    printf("onset time sum: "); putd(tsum, 4); printf("\n");
    printf("feature sum: "); putd(fsum, 3); printf("\n");
    printf("profile abs sum: "); putd(psum, 4); printf("\n");
    printf("profile: %d\n", dsp_checksum(prof.data(), 0, (int)prof.size()));
    printf("features: %d\n", dsp_checksum(sa.onset_feat.data(), 0, (int)sa.onset_feat.size()));
    printf("soft: %d\n", dsp_checksum(softAll.data(), 0, (int)softAll.size()));
    printf("labels: ");
    for (int i = 0; i < std::min(n, 48); i++) putchar('0' + sa.onset_shape[i]);
    printf("\n");
    int32_t h = 0;
    for (int i = 0; i < n; i++) h = (int32_t)((uint32_t)h * 31u + (uint32_t)sa.onset_shape[i]);
    printf("label hash: %d\n", h);

    static const char* NAMES[4] = { "kick", "snare", "hat", "other" };
    double half = med / 2;
    int nslots = (int)(duration / half) + 1;
    std::vector<bool> slotHit(nslots, false);
    std::vector<int> cls(n);
    int conf[4][SHAPE_MAX_K] = {};
    for (int i = 0; i < n; i++) {
        double t = sa.onset_t[i] + 2048.0 / song.sampleRate * 0.5;
        int k = (int)std::nearbyint(t / half);
        int c = 3;
        if (fabs(t - k * half) < 0.06) {
            int beat = k / 2; c = (k % 2) == 1 ? 2 : (beat % 2) == 0 ? 0 : 1;
            if (k < nslots) slotHit[k] = true;
        }
        cls[i] = c;
        if (sa.onset_shape[i] >= 0) conf[c][sa.onset_shape[i]]++;
    }
    int major[3];
    for (int c = 0; c < 4; c++) {
        printf("%s:", NAMES[c]);
        int best = 0;
        for (int s = 0; s < v.k; s++) { printf(" %d", conf[c][s]); if (conf[c][s] > conf[c][best]) best = s; }
        printf("\n");
        if (c < 3) major[c] = best;
    }
    printf("majority shapes: kick=%d snare=%d hat=%d\n", major[0], major[1], major[2]);
    int expected = 0, hit = 0;
    for (int k = 1; k < nslots; k++) {
        if (k * half + 0.06 >= duration) break;
        expected++;
        if (slotHit[k]) hit++;
    }
    int pure = 0;
    for (int i = 0; i < n; i++) {
        int s = sa.onset_shape[i];
        if (s < 0) continue;
        int bc = 0;
        for (int c = 0; c < 4; c++) if (conf[c][s] > conf[bc][s]) bc = c;
        if (bc == cls[i]) pure++;
    }
    auto check = [](const char* what, int num, int den, int pct) {
        bool pass = den > 0 && num * 100 >= pct * den;
        printf("check %s (%d/%d): %s\n", what, num, den, pass ? "PASS" : "FAIL");
        return pass;
    };
    bool ok = true;
    ok &= check("onset recall >= 90% of drum hits", hit, expected, 90);
    ok &= check("shape purity >= 85% of onsets", pure, n, 85);
    bool distinct = v.valid && major[0] != major[1] && major[0] != major[2] && major[1] != major[2];
    printf("check kick/snare/hat in distinct shapes: %s\n", distinct ? "PASS" : "FAIL");
    ok &= distinct;
    return ok ? 0 : 1;
}
