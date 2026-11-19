#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// The plugin's own icons on the automap (maploot.cpp): a gold star for a unique item, a green gem for a set item, an
// orange rune stone for a rune; and a red dot for an enemy, ringed in orange for a named one (mapmarks.cpp). The automap's art has a chest (the coin chest, the sparkle chest) and its eight unit
// crosses, nothing that says unique, set or rune (approved 2026-11-02: "it's important that each cheat has a unique icon
// for what it shows"). They are painted here as RGBA, put in the game's HD sprite format, registered with D2RLoader's
// Resource service under the plugin's own folder, and loaded by the game's own sprite loader, which looks for a name
// under data/hd/global/ui/ with ".sprite" after it (".lowend.sprite" in the game's low-end texture mode). The rules
// (the shapes, the file) are here for tests/test_maploot.cpp.
namespace d2rcc::mapicons {

enum Icon : int { kUnique = 0, kSet, kRune, kEnemy, kNamedEnemy, kIcons };
constexpr const char* kIconNames[kIcons] = {"unique", "set", "rune", "enemy", "named"};
// An enemy's dot (mapmarks.cpp; asked 2026-11-03, "I want red dots instead of crosses", then "the dots for normal
// enemies are too small. Instead of a size difference maybe put an orange border around the named ones to make them
// look a little bigger", and "the orange ring is not noticable enough, make it larger"): the same red disc for every
// enemy, a named enemy's ringed in orange, both drawn at this much of the icons' size (the disc and its rim some 19
// pixels across at the automap's own scale, the ring's orange band some 5 pixels wide, 32 across in all).
constexpr float kEnemyDotScale = 0.58f;
constexpr float kDotRadius = 0.24f;   // the red disc, of the icon's size (both icons)
constexpr float kRingRadius = 0.49f;  // a named enemy's orange ring, with its rim, reaches out to here
constexpr int kSize = 56;         // HD pixels, square; the low-end file is half as large
constexpr int kLowEndSize = kSize / 2;
// The sprite loader's name: a path under data/hd/global/ui/, with neither the folder nor the extension. The files go in
// the plugin's own namespace there (d2rloader/<plugin id>/, the Resource service's rule).
#ifdef D2RCC_RUNES_ONLY
constexpr const char* kSpriteFolder = "d2rloader/rune-marker/mapicons/";  // the runes-only build's own plugin id
#else
constexpr const char* kSpriteFolder = "d2rloader/cabbycodes/mapicons/";
#endif
constexpr const char* kLoaderRoot = "data/hd/global/ui/";

// The game's HD sprite file ("SpA1": the .sprite files under data/hd/global/ui). A 0x28-byte header, then the frames
// side by side, rows of straight-alpha RGBA: +0x04 u16 version (31), +0x06 u16 a frame's width, +0x08 the width of all
// the frames, +0x0C the height, +0x14 the frame count, +0x20 the bytes of pixels, +0x24 4. What the game's own files
// hold (the automap's units, a rune's picture), read in them.
constexpr char kSpriteMagic[4] = {'S', 'p', 'A', '1'};
constexpr uint16_t kSpriteVersion = 31;
constexpr size_t kSpriteHeader = 0x28;

inline void put16(std::vector<uint8_t>& f, size_t at, uint16_t v) { std::memcpy(f.data() + at, &v, sizeof(v)); }
inline void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) { std::memcpy(f.data() + at, &v, sizeof(v)); }

// One frame of width x height RGBA pixels as a sprite file.
inline std::vector<uint8_t> sprite_file(const uint8_t* rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) return {};
  const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  std::vector<uint8_t> f(kSpriteHeader + pixels, 0);
  std::memcpy(f.data(), kSpriteMagic, sizeof(kSpriteMagic));
  put16(f, 0x04, kSpriteVersion);
  put16(f, 0x06, static_cast<uint16_t>(width));
  put32(f, 0x08, static_cast<uint32_t>(width));
  put32(f, 0x0C, static_cast<uint32_t>(height));
  put32(f, 0x14, 1);
  put32(f, 0x20, static_cast<uint32_t>(pixels));
  put32(f, 0x24, 4);
  std::memcpy(f.data() + kSpriteHeader, rgba, pixels);
  return f;
}

// --- the painting -------------------------------------------------------------------------------------------------

struct Rgb {
  float r, g, b;
};
struct Pt {
  float x, y;
};

