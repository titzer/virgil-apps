// Minimal Dear ImGui stub so spectrogram.cpp compiles (reference harness only).
#pragma once
#include <cstdint>
struct ImVec2 { float x, y; ImVec2(float a = 0, float b = 0) : x(a), y(b) {} };
typedef void* ImTextureID;
#define IM_COL32(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
struct ImDrawList {
    void AddRectFilled(ImVec2, ImVec2, uint32_t) {}
    void AddText(ImVec2, uint32_t, const char*) {}
    void AddImage(ImTextureID, ImVec2, ImVec2, ImVec2, ImVec2) {}
};
namespace ImGui { inline ImVec2 CalcTextSize(const char*) { return ImVec2(); } }
