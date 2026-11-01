// Reference harness for apps/TimeStretch: runs beatmapper's ORIGINAL
// stretch.cpp phase-vocoder engine (compiled unmodified) on the same input
// clip as TimeStretch.v3 and prints the same summary and checks.
//
//   stretch_ref <song.wav> <centiseconds> [settings] [fftsize] [virgil-dump-prefix]
//
// fftsize must be 0 (stretch.cpp picks the FFT size itself).  With a dump
// prefix (files written by `TimeStretch ... <prefix>`, raw little-endian f32
// <prefix><run>.f32), each run's output is also compared sample by sample
// with the Virgil output (printed as "  vs virgil: ..." lines).
#include "stretch.h"
#include "song_file.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const double PI = 3.14159265358979323846;
static const int BLOCK = 512;

static std::vector<float> render(SynthSong& song, float speed, float pitch, int* nframes) {
    StretchSource ss;
    stretch_init(&ss, song.pcm.data(), song.frames, song.channels, song.sampleRate, false);
    stretch_set_speed(&ss, speed);
    stretch_set_pitch(&ss, pitch);
    const ma_data_source_vtable* vt = ss.base.vtable;
    vt->onSeek(&ss, 0);     // restart at frame 0 with this speed/pitch
    std::vector<float> out;
    std::vector<float> buf(BLOCK * song.channels);
    int total = 0;
    for (;;) {
        ma_uint64 n = 0;
        vt->onRead(&ss, buf.data(), BLOCK, &n);
        out.insert(out.end(), buf.begin(), buf.begin() + n * song.channels);
        total += (int)n;
        if ((int)n < BLOCK) break;
    }
    stretch_uninit(&ss);
    *nframes = total;
    return out;
}

static double rms(const std::vector<float>& a, int frames, int ch, int c) {
    double sum = 0.0;
    for (int i = 0; i < frames; i++) { double v = a[i * ch + c]; sum += v * v; }
    return frames > 0 ? std::sqrt(sum / frames) : 0.0;
}

// ---- pitch-shift estimator (same algorithm as TimeStretch.v3) ----------------
static const int CN = 4096, LSTEP = 5, LBINS = 2160, LBAND_LO = 240, LBAND_HI = 1920, SEARCH = 20;
static const double LF0 = 25.0;

// Radix-2 FFT as in stretch.cpp's fft_run (which is static there).
static void fft(std::vector<float>& re, std::vector<float>& im) {
    static std::vector<float> cs, sn; static std::vector<int> rev;
    const int n = CN;
    if (cs.empty()) {
        cs.resize(n / 2); sn.resize(n / 2); rev.resize(n);
        for (int j = 0; j < n / 2; j++) { cs[j] = (float)cos(2.0 * PI * j / n); sn[j] = (float)-sin(2.0 * PI * j / n); }
        int bits = 0; while ((1 << bits) < n) bits++;
        for (int i = 0; i < n; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[i] = r; }
    }
    for (int i = 0; i < n; i++) { int j = rev[i]; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
    for (int len = 2, step = n / 2; len <= n; len <<= 1, step >>= 1) {
        int half = len >> 1;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; j++) {
                float wr = cs[j * step], wi = sn[j * step];
                int a = i + j, b = a + half;
                float vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
            }
    }
}

static std::vector<double> logSpectrum(const std::vector<float>& a, int frames, int ch, int sr) {
    std::vector<float> w(CN), re(CN), im(CN);
    for (int i = 0; i < CN; i++) w[i] = (float)(0.5 - 0.5 * cos(2.0 * PI * i / CN));
    std::vector<double> pw(CN / 2 + 1, 0.0);
    for (int start = 0; start < frames; start += CN / 2) {
        for (int i = 0; i < CN; i++) {
            int t = start + i; float v = 0.0f;
            if (t < frames) { v = a[t * ch]; if (ch > 1) v = (v + a[t * ch + 1]) * 0.5f; }
            re[i] = v * w[i]; im[i] = 0.0f;
        }
        fft(re, im);
        for (int k = 0; k <= CN / 2; k++) pw[k] += (double)re[k] * re[k] + (double)im[k] * im[k];
    }
    std::vector<double> ls(LBINS, 0.0);
    double step = pow(2.0, (double)LSTEP / 1200.0), f = LF0;
    for (int j = 0; j < LBINS; j++) {
        if (j > 0) f *= step;
        double x = f * CN / sr; int k = (int)x;
        if (k + 1 >= (int)pw.size()) break;
        double fr = x - k;
        ls[j] = sqrt(pw[k] + (pw[k + 1] - pw[k]) * fr);
    }
    return ls;
}

