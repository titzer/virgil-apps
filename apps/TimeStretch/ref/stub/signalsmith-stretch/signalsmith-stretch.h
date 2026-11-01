// Stand-in for the vendored Signalsmith Stretch (not benchmarked): no-op
// methods so beatmapper's stretch.cpp compiles; the harness only uses the
// phase-vocoder engine (STRETCH_ENGINE_PV).
#pragma once
namespace signalsmith { namespace stretch {
template <typename T> struct SignalsmithStretch {
    void presetDefault(int, T) {}
    int inputLatency() const { return 0; }
    int outputLatency() const { return 0; }
    void setTransposeFactor(T, T = 0) {}
    void setFormantFactor(T, bool = false) {}
    int outputSeekLength(T) const { return 0; }
    template <class In> void outputSeek(In&&, int) {}
    template <class In, class Out> void process(In&&, int, Out&&, int) {}
};
}}
