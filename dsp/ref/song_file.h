// C++ twin of dsp/SongFile.v3 + SynthSong.loadTiled/computeTruth (dsp/Synth.v3).
// Loads the SAME checked-in WAV clip as the Virgil benchmark and tiles it the same
// way, so the reference sees bit-identical input samples.
#pragma once
#include "synth_song.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct SongClip { int sampleRate = 0, channels = 0, frames = 0; std::vector<float> pcm; };

inline bool song_file_read(const char* path, SongClip* out) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    std::vector<unsigned char> d; unsigned char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    auto u16 = [&](size_t p) { return (int)d[p] | ((int)d[p + 1] << 8); };
    auto u32 = [&](size_t p) { return (int)((uint32_t)d[p] | ((uint32_t)d[p+1] << 8) | ((uint32_t)d[p+2] << 16) | ((uint32_t)d[p+3] << 24)); };
    if (d.size() < 12 || memcmp(d.data(), "RIFF", 4)) return false;
    size_t pos = 12; int sr = 0, ch = 0, bits = 0;
    while (pos + 8 <= d.size()) {
        int len = u32(pos + 4); size_t body = pos + 8;
        if (!memcmp(&d[pos], "fmt ", 4)) { ch = u16(body + 2); sr = u32(body + 4); bits = u16(body + 14); }
        else if (!memcmp(&d[pos], "data", 4)) {
            if (bits != 16 || ch == 0) return false;
            int cnt = len / 2;
            if (body + (size_t)cnt * 2 > d.size()) cnt = (int)((d.size() - body) / 2);
            out->sampleRate = sr; out->channels = ch; out->frames = cnt / ch;
            out->pcm.resize((size_t)out->frames * ch);
            for (size_t i = 0; i < out->pcm.size(); i++) {
                int v = u16(body + 2 * i); if (v >= 32768) v -= 65536;
                out->pcm[i] = (float)v / 32768.0f;
            }
            return true;
        }
        pos = body + len + (len & 1);
    }
    return false;
}

// Ground truth only (frames, beatTimes, chordAt), as SynthSong.computeTruth().
inline void song_compute_truth(SynthSong& s) {
    s.frames = (int)(s.seconds * s.sampleRate);
    double period = 60.0 / s.bpm;
    int nbeats = (int)(s.seconds / period);
    s.beatTimes.assign(nbeats, 0.0); s.chordAt.assign(nbeats, 0);
    for (int b = 0; b < nbeats; b++) {
        s.beatTimes[b] = b * period;
        int ci = (b / s.beatsPerChord) % (int)s.roots.size();
        s.chordAt[b] = s.roots[ci] + (s.minors[ci] ? 12 : 0);
    }
}

// As SynthSong.loadTiled(path): s.seconds is the wanted length on entry.
inline void song_load_tiled(SynthSong& s, const char* path) {
    SongClip clip;
    if (!song_file_read(path, &clip)) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    if (clip.sampleRate != s.sampleRate || clip.channels != s.channels) { fprintf(stderr, "sr/ch mismatch\n"); exit(1); }
    double want = s.seconds; int cf = clip.frames, ch = s.channels;
    double clipSec = (double)cf / s.sampleRate;
    s.seconds = clipSec; song_compute_truth(s);
    std::vector<double> cb = s.beatTimes; std::vector<int> cc = s.chordAt;
    s.seconds = want;
    s.frames = (int)(want * s.sampleRate);
    s.pcm.assign((size_t)s.frames * ch, 0.0f);
    size_t n = (size_t)cf * ch, j = 0;
    for (size_t i = 0; i < s.pcm.size(); i++) { s.pcm[i] = clip.pcm[j]; if (++j == n) j = 0; }
    s.beatTimes.clear(); s.chordAt.clear();
    for (int k = 0; k * clipSec < want; k++)
        for (size_t b = 0; b < cb.size(); b++) {
            double t = k * clipSec + cb[b];
            if (t < want) { s.beatTimes.push_back(t); s.chordAt.push_back(cc[b]); }
        }
}
