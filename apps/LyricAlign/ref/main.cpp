// C++ reference for apps/LyricAlign: runs the ORIGINAL beatmapper
// lyric_align.cpp on the same synthetic vocal track as LyricAlign.v3 and
// prints the same output.  The generator uses only integer LCG + basic double
// arithmetic, so the input samples are bit-identical to the Virgil ones.
#include "lyric_align.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <vector>
#include <cstring>
#include <string>
#include <algorithm>

// Virgil's double.round is round-half-EVEN; std::round (and dsp_checksum in
// dsp/ref/synth_song.h) rounds half away from zero, which differs on exact
// ties (frequent for floats * 10000).  nearbyint (default mode) matches Virgil.
static int32_t checksum(const float* a, int start, int len) {
    uint32_t h = 0;
    for (int i = start; i < start + len; i++)
        h = h * 31u + (uint32_t)(int32_t)std::nearbyint((double)a[i] * 10000.0);
    return (int32_t)h;
}

// ---- DspOut twins (exact same rounding/formatting as dsp/Synth.v3) ----
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
static void label(const char* s) { fputs(s, stdout); fputs(": ", stdout); }

// ---- the synthetic vocal track (twin of VocalSynth in LyricAlign.v3) ----
static const char* LYRICS[] = {
    "Walking down the empty road tonight",
    "Every window burning with a light",
    "I remember how you used to sing",
    "Little voices calling out in spring",
    "Hold on",
    "Hold on to the morning when it comes",
    "We were running wild beneath the summer sun",
    "Never looking back",
    "Carry me home",
    "Over the water and under the stars",
    "Wanted to tell you who we are",
    "Oh the river keeps on rolling by",
    "Ninety nine reasons and a lullaby",
    "Somewhere a radio is playing our song",
    "Dancing alone",
    "All night long"
};
static const int NLYR = 16;