static int estimateCents(const std::vector<double>& in, const std::vector<double>& out, int wantCents) {
    int c0 = wantCents / LSTEP, bestLag = 0; double bestCorr = -1.0;
    for (int m = c0 - SEARCH; m <= c0 + SEARCH; m++) {
        double xy = 0, xx = 0, yy = 0;
        for (int j = LBAND_LO; j < LBAND_HI; j++) { double x = in[j], y = out[j + m]; xy += x * y; xx += x * x; yy += y * y; }
        double c = (xx > 0 && yy > 0) ? xy / sqrt(xx * yy) : 0.0;
        if (c > bestCorr) { bestCorr = c; bestLag = m; }
    }
    return bestLag * LSTEP;
}

static int failures = 0;
static void check(const char* what, bool ok) {
    printf("  check %s: %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("Usage: stretch_ref <song.wav> <centiseconds> [settings] [fftsize] [dump-prefix]\n"); return 1; }
    int cs = atoi(argv[2]);
    int nset = argc > 3 ? atoi(argv[3]) : 5;
    int fftSize = argc > 4 ? atoi(argv[4]) : 0;
    const char* dump = argc > 5 ? argv[5] : nullptr;
    if (fftSize != 0) { fprintf(stderr, "stretch_ref: stretch.cpp fixes the FFT size; fftsize must be 0\n"); return 1; }
    SynthSong song;
    song.seconds = (double)cs / 100.0;
    song_load_tiled(song, argv[1]);
    int ch = song.channels, sr = song.sampleRate;

    double inL = rms(song.pcm, song.frames, ch, 0), inR = rms(song.pcm, song.frames, ch, 1);
    std::vector<double> inS = logSpectrum(song.pcm, song.frames, ch, sr);
    printf("input frames: %d, rms %.5f %.5f\n", song.frames, inL, inR);

    float semis3 = (float)std::pow(2.0, 3.0 / 12.0);
    float speeds[]  = {1.0f, 0.75f, 1.25f, 1.0f, 0.8f};
    float pitches[] = {1.0f, 1.0f, 1.0f, semis3, 0.75f};
    if (nset > 5) nset = 5;
    for (int s = 0; s < nset; s++) {
        float sp = speeds[s], pt = pitches[s];
        int n = 0;
        std::vector<float> out = render(song, sp, pt, &n);
        double oL = rms(out, n, ch, 0), oR = rms(out, n, ch, 1);
        int wantCents = (int)std::round(1200.0 * std::log2((double)pt));
        int cents = estimateCents(inS, logSpectrum(out, n, ch, sr), wantCents);
        double expect = song.frames / (double)sp;
        printf("speed %.2f pitch %.4f: frames %d (expect %.1f), rms %.5f %.5f, shift %d cents, checksum %d\n",
               sp, pt, n, expect, oL, oR, cents, dsp_checksum(out.data(), 0, n * ch));
        double dn = n - expect;
        check("length within 1 frame of input/speed", dn > -1.0 && dn <= 1.0);
        check("rms within 10% of input (both channels)", fabs(oL / inL - 1.0) < 0.10 && fabs(oR / inR - 1.0) < 0.10);
        int dc = cents - wantCents;
        check("spectral shift within 10 cents of pitch", dc >= -10 && dc <= 10);
        if (sp == 1.0f && pt == 1.0f) {
            int len = std::min(n, song.frames) * ch;
            double maxErr = 0, err2 = 0, sig2 = 0;
            for (int i = 0; i < len; i++) {
                double d = std::fabs((double)out[i] - song.pcm[i]);
                if (d > maxErr) maxErr = d;
                err2 += d * d; sig2 += (double)song.pcm[i] * song.pcm[i];
            }
            double snr = err2 > 0 ? 10.0 * std::log10(sig2 / err2) : 999.0;
            check("identity length exact", n == song.frames);
            check("identity max abs error < 1e-4", maxErr < 1e-4);
            check("identity transparency SNR > 80 dB", snr > 80.0);
        }
        if (dump) {
            std::string path = std::string(dump) + std::to_string(s) + ".f32";
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) { printf("  vs virgil: cannot open %s\n", path.c_str()); continue; }
            std::vector<float> v; float x;
            while (fread(&x, sizeof(float), 1, f) == 1) v.push_back(x);
            fclose(f);
            int vn = (int)v.size() / ch, len = std::min(vn, n) * ch, exact = 0;
            double maxd = 0, d2 = 0, s2 = 0;
            for (int i = 0; i < len; i++) {
                double d = std::fabs((double)v[i] - out[i]);
                if (d == 0) exact++;
                if (d > maxd) maxd = d;
                d2 += d * d; s2 += (double)out[i] * out[i];
            }
            double snr = d2 > 0 ? 10.0 * std::log10(s2 / d2) : 999.0;
            printf("  vs virgil: frames %d vs %d (%s), bit-exact samples %.1f%%, max abs diff %.3g, snr %.1f dB\n",
                   n, vn, n == vn ? "equal" : "DIFFER", len ? 100.0 * exact / len : 100.0, maxd, snr);
        }
    }
    if (failures > 0) { printf("FAILED checks: %d\n", failures); return 1; }
    return 0;
}
