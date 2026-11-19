#include "toast.h"

#include <windows.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace d2rcc::toast {
namespace {

constexpr int kMaxLines = 6;
constexpr ULONGLONG kShowMs = 9000;  // how long a line stays up
constexpr ULONGLONG kFadeMs = 1500;  // ... the last of it fading out
constexpr size_t kTextLen = 96;

struct Line {
  char text[kTextLen];
  uint8_t r, g, b;
  ULONGLONG at;
};

SRWLOCK g_lock = SRWLOCK_INIT;
Line g_lines[kMaxLines];  // under g_lock, the oldest first
int g_count = 0;          // under g_lock

// Under g_lock: the lines whose time is up go.
void prune(ULONGLONG now) {
  int keep = 0;
  for (int i = 0; i < g_count; ++i)
    if (now - g_lines[i].at < kShowMs) g_lines[keep++] = g_lines[i];
  g_count = keep;
}

}  // namespace

void push(const char* text, uint8_t r, uint8_t g, uint8_t b) {
  if (!text || !text[0]) return;
  const ULONGLONG now = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  prune(now);
  if (g_count == kMaxLines) {
    std::memmove(&g_lines[0], &g_lines[1], sizeof(Line) * (kMaxLines - 1));
    g_count = kMaxLines - 1;
  }
  Line& l = g_lines[g_count++];
  std::snprintf(l.text, sizeof(l.text), "%s", text);
  l.r = r;
  l.g = g;
  l.b = b;
  l.at = now;
  ReleaseSRWLockExclusive(&g_lock);
}

bool any() {
  const ULONGLONG now = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  prune(now);
  const bool up = g_count > 0;
  ReleaseSRWLockExclusive(&g_lock);
  return up;
}

void draw(ImFont* font) {
  Line lines[kMaxLines];
  const ULONGLONG now = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  prune(now);
  const int count = g_count;
  for (int i = 0; i < count; ++i) lines[i] = g_lines[i];
  ReleaseSRWLockExclusive(&g_lock);
  if (!count) return;
  if (!font) font = ImGui::GetFont();
  if (!font) return;
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  if (screen.x <= 0.0f || screen.y <= 0.0f) return;
  float size = screen.y * 0.026f;  // 37 pixels on a screen of 1440 lines
  if (size < 18.0f) size = 18.0f;
  if (size > 60.0f) size = 60.0f;
  const float step = std::floor(size * 1.3f);
  const float shadow = size >= 30.0f ? 2.0f : 1.0f;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  float y = std::floor(screen.y * 0.18f);
  for (int i = 0; i < count; ++i) {
    const Line& l = lines[i];
    const ULONGLONG age = now - l.at;  // below kShowMs: prune() ran with the same `now`
    float alpha = 1.0f;
    if (age + kFadeMs > kShowMs) alpha = static_cast<float>(kShowMs - age) / static_cast<float>(kFadeMs);
    const int a = static_cast<int>(alpha * 255.0f);
    if (a > 0) {
      const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, l.text);
      const ImVec2 at(std::floor((screen.x - ts.x) * 0.5f), y);
      dl->AddText(font, size, ImVec2(at.x + shadow, at.y + shadow), IM_COL32(0, 0, 0, (a * 9) / 10), l.text);
      dl->AddText(font, size, at, IM_COL32(l.r, l.g, l.b, a), l.text);
    }
    y += step;
  }
}

void clear() {
  AcquireSRWLockExclusive(&g_lock);
  g_count = 0;
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace d2rcc::toast