// The distance from a point to a polygon's edge, negative inside it.
inline float polygon_distance(float px, float py, const Pt* pts, int n) {
  float best = 1e9f;
  bool inside = false;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    const Pt a = pts[j], b = pts[i];
    const float ex = b.x - a.x, ey = b.y - a.y, wx = px - a.x, wy = py - a.y;
    const float len2 = ex * ex + ey * ey;
    float t = len2 > 0.0f ? (wx * ex + wy * ey) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float dx = wx - ex * t, dy = wy - ey * t;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d < best) best = d;
    if ((a.y > py) != (b.y > py) && px < (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside ? -best : best;
}

// The distance from a point to a line segment.
inline float segment_distance(float px, float py, Pt a, Pt b) {
  const float ex = b.x - a.x, ey = b.y - a.y, wx = px - a.x, wy = py - a.y;
  const float len2 = ex * ex + ey * ey;
  float t = len2 > 0.0f ? (wx * ex + wy * ey) / len2 : 0.0f;
  t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  const float dx = wx - ex * t, dy = wy - ey * t;
  return std::sqrt(dx * dx + dy * dy);
}

constexpr float kPi = 3.14159265358979f;

// An icon's shape, in units of its size (0 .. 1 across): the outline and the fill, top to bottom.
struct Shape {
  Pt pts[12];
  int n = 0;
  Pt inner[12];  // a lighter facet (the gem's table), none when n_inner is 0
  int n_inner = 0;
  Pt strokes[3][2];  // dark strokes over the fill (the rune's glyph)
  int n_strokes = 0;
  Rgb top, bottom, outline, mark;
};

inline Shape shape_of(Icon icon) {
  Shape s;
  if (icon == kUnique) {
    // A five-pointed star, gold: the game's text color for a unique item (199, 179, 119), lighter at its top.
    for (int k = 0; k < 10; ++k) {
      const float r = (k % 2 == 0) ? 0.47f : 0.205f;
      const float a = -kPi / 2.0f + static_cast<float>(k) * kPi / 5.0f;
      s.pts[k] = Pt{0.5f + r * std::cos(a), 0.53f + r * std::sin(a)};
    }
    s.n = 10;
    s.top = Rgb{255, 236, 170};
    s.bottom = Rgb{199, 160, 70};
    s.outline = Rgb{40, 26, 6};
  } else if (icon == kSet) {
    // A gem cut as a hexagon, green: the game's text color for a set item (0, 252, 0), with a lighter table.
    for (int k = 0; k < 6; ++k) {
      const float a = kPi / 6.0f + static_cast<float>(k) * kPi / 3.0f;
      s.pts[k] = Pt{0.5f + 0.43f * std::cos(a), 0.5f + 0.43f * std::sin(a)};
      s.inner[k] = Pt{0.5f + 0.2f * std::cos(a), 0.5f + 0.2f * std::sin(a)};
    }
    s.n = 6;
    s.n_inner = 6;
    s.top = Rgb{130, 255, 130};
    s.bottom = Rgb{0, 150, 0};
    s.outline = Rgb{4, 36, 4};
  } else {
    // A rune stone, orange: the game's text color for a rune (255, 168, 0), with a dark glyph cut in it.
    const Pt stone[6] = {{0.22f, 0.12f}, {0.72f, 0.08f}, {0.83f, 0.40f}, {0.76f, 0.88f}, {0.27f, 0.92f}, {0.18f, 0.55f}};
    for (int k = 0; k < 6; ++k) s.pts[k] = stone[k];
    s.n = 6;
    s.strokes[0][0] = Pt{0.43f, 0.24f};
    s.strokes[0][1] = Pt{0.43f, 0.78f};
    s.strokes[1][0] = Pt{0.43f, 0.29f};
    s.strokes[1][1] = Pt{0.64f, 0.40f};
    s.strokes[2][0] = Pt{0.43f, 0.47f};
    s.strokes[2][1] = Pt{0.64f, 0.58f};
    s.n_strokes = 3;
    s.top = Rgb{255, 205, 90};
    s.bottom = Rgb{218, 122, 0};
    s.outline = Rgb{46, 22, 0};
    s.mark = Rgb{74, 36, 0};
  }
  return s;
}

// An enemy's dot painted at size x size (4 samples a pixel): a red disc, the automap's red of an enemy (its marker 5),
// lighter at its top, and a dark rim around what is drawn; a named enemy's has an orange ring between the disc and the
// rim (the game's orange, a rune's text color 255, 168, 0, lighter at its top) and a thin dark line between the two. The
// disc is the same in both, so both are drawn at one scale and the ring alone makes a named enemy's the larger.
inline std::vector<uint8_t> paint_dot(bool named, int size) {
  std::vector<uint8_t> out(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 0);
  if (size <= 0) return out;
  const float scale = static_cast<float>(size);
  const float centre = scale / 2.0f;
  const float rim = scale * 0.045f + 0.5f;  // the dark rim's width, in pixels
  const float disc = kDotRadius * scale;
  const float line = named ? scale * 0.02f + 0.3f : 0.0f;  // between the disc and the ring
  const float outer = named ? kRingRadius * scale : disc + rim;
  const Rgb dark = named ? Rgb{56, 26, 0} : Rgb{48, 0, 0};
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (int sy = 0; sy < 2; ++sy)
        for (int sx = 0; sx < 2; ++sx) {
          const float px = static_cast<float>(x) + 0.25f + 0.5f * static_cast<float>(sx);
          const float py = static_cast<float>(y) + 0.25f + 0.5f * static_cast<float>(sy);
          const float d = std::sqrt((px - centre) * (px - centre) + (py - centre) * (py - centre));
          if (d >= outer) continue;
          const float t = py / scale;
          Rgb c = dark;
          if (d < disc)
            c = Rgb{255.0f + (200.0f - 255.0f) * t, 90.0f + (16.0f - 90.0f) * t, 80.0f + (12.0f - 80.0f) * t};
          else if (named && d >= disc + line && d < outer - rim)
            c = Rgb{255.0f + (240.0f - 255.0f) * t, 206.0f + (126.0f - 206.0f) * t, 80.0f + (0.0f - 80.0f) * t};
          acc[0] += c.r;
          acc[1] += c.g;
          acc[2] += c.b;
          acc[3] += 255.0f;
        }
      uint8_t* p = out.data() + (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4;
      if (acc[3] <= 0.0f) {
        p[0] = static_cast<uint8_t>(dark.r);
        p[1] = static_cast<uint8_t>(dark.g);
        p[2] = static_cast<uint8_t>(dark.b);
        continue;
      }
      const float n = acc[3] / 255.0f;
      p[0] = static_cast<uint8_t>(acc[0] / n + 0.5f);
      p[1] = static_cast<uint8_t>(acc[1] / n + 0.5f);
      p[2] = static_cast<uint8_t>(acc[2] / n + 0.5f);
      p[3] = static_cast<uint8_t>(acc[3] / 4.0f + 0.5f);
    }
  return out;
}

// An icon painted at size x size: 4 samples a pixel, a dark outline around the fill so it reads on any part of the
// map. A transparent pixel keeps the outline's color, so that what the game's filtering blends in at the edge is dark.
inline std::vector<uint8_t> paint(Icon icon, int size) {
  if (icon == kEnemy || icon == kNamedEnemy) return paint_dot(icon == kNamedEnemy, size);
  std::vector<uint8_t> out(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 0);
  if (size <= 0 || icon < 0 || icon >= kIcons) return out;
  const Shape s = shape_of(icon);
  const float scale = static_cast<float>(size);
  Pt pts[12], inner[12];
  for (int k = 0; k < s.n; ++k) pts[k] = Pt{s.pts[k].x * scale, s.pts[k].y * scale};
  for (int k = 0; k < s.n_inner; ++k) inner[k] = Pt{s.inner[k].x * scale, s.inner[k].y * scale};
  const float outline = scale * 0.055f + 0.6f;  // its width, in pixels
  const float stroke = scale * 0.045f + 0.4f;   // the glyph's half width
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (int sy = 0; sy < 2; ++sy)
        for (int sx = 0; sx < 2; ++sx) {
          const float px = static_cast<float>(x) + 0.25f + 0.5f * static_cast<float>(sx);
          const float py = static_cast<float>(y) + 0.25f + 0.5f * static_cast<float>(sy);
          const float d = polygon_distance(px, py, pts, s.n);
          if (d >= 0.0f) continue;
          Rgb c = s.outline;
          if (d < -outline) {
            const float t = py / scale;
            c = Rgb{s.top.r + (s.bottom.r - s.top.r) * t, s.top.g + (s.bottom.g - s.top.g) * t,
                    s.top.b + (s.bottom.b - s.top.b) * t};
            if (s.n_inner && polygon_distance(px, py, inner, s.n_inner) < 0.0f)
              c = Rgb{std::fmin(255.0f, c.r * 1.25f + 30.0f), std::fmin(255.0f, c.g * 1.25f + 30.0f),
                      std::fmin(255.0f, c.b * 1.25f + 30.0f)};
            for (int k = 0; k < s.n_strokes; ++k) {
              const Pt a{s.strokes[k][0].x * scale, s.strokes[k][0].y * scale};
              const Pt b{s.strokes[k][1].x * scale, s.strokes[k][1].y * scale};
              if (segment_distance(px, py, a, b) < stroke) c = s.mark;
            }
          }
          acc[0] += c.r;
          acc[1] += c.g;
          acc[2] += c.b;
          acc[3] += 255.0f;
        }
      uint8_t* p = out.data() + (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4;
      if (acc[3] <= 0.0f) {
        p[0] = static_cast<uint8_t>(s.outline.r);
        p[1] = static_cast<uint8_t>(s.outline.g);
        p[2] = static_cast<uint8_t>(s.outline.b);
        p[3] = 0;
        continue;
      }
      const float n = acc[3] / 255.0f;  // the samples inside
      p[0] = static_cast<uint8_t>(acc[0] / n + 0.5f);
      p[1] = static_cast<uint8_t>(acc[1] / n + 0.5f);
      p[2] = static_cast<uint8_t>(acc[2] / n + 0.5f);
      p[3] = static_cast<uint8_t>(acc[3] / 4.0f + 0.5f);
    }
  return out;
}

}  // namespace d2rcc::mapicons
