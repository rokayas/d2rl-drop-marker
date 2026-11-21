#pragma once

#include <cmath>
#include <cstdint>

#include "imgui.h"

// The light pillar's look (a local addition, not CabbyCodes' own): a soft beam standing on a point of the screen, and
// nothing else (a glow and a spreading ring on the ground at its foot were tried and taken out: in the game they read
// as a thing apart from the pillar). Dear ImGui draw lists only, nothing of the game or the overlay in it, so that
// tests/test_trackmenu.cpp can paint it to a picture without the game. Where the pillars stand is pillars.cpp's.
namespace d2rcc::pillarart {

struct Color {
  uint8_t r, g, b;
};
// A rune's pillar in the game's rune orange, another rare drop's in gold (the colours of their marks on the map).
constexpr Color kRuneColor{255, 150, 30};
constexpr Color kOtherColor{255, 214, 110};
constexpr Color kSetColor{70, 235, 90};

// What a beam is made of, in pixels of a screen 1080 lines high (everything scales with the screen's height).
constexpr float kHeight = 340.0f;     // from its foot to where it has faded away
constexpr float kGlowHalf = 30.0f;    // half the width of its soft light
constexpr float kCoreHalf = 3.5f;     // ... and of its bright core
constexpr float kShadeHalf = kGlowHalf * 1.3f;  // ... and of the faint shade around it: the widest it gets
constexpr int kSteps = 14;            // the beam's pieces, foot to top
constexpr float kPulseSeconds = 2.6f;

// How bright the beam is at a height (0 its foot, 1 its top): full at the foot, fading out toward the top.
inline float height_alpha(float v) {
  if (v <= 0.0f) return 1.0f;
  if (v >= 1.0f) return 0.0f;
  const float f = 1.0f - v;
  return f * std::sqrt(f);
}

inline ImU32 rgba(Color c, float a) {
  const int ai = a <= 0.0f ? 0 : a >= 1.0f ? 255 : static_cast<int>(a * 255.0f + 0.5f);
  return IM_COL32(c.r, c.g, c.b, ai);
}
// The colour pulled toward white, for the core.
constexpr Color lighten(Color c, float by) {
  return Color{static_cast<uint8_t>(c.r + (255 - c.r) * by), static_cast<uint8_t>(c.g + (255 - c.g) * by),
               static_cast<uint8_t>(c.b + (255 - c.b) * by)};
}

// One layer of the beam: brightest on the axis, fading to nothing `half` to either side, and with the height.
inline void layer(ImDrawList* dl, float x, float foot, float height, float half, Color c, float alpha) {
  for (int i = 0; i < kSteps; ++i) {
    const float v0 = static_cast<float>(i) / kSteps, v1 = static_cast<float>(i + 1) / kSteps;
    const float y0 = foot - height * v0, y1 = foot - height * v1;  // y1 above y0
    const ImU32 lower = rgba(c, alpha * height_alpha(v0)), upper = rgba(c, alpha * height_alpha(v1));
    const ImU32 none = rgba(c, 0.0f);
    // The corners in Dear ImGui's order: upper left, upper right, bottom right, bottom left.
    dl->AddRectFilledMultiColor(ImVec2(x - half, y1), ImVec2(x, y0), none, upper, lower, none);
    dl->AddRectFilledMultiColor(ImVec2(x, y1), ImVec2(x + half, y0), upper, none, none, lower);
  }
  // Below the foot it fades out over a short way, so that it ends in no hard edge on the ground.
  const float skirt = half * 0.6f;
  const ImU32 at_foot = rgba(c, alpha), none = rgba(c, 0.0f);
  dl->AddRectFilledMultiColor(ImVec2(x - half, foot), ImVec2(x, foot + skirt), none, at_foot, none, none);
  dl->AddRectFilledMultiColor(ImVec2(x, foot), ImVec2(x + half, foot + skirt), at_foot, none, none, none);
}

// A pillar with its foot at (x, y). `unit`: the screen's height / 1080; `seconds`: a clock for the slow pulse of its
// brightness (any start; give each pillar its own offset and they do not beat together); `alpha`: 0 .. 1 over all of it.
inline void beam(ImDrawList* dl, float x, float y, float unit, Color c, double seconds, float alpha = 1.0f) {
  if (!dl || unit <= 0.0f || alpha <= 0.0f) return;
  const float two_pi = 6.2831853f;
  const float pulse = 0.86f + 0.14f * std::sin(two_pi * static_cast<float>(std::fmod(seconds, kPulseSeconds) / kPulseSeconds));
  const float a = alpha * pulse;
  const float height = kHeight * unit, glow = kGlowHalf * unit, core = kCoreHalf * unit;
  // The beam: a faint shade around it (what sets it off from snow and sand; it does not show on dark ground), wide
  // soft light, a tighter glow, and the core nearly white.
  layer(dl, x, y, height, kShadeHalf * unit, Color{0, 0, 0}, a * 0.22f);
  layer(dl, x, y, height, glow, c, a * 0.30f);
  layer(dl, x, y, height * 0.96f, glow * 0.45f, c, a * 0.50f);
  layer(dl, x, y, height * 0.92f, core * 2.6f, lighten(c, 0.45f), a * 0.85f);
  layer(dl, x, y, height * 0.88f, core < 1.0f ? 1.0f : core, lighten(c, 0.85f), a);
}

}  // namespace d2rcc::pillarart
