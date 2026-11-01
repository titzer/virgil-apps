// C++ twin of dsp/Synth.v3 (SynthSong + DspOut::checksum), for building
// reference harnesses that run the ORIGINAL beatmapper C++ on the exact same
// synthetic input as the Virgil benchmarks.  Keep in sync with Synth.v3.
// NOTE: samples match Synth.v3 to ~1 ulp (libm sin/exp/pow vs DspMath), NOT bit-exactly,
// so compare reference vs Virgil results with a tolerance (and discrete outputs such as
// beat counts / chord labels exactly), not by exact checksum.
// Smoke test: c++ -O2 -std=c++17 -o check check.cpp && ./check  (frames=220500 beats=20)
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

struct SynthSong {
    int    sampleRate = 22050;
    int    channels = 2;
    double bpm = 120.0;
    double seconds = 10.0;
    double a4 = 440.0;
    int    beatsPerChord = 4;
    std::vector<int>  roots  = {0, 9, 5, 7};          // C Am F G
    std::vector<bool> minors = {false, true, false, false};
    uint32_t seed = 12345u;

    std::vector<float>  pcm;        // interleaved
    int                 frames = 0;
    std::vector<double> beatTimes;
    std::vector<int>    chordAt;    // root + 12 if minor

    float rand_() {
        seed = seed * 1664525u + 1013904223u;
        return (float)(double)(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
    }
    double midiFreq(double midi) { return a4 * std::pow(2.0, (midi - 69.0) / 12.0); }

    SynthSong& generate() {
        const double PI = 3.14159265358979323846, TWO_PI = 6.28318530717958647692;
        frames = (int)(seconds * sampleRate);
        std::vector<float> mono(frames, 0.0f);
        double period = 60.0 / bpm;
        int nbeats = (int)(seconds / period);
        beatTimes.assign(nbeats, 0.0); chordAt.assign(nbeats, 0);
        double sr = sampleRate;
        for (int b = 0; b < nbeats; b++) {
            double t0 = b * period;
            beatTimes[b] = t0;
            int ci = (b / beatsPerChord) % (int)roots.size();
            int root = roots[ci]; bool minor = minors[ci];
            chordAt[b] = root + (minor ? 12 : 0);
            int s0 = (int)(t0 * sr), s1 = std::min(frames, (int)((t0 + period) * sr));
            int notes[4] = {60 + root, 60 + root + (minor ? 3 : 4), 60 + root + 7, 36 + root};
            double amps[4] = {0.10, 0.08, 0.08, 0.14};
            for (int n = 0; n < 4; n++) {
                double f = midiFreq(notes[n]);
                double w = TWO_PI * f / sr;
                for (int h = 1; h <= 4; h++) {
                    double wh = w * h;
                    if (wh >= PI) break;
                    double amp = amps[n] / h;
                    double s1r = std::sin(wh * s0), s2r = std::sin(wh * (s0 - 1));
                    double c2 = 2.0 * std::cos(wh);
                    for (int s = s0; s < s1; s++) {
                        double env = 1.0 - 0.4 * (double)(s - s0) / (double)(s1 - s0 + 1);
                        mono[s] += (float)(amp * env * s1r);
                        double nx = c2 * s1r - s2r;
                        s2r = s1r; s1r = nx;
                    }
                }
            }
        }
        double half = period / 2;
        int nhalf = (int)(seconds / half);
        for (int k = 0; k < nhalf; k++) {
            double t = k * half;
            int s0 = (int)(t * sr);
            int beat = k / 2; bool onBeat = (k % 2) == 0;
            if (onBeat && (beat % 2) == 0) kick(mono, s0, sr);
            if (onBeat && (beat % 2) == 1) snare(mono, s0, sr);
            hat(mono, s0, sr, onBeat ? 0.12f : 0.07f);
        }
        pcm.assign((size_t)frames * channels, 0.0f);
        for (int i = 0; i < frames; i++) {
            float v = mono[i] + 0.002f * rand_();
            if (channels == 1) pcm[i] = v;
            else {
                pcm[i * channels] = v * 0.9f;
                pcm[i * channels + 1] = v * 0.8f + 0.001f * rand_();
            }
        }
        return *this;
    }
    void kick(std::vector<float>& out, int s0, double sr) {
        const double TWO_PI = 6.28318530717958647692;
        int len = (int)(0.15 * sr); double ph = 0.0;
        for (int i = 0; i < len; i++) {
            int s = s0 + i; if (s >= (int)out.size()) break;
            double t = i / sr;
            double f = 50.0 + 100.0 * std::exp(-t * 30.0);
            ph += TWO_PI * f / sr;
            out[s] += (float)(0.6 * std::exp(-t * 18.0) * std::sin(ph));
        }
    }
    void snare(std::vector<float>& out, int s0, double sr) {
        const double TWO_PI = 6.28318530717958647692;
        int len = (int)(0.12 * sr);
        for (int i = 0; i < len; i++) {
            int s = s0 + i; if (s >= (int)out.size()) break;
            double t = i / sr;
            double env = std::exp(-t * 25.0);
            out[s] += (float)(env * (0.25 * (double)rand_() + 0.15 * std::sin(TWO_PI * 190.0 * t)));
        }
    }
    void hat(std::vector<float>& out, int s0, double sr, float amp) {
        int len = (int)(0.04 * sr); float prev = 0.0f;
        for (int i = 0; i < len; i++) {
            int s = s0 + i; if (s >= (int)out.size()) break;
            double t = i / sr;
            float n = rand_();
            float hp = n - prev; prev = n;
            out[s] += amp * (float)std::exp(-t * 90.0) * hp;
        }
    }
    std::vector<float> mono() const {
        std::vector<float> m(frames);
        for (int i = 0; i < frames; i++) {
            float sum = 0.0f;
            for (int c = 0; c < channels; c++) sum += pcm[i * channels + c];
            m[i] = sum / (float)channels;
        }
        return m;
    }
};

// Same as DspOut.checksum in Synth.v3 (int32 wraparound, quantized to 1e-4).
inline int32_t dsp_checksum(const float* a, int start, int len) {
    uint32_t h = 0;
    for (int i = start; i < start + len; i++) {
        int32_t q = (int32_t)std::nearbyint((double)a[i] * 10000.0);  // half-to-even, like Virgil double.round
        h = h * 31u + (uint32_t)q;
    }
    return (int32_t)h;
}
