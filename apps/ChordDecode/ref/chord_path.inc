// VERBATIM extract of beatmapper src/complete_algo.cpp (chord decoder path), generated with sed:
//   lines 119-133 (first_beat_at, nearest_beat),
//   146-149 (overlap_len), 2575-2580 (chords_in), 2963-3208 (triad_template .. decode_chords).
// The only change is one line marked [ref] in decode_chords exporting the final Viterbi score.
static float g_ref_best_score = 0.0f;
static int first_beat_at(const BeatMap* bm, double t) {
    int lo = 0, hi = bm->count;
    while (lo < hi) { int m = (lo + hi) / 2; if (bm->beats[m].time < t) lo = m + 1; else hi = m; }
    return lo;
}

// Index of the beat nearest to t.  Section and chord edges are meant to sit
// on beats; taking the first beat *after* an edge that lands a few ms past
// its beat would shift everything built on it one beat late.
static int nearest_beat(const BeatMap* bm, double t) {
    int i = first_beat_at(bm, t);
    if (i >= bm->count) return bm->count - 1;
    if (i > 0 && t - bm->beats[i - 1].time < bm->beats[i].time - t) return i - 1;
    return i;
}
static double overlap_len(double a0, double a1, double b0, double b1) {
    double o = std::min(a1, b1) - std::max(a0, b0);
    return o > 0.0 ? o : 0.0;
}
static int chords_in(const MiscMap* cm, double t0, double t1) {
    int n = 0;
    for (int i = 0; i < cm->count; i++)
        if (cm->entries[i].t_start >= t0 - 1e-3 && cm->entries[i].t_start < t1 - 1e-3) n++;
    return n;
}
static const char* ROOT_NAMES[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

struct ChordModel { char text[32]; float v[12]; int n; bool seen; };

static void triad_template(int root, bool minor, float v[12]) {
    memset(v, 0, sizeof(float) * 12);
    v[root] = 1.0f; v[(root + (minor ? 3 : 4)) % 12] = 0.8f; v[(root + 7) % 12] = 0.8f;
}

// Parse a chord name's root and quality well enough to pick a triad.
static bool parse_chord(const char* text, int* root, bool* minor) {
    if (!text || !text[0]) return false;
    int r = -1;
    switch (text[0]) { case 'C': r = 0; break; case 'D': r = 2; break; case 'E': r = 4; break;
                       case 'F': r = 5; break; case 'G': r = 7; break; case 'A': r = 9; break;
                       case 'B': r = 11; break; default: return false; }
    const char* q = text + 1;
    if (*q == '#') { r = (r + 1) % 12; q++; } else if (*q == 'b') { r = (r + 11) % 12; q++; }
    *root = r;
    *minor = (q[0] == 'm' && q[1] != 'a');     // "m", "m7", "min" but not "maj"
    return true;
}

static void build_chord_models(const CompleteInputs& in, const CompleteParams& p,
                               const BeatChromaCache* cache, std::vector<ChordModel>* models)
{
    const BeatMap* bm = in.beatmap;
    const MiscMap* cm = in.chordmap;
    models->clear();
    for (int i = 0; i < cm->count; i++) {
        const MiscAnnotation& ch = cm->entries[i];
        int bs = nearest_beat(bm, ch.t_start), be = nearest_beat(bm, ch.t_end);
        if (be <= bs) continue;
        char name[32]; strncpy(name, ch.text, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        char* sp = strchr(name, ' '); if (sp) *sp = 0;          // "Em Em" -> "Em"
        ChordModel* m = nullptr;
        for (ChordModel& x : *models) if (!strcmp(x.text, name)) m = &x;
        if (!m) {
            ChordModel x; strncpy(x.text, name, sizeof(x.text) - 1); x.text[sizeof(x.text) - 1] = 0;
            memset(x.v, 0, sizeof(x.v)); x.n = 0; x.seen = true;
            models->push_back(x); m = &models->back();
        }
        for (int k = bs; k < be && k < (int)cache->entries.size(); k++) {
            for (int c = 0; c < 12; c++) m->v[c] += cache->entries[k].v[c];
            m->n++;
        }
    }
    // Blend each learned model with its triad (k pseudo-beats of it)
    for (ChordModel& m : *models) {
        int root; bool minor;
        float tri[12];
        bool have_tri = parse_chord(m.text, &root, &minor);
        float mx = 0; for (int c = 0; c < 12; c++) mx = std::max(mx, m.v[c]);
        for (int c = 0; c < 12; c++) {
            float learned = mx > 0 ? m.v[c] / mx : 0.0f;     // normalise peak to 1 like a triad
            if (have_tri) { triad_template(root, minor, tri); m.v[c] = (m.n * learned + p.chord_prior_beats * tri[c]) / (m.n + p.chord_prior_beats); }
            else m.v[c] = learned;
        }
    }
    // Unseen triads as a fallback vocabulary, penalised in the decoder
    if (p.chord_unseen_penalty < 1.0f) {
        for (int t = 0; t < 24; t++) {
            int root = t % 12; bool minor = t >= 12;
            char name[32]; snprintf(name, sizeof(name), "%s%s", ROOT_NAMES[root], minor ? "m" : "");
            bool dup = false;
            for (const ChordModel& m : *models) { int r2; bool m2; if (parse_chord(m.text, &r2, &m2) && r2 == root && m2 == minor) dup = true; }
            if (dup) continue;
            ChordModel x; strncpy(x.text, name, sizeof(x.text)); x.n = 0; x.seen = false;
            triad_template(root, minor, x.v);
            models->push_back(x);
        }
    }
}

// The corpus-trained emission model (src/chord_model.h, from
// scripts/train_chords.py): one weight vector per quality, applied to the
// beat's chroma rotated to each candidate root, softmaxed over the 24
// major/minor triads.  Trained across every mapped track, so it knows what
// a chord looks like in this chroma front end before this song has taught
// the decoder anything -- which is exactly when the hand triad is weakest.
static void corpus_chord_probs(const BeatChromaCache* cache, int bi, float p24[24]) {
    float x[CHORD_MODEL_DIM];
    auto put = [&](int off, const float* v) {
        double n = 0; for (int k = 0; k < 12; k++) n += (double)v[k] * v[k];
        float inv = n > 1e-12 ? (float)(1.0 / sqrt(n)) : 0.0f;
        for (int k = 0; k < 12; k++) x[off + k] = v[k] * inv;
    };
    put(0, cache->entries[bi].v);
#if CHORD_MODEL_CONTEXT
    {
        float ctx[12];
        int a = bi > 0 ? bi - 1 : bi, b = bi + 1 < (int)cache->entries.size() ? bi + 1 : bi;
        float na[12], nb[12];
        put(0, cache->entries[a].v); memcpy(na, x, sizeof(na));
        put(0, cache->entries[b].v); memcpy(nb, x, sizeof(nb));
        for (int k = 0; k < 12; k++) ctx[k] = 0.5f * (na[k] + nb[k]);
        put(0, cache->entries[bi].v);
        memcpy(x + 12, ctx, sizeof(ctx));
    }
#endif
    float s[24], mx = -1e30f;
    for (int q = 0; q < 2; q++)
        for (int r = 0; r < 12; r++) {
            float v = CHORD_MODEL_B[q];
            for (int blk = 0; blk < CHORD_MODEL_DIM / 12; blk++)
                for (int k = 0; k < 12; k++)
                    v += CHORD_MODEL_W[q][blk * 12 + k] * x[blk * 12 + (k + r) % 12];
            s[q * 12 + r] = v;
            if (v > mx) mx = v;
        }
    double sum = 0;
    for (int c = 0; c < 24; c++) { p24[c] = expf(s[c] - mx); sum += p24[c]; }
    for (int c = 0; c < 24; c++) p24[c] = (float)(p24[c] / sum);
}

// The song's key: from its mapped chords when it has enough of them (the
// major or minor key whose diatonic triads cover the most chord beats), else
// Krumhansl-Schmuckler correlation of the mean chroma with the key profiles.
// Returns false when neither is available.
static bool diatonic(int tonic, bool key_minor, int root, bool minor) {
    int d = ((root - tonic) % 12 + 12) % 12;
    if (!key_minor) return minor ? (d == 2 || d == 4 || d == 9) : (d == 0 || d == 5 || d == 7);
    // natural minor, plus the harmonic-minor dominant
    return minor ? (d == 0 || d == 5 || d == 7) : (d == 3 || d == 8 || d == 10 || d == 7);
}

static bool estimate_key(const CompleteInputs& in, const BeatChromaCache* cache, int* tonic, bool* key_minor) {
    const BeatMap* bm = in.beatmap;
    const MiscMap* cm = in.chordmap;
    if (cm->count >= 3) {
        double best = -1; int bt = 0; bool bmin = false;
        for (int k = 0; k < 24; k++) {
            int t = k % 12; bool mi = k >= 12;
            double cover = 0, tonic_w = 0;
            for (int i = 0; i < cm->count; i++) {
                char name[32]; strncpy(name, cm->entries[i].text, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
                char* sp = strchr(name, ' '); if (sp) *sp = 0;
                int r; bool m2;
                if (!parse_chord(name, &r, &m2)) continue;
                int bs = nearest_beat(bm, cm->entries[i].t_start), be = nearest_beat(bm, cm->entries[i].t_end);
                double w = be > bs ? be - bs : 1;
                if (diatonic(t, mi, r, m2)) cover += w;
                if (r == t && m2 == mi) tonic_w += w;
            }
            double sc = cover + 0.5 * tonic_w;     // the tonic itself breaks relative-key ties
            if (sc > best) { best = sc; bt = t; bmin = mi; }
        }
        if (best > 0) { *tonic = bt; *key_minor = bmin; return true; }
    }
    if (cache->entries.empty()) return false;
    static const float KS_MAJ[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float KS_MIN[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    double mean[12] = {};
    for (const BeatChromaEntry& e : cache->entries) for (int k = 0; k < 12; k++) mean[k] += e.v[k];
    double best = -2; int bt = 0; bool bmin = false;
    for (int k = 0; k < 24; k++) {
        const float* prof = k >= 12 ? KS_MIN : KS_MAJ;
        int t = k % 12;
        double dot = 0, na = 0, nb = 0, ma = 0, mb = 0;
        for (int q = 0; q < 12; q++) { ma += mean[q] / 12; mb += prof[q] / 12.0; }
        for (int q = 0; q < 12; q++) {
            double a = mean[(q + t) % 12] - ma, b = prof[q] - mb;
            dot += a * b; na += a * a; nb += b * b;
        }
        double r = (na > 0 && nb > 0) ? dot / sqrt(na * nb) : -2;
        if (r > best) { best = r; bt = t; bmin = k >= 12; }
    }
    if (best <= -2) return false;
    *tonic = bt; *key_minor = bmin;
    return true;
}

// Viterbi decode over beat intervals [i0, i1) of the map.  out_label[i] = model
// index or -1 (beats with no chroma).
static void decode_chords(const CompleteInputs& in, const CompleteParams& p,
                          const BeatChromaCache* cache, const std::vector<ChordModel>& models,
                          int i0, int i1, int ts, int measure_anchor, std::vector<int>* out_label,
                          const std::vector<int>* ext_model = nullptr,
                          const std::vector<float>* ext_conf = nullptr)
{
    int n = i1 - i0, M = (int)models.size();
    out_label->assign(n, -1);
    if (n <= 0 || M == 0) return;
    // Emissions
    std::vector<float> em((size_t)n * M);
    // Which corpus-model class each model is, when it is a plain triad
    std::vector<int> cls(M, -1);
    bool any_seen = false;
    for (int m = 0; m < M; m++) if (models[m].seen) any_seen = true;
    const float cw = any_seen ? p.chord_corpus_weight : p.chord_corpus_weight_cold;
    if (cw > 0.0f)
        for (int m = 0; m < M; m++) {
            int r; bool mi;
            if (parse_chord(models[m].text, &r, &mi)) cls[m] = r + (mi ? 12 : 0);
        }
    // Key: a bonus for the triads that belong to it.  A chord the song has
    // mapped is evidence of its own and is never marked down for being
    // outside the key, so the bonus goes only to unseen candidates.
    std::vector<float> keyb(M, 0.0f);
    if (p.chord_key_bonus > 0.0f) {
        int tonic; bool kmin;
        if (estimate_key(in, cache, &tonic, &kmin))
            for (int m = 0; m < M; m++) {
                int r; bool mi;
                if (!models[m].seen && parse_chord(models[m].text, &r, &mi) && diatonic(tonic, kmin, r, mi))
                    keyb[m] = p.chord_key_bonus;
            }
    }
    for (int i = 0; i < n; i++) {
        int bi = i0 + i;
        const float* v = bi < (int)cache->entries.size() ? cache->entries[bi].v : nullptr;
        float p24[24];
        if (cw > 0.0f && v) corpus_chord_probs(cache, bi, p24);
        for (int m = 0; m < M; m++) {
            float s = v ? chroma_cosine(v, models[m].v) : 0.0f;
            if (cw > 0.0f && v && cls[m] >= 0) s = (1.0f - cw) * s + cw * p24[cls[m]];
            if (!models[m].seen) s -= p.chord_unseen_penalty;
            s += keyb[m];
            // External model vote: the learned recogniser's label for this
            // beat earns its matching model a bonus -- evidence blended with
            // the track's own chroma rather than trusted verbatim.
            if (ext_model && (*ext_model)[bi] == m)
                s += p.chord_external_blend * (ext_conf ? (*ext_conf)[bi] : 1.0f);
            em[(size_t)i * M + m] = s;
        }
    }
    std::vector<float> score((size_t)n * M, -1e30f);
    std::vector<int>   back((size_t)n * M, -1);
    for (int m = 0; m < M; m++) score[m] = em[m];
    for (int i = 1; i < n; i++) {
        bool measure_start = ts > 0 && ((i0 + i - measure_anchor) % ts + ts) % ts == 0;
        float change = p.chord_transition * (measure_start ? (1.0f - p.chord_measure_bonus) : 1.0f);
        // Best previous state overall, and per-state stay
        int   bp = 0; float bv = -1e30f;
        for (int m = 0; m < M; m++) if (score[(size_t)(i - 1) * M + m] > bv) { bv = score[(size_t)(i - 1) * M + m]; bp = m; }
        for (int m = 0; m < M; m++) {
            float stay = score[(size_t)(i - 1) * M + m];
            float sw   = bv - change;
            if (stay >= sw) { score[(size_t)i * M + m] = stay + em[(size_t)i * M + m]; back[(size_t)i * M + m] = m; }
            else            { score[(size_t)i * M + m] = sw   + em[(size_t)i * M + m]; back[(size_t)i * M + m] = bp; }
        }
    }
    int best = 0; float bv = -1e30f;
    for (int m = 0; m < M; m++) if (score[(size_t)(n - 1) * M + m] > bv) { bv = score[(size_t)(n - 1) * M + m]; best = m; }
    g_ref_best_score = bv;   // [ref] export the final Viterbi score so the harness can print it
    for (int i = n - 1; i >= 0; i--) { (*out_label)[i] = best; best = back[(size_t)i * M + best]; if (best < 0) best = 0; }
}