static int word_syl(const char* w, int len) {   // via the original: one word's syllables
    std::string s(w, len);
    return lyric_syllables(s.c_str());
}
static bool is_sep(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-' || c == '/'; }

struct VocalSynth {
    int sr = 8000;
    uint32_t seed = 4242u;
    std::vector<float> mono;
    int frames = 0;
    std::vector<const char*> lines;
    std::vector<double> starts, ends;

    double rand_() {
        seed = seed * 1664525u + 1013904223u;
        return (double)(seed >> 8) / 16777216.0;
    }
    double sing(std::vector<float>& buf, double t, const std::vector<int>& words, double amp,
                double f0, double rate) {
        double srd = sr;
        double dph = f0 / srd;
        for (size_t wi = 0; wi < words.size(); wi++) {
            for (int s = 0; s < words[wi]; s++) {
                double dur = rate * (0.8 + 0.4 * rand_());
                int s0 = (int)(t * srd), s1 = (int)((t + dur) * srd);
                if (s1 > (int)buf.size()) s1 = (int)buf.size();
                int n = s1 - s0;
                double ph = 0.0, ph2 = 0.25;
                for (int k = 0; k < n; k++) {
                    double x = (double)k / (double)n;
                    double bump = 4.0 * x * (1.0 - x);
                    double env = 0.35 + 0.65 * bump * bump;
                    double tri = 4.0 * std::fabs(ph - 0.5) - 1.0;
                    double tri2 = 4.0 * std::fabs(ph2 - 0.5) - 1.0;
                    buf[s0 + k] += (float)(amp * env * (tri + 0.4 * tri2));
                    ph += dph; if (ph >= 1.0) ph -= 1.0;
                    ph2 += 2.0 * dph; if (ph2 >= 1.0) ph2 -= 1.0;
                }
                t += dur;
            }
        }
        return t;
    }
    static std::vector<int> wordSyllables(const char* text) {
        std::vector<int> v;
        int p = 0, L = (int)strlen(text);
        while (p < L) {
            while (p < L && is_sep(text[p])) p++;
            int w = p;
            while (p < L && !is_sep(text[p])) p++;
            if (p > w) v.push_back(std::max(1, word_syl(text + w, p - w)));
        }
        return v;
    }
    void generate(int nl) {
        lines.assign(nl, nullptr); starts.assign(nl, 0.0); ends.assign(nl, 0.0);
        std::vector<std::vector<int>> words(NLYR);
        for (int i = 0; i < NLYR; i++) words[i] = wordSyllables(LYRICS[i]);
        int total = 0;
        for (int i = 0; i < nl; i++) for (int x : words[i % NLYR]) total += x;
        double est = 3.0 + total * 0.33 + nl * 2.2;
        std::vector<float> buf((size_t)(int)(est * sr), 0.0f);
        double t = 1.5, rate = 0.24;
        for (int i = 0; i < nl; i++) {
            int li = i % NLYR;
            lines[i] = LYRICS[li];
            double amp = 0.25 + 0.15 * rand_();
            double f0 = 160.0 + 140.0 * rand_();
            starts[i] = t;
            t = sing(buf, t, words[li], amp, f0, rate * (0.9 + 0.2 * rand_()));
            ends[i] = t;
            double pause = 0.6 + 0.8 * rand_();
            if (i % 7 == 6) {
                double t2 = sing(buf, t + 0.65, std::vector<int>{1}, amp * 0.6, f0 * 1.5, 0.3);
                t = t2 + 0.65;
            } else {
                t += pause;
            }
        }
        frames = (int)(t * sr);   // the clip ends after the last pause
        if (frames > (int)buf.size()) frames = (int)buf.size();
        mono.assign(frames, 0.0f);
        for (int i = 0; i < frames; i++) mono[i] = buf[i] + (float)(0.002 * (rand_() - 0.5));
    }
};

static const int QUIET = 5;

// The song: the 16-line clip tiled to nl lines (twin of Song in LyricAlign.v3;
// the Virgil app synthesizes the clip at compile time).
struct Song {
    int sr = 0, frames = 0, syllables = 0;
    std::vector<float> mono;
    std::vector<const char*> lines;
    std::vector<double> starts, ends;
    Song(const VocalSynth& clip, int nl) {
        sr = clip.sr;
        int NC = (int)clip.lines.size(), L = clip.frames;
        lines.assign(nl, nullptr); starts.assign(nl, 0.0); ends.assign(nl, 0.0);
        for (int i = 0; i < nl; i++) {
            int ci = i % NC;
            double off = (double)((i / NC) * L) / sr;
            lines[i] = clip.lines[ci];
            starts[i] = clip.starts[ci] + off;
            ends[i] = clip.ends[ci] + off;
            syllables += lyric_syllables(lines[i]);
        }
        int last = nl - 1;
        frames = (last / NC) * L + std::min(L, (int)((clip.ends[last % NC] + 1.0) * sr));
        mono.assign(frames, 0.0f);
        for (int base = 0; base < frames; base += L) {
            int n = std::min(L, frames - base);
            for (int k = 0; k < n; k++) mono[base + k] = clip.mono[k];
        }
        for (int i = (int)(starts[nl - QUIET] * sr) - 1; i < frames; i++) mono[i] *= 0.25f;
    }
};

static int failures = 0;
static void check(const char* what, bool ok) {
    printf("check %s%s\n", what, ok ? ": PASS" : ": FAIL");
    if (!ok) failures++;
}

int main(int argc, char** argv) {
    int nlines = argc > 1 ? atoi(argv[1]) : 32;
    int nanchors = argc > 2 ? atoi(argv[2]) : 3;
    if (nanchors < 0) nanchors = 0;
    if (nanchors > 3) nanchors = 3;
    int allAnchors[3] = {2, 7, 12};
    int minLines = std::max(QUIET + 3, nanchors > 0 ? allAnchors[nanchors - 1] + 1 : 0);
    if (nlines < minLines) nlines = minLines;
    VocalSynth clip; clip.generate(NLYR);
    Song syn(clip, nlines);
    LyricActivity act;
    lyric_activity(syn.mono.data(), (uint64_t)syn.frames, (uint32_t)syn.sr, &act);

    const int* anchorIdx = allAnchors;
    int nl = (int)syn.lines.size();
    std::vector<bool> isAnchor(nl, false);
    std::vector<LyricAnchor> anchors;
    for (int k = 0; k < nanchors; k++) {
        int li = anchorIdx[k];
        isAnchor[li] = true;
        LyricAnchor a; a.t0 = syn.starts[li]; a.t1 = syn.ends[li]; a.text = syn.lines[li]; a.seq = li - k;
        anchors.push_back(a);
    }
    std::vector<const char*> lines;
    std::vector<double> truth;
    for (int i = 0; i < nl; i++) {
        if (isAnchor[i]) continue;
        lines.push_back(syn.lines[i]); truth.push_back(syn.starts[i]);
    }
    LyricAlignParams p;
    lyric_align_defaults(&p);
    std::vector<LyricAlignOut> out;
    double duration = (double)syn.frames / syn.sr;
    double rate = lyric_align(act, 0.0, duration, lines, anchors, p, &out);

    label("frames"); putl((long long)act.db.size());
    label(" floor"); putd(act.floor_db, 2);
    label(" peak"); putd(act.peak_db, 2);
    putchar('\n');
    LyricAlignParams pr = p;
    pr.max_below = 20.0f;
    std::vector<LyricUnit> units;
    lyric_units(act, 0.0, duration, pr, &units);
    int nuc = 0; uint32_t uh = 0;
    for (const LyricUnit& u : units) {
        nuc += u.nuclei;
        uh = uh * 31u + (uint32_t)(int32_t)std::nearbyint(u.t0 * 100.0);
        uh = uh * 31u + (uint32_t)(int32_t)std::nearbyint(u.t1 * 100.0);
        uh = uh * 31u + (uint32_t)u.nuclei;
        uh = uh * 31u + (uint32_t)(int32_t)std::nearbyint(u.dip_before * 10.0);
    }
    label("envelope checksum"); putl(checksum(act.db.data(), 0, (int)act.db.size()));
    putchar('\n');
    label("units"); putl((long long)units.size());
    label(" nuclei"); putl(nuc);
    label(" hash"); putl((int32_t)uh);
    label(" rate"); putd(rate, 4);
    putchar('\n');

    bool verbose = lines.size() <= 64;
    int placed = 0, close = 0; double sumScore = 0.0; uint32_t ph = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        const LyricAlignOut& o = out[i];
        bool ok = o.placed && std::fabs(o.t0 - truth[i]) <= 0.5;
        if (o.placed) {
            placed++; sumScore += o.score;
            ph = ph * 31u + (uint32_t)(int32_t)std::nearbyint(o.t0 * 100.0);
            ph = ph * 31u + (uint32_t)(int32_t)std::nearbyint(o.t1 * 100.0);
        }
        if (ok) close++;
        if (verbose) {
            putl((long long)i);
            fputs(" syl=", stdout); putl(lyric_syllables(lines[i]));
            fputs(" truth=", stdout); putd(truth[i], 2);
            if (o.placed) {
                fputs(" t0=", stdout); putd(o.t0, 2);
                fputs(" t1=", stdout); putd(o.t1, 2);
                fputs(" score=", stdout); putd(o.score, 3);
            } else fputs(" unplaced", stdout);
            if (ok) fputs(" ok", stdout);
            putchar('\n');
        }
    }
    label("lines"); putl((long long)lines.size());
    label(" placed"); putl(placed);
    label(" within0.5s"); putl(close);
    label(" meanScore"); putd(placed > 0 ? sumScore / placed : 0.0, 4);
    label(" hash"); putl((int32_t)ph);
    putchar('\n');

    bool inOrder = true, clearOfAnchors = true;
    double prevEnd = -1.0;
    for (size_t i = 0; i < lines.size(); i++) {
        const LyricAlignOut& o = out[i];
        if (!o.placed) continue;
        if (o.t0 < prevEnd - 1e-9 || o.t1 <= o.t0) inOrder = false;
        prevEnd = o.t1;
        for (const LyricAnchor& a : anchors) if (o.t0 < a.t1 - 1e-9 && o.t1 > a.t0 + 1e-9) clearOfAnchors = false;
    }
    label("syllables"); putl(syn.syllables);
    putchar('\n');
    check("all lines placed", placed == (int)lines.size());
    double quietT = syn.starts[syn.lines.size() - QUIET] - 0.01;
    int loud = 0, loudOk = 0, quiet = 0, quietOk = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        bool ok = out[i].placed && std::fabs(out[i].t0 - truth[i]) <= 0.5;
        if (truth[i] < quietT) { loud++; if (ok) loudOk++; }
        else { quiet++; if (ok) quietOk++; }
    }
    label("loud lines ok"); putl(loudOk); fputs("/", stdout); putl(loud);
    label(" quiet lines ok"); putl(quietOk); fputs("/", stdout); putl(quiet);
    putchar('\n');
    check("lines before the quiet chorus starting within 0.5 s of truth >= 85%", loudOk * 100 >= loud * 85);
    check("placements in sheet order, non-overlapping", inOrder);
    check("no line placed over an anchor", clearOfAnchors);
    check("syllable nuclei within 15% of sung syllables",
          nuc * 100 >= syn.syllables * 85 && nuc * 100 <= syn.syllables * 115);
    check("singing rate within 15% of the synthesized 0.24 s", std::fabs(rate - 0.24) <= 0.036);
    return failures > 0 ? 1 : 0;
}
