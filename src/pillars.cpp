// The light pillars (pillars.h): a local addition.
#include "pillars.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

#include "cheats.h"
#include "game.h"
#include "imgui.h"
#include "log.h"
#include "maploot.h"
#include "pillarart.h"
#include "track.h"

namespace d2rcc::pillars {
namespace {

using game::Unit;

constexpr int kMaxPillars = 48;       // pillars on screen at once
constexpr size_t kMaxUnits = 8192;    // the client's items looked through (what the player carries and keeps is among them)
constexpr uint64_t kScanMs = 150;     // how often they are looked through; the pillars known are placed at every frame
constexpr uint64_t kStaleMs = 250;    // pillars placed longer ago are not drawn (the UI thread stopped coming)
constexpr int kLogFirst = 3;          // pillars written to the log in a game

struct Pillar {
  float x = 0.0f, y = 0.0f;  // its foot, in the projection's pixels
  uint8_t kind = 0;          // maploot::ItemKind: a rune or another rare drop
  uint32_t id = 0;           // the item's id: its own place in the pulse
};
struct Snapshot {
  uint64_t at = 0;
  int render_w = 0, render_h = 0;
  int side = 0;  // game::panel_side()
  int count = 0;
  Pillar pillar[kMaxPillars];
};
// A chosen item on the ground, from the last look through the client's items.
struct Known {
  uint32_t id = 0;
  uint8_t kind = 0;
  int8_t rune = 0;    // its number, 1 .. 33
  int8_t group = -1;  // ... or its group
};

volatile LONG g_bound = 0;
volatile LONG g_legacy = 0;  // the last frame found legacy graphics
volatile LONG g_reset = 0;   // a game was left: the UI thread forgets what it knew
const char* volatile g_why = "not looked for yet";

CRITICAL_SECTION g_cs;  // the snapshot: the UI thread publishes it, the present thread takes it
bool g_cs_ready = false;
Snapshot g_published;   // under g_cs

// The UI thread only.
Snapshot g_work;
Unit* g_units[kMaxUnits];
Known g_known[kMaxPillars];
int g_known_count = 0;
uint64_t g_scanned_at = 0;
bool g_showing = false;
int g_logged = 0;
bool g_said_no_point = false, g_said_full = false;

// The present thread only.
Snapshot g_view;

bool chosen(int rune, int group) { return group >= 0 ? track::pillar_group(group) : track::pillar_rune(rune); }

void publish(const Snapshot& s, uint64_t now) {
  EnterCriticalSection(&g_cs);
  g_published.at = now;
  g_published.render_w = s.render_w;
  g_published.render_h = s.render_h;
  g_published.side = s.side;
  g_published.count = s.count;
  std::memcpy(g_published.pillar, s.pillar, sizeof(Pillar) * static_cast<size_t>(s.count));
  LeaveCriticalSection(&g_cs);
}

// The client's items on the ground that are chosen now.
void scan() {
  g_known_count = 0;
  const size_t n = game::client_units(game::kItem, g_units, kMaxUnits);
  for (size_t i = 0; i < n; ++i) {
    int rune = 0, group = -1;
    const maploot::ItemKind kind = maploot::ground_drop(g_units[i], &rune, &group);
    if (kind == maploot::kItemNone || !chosen(rune, group)) continue;
    if (g_known_count == kMaxPillars) {
      if (!g_said_full) {
        g_said_full = true;
        logf("light pillars: more than %d chosen drops lie near you - the rest get no pillar", kMaxPillars);
      }
      break;
    }
    Known& k = g_known[g_known_count++];
    k.id = game::unit_id(g_units[i]);
    k.kind = static_cast<uint8_t>(kind);
    k.rune = static_cast<int8_t>(rune);
    k.group = static_cast<int8_t>(group);
  }
}

}  // namespace

void bind(bool ui_thread) {
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_cs);
    g_cs_ready = true;
  }
  const bool projection = game::has_unit_projection() || game::has_projection();
  const char* why = !ui_thread                    ? "no UI thread from the loader"
                    : !projection                 ? "the game's screen projection not found"
                    : !game::has_client_units()   ? "the game's list of items not found"
                                                  : nullptr;
  g_why = why;
  InterlockedExchange(&g_bound, why ? 0 : 1);
  logf("light pillars: %s%s", why ? why : "ready",
       why ? "" : game::has_unit_projection() ? " (a drop's place asked of the game's own projection)"
                                              : " (the projection under the above-head point not named: that point is used)");
}

const char* why_not() {
  if (const char* why = g_why) return why;
  return g_legacy ? "not shown in legacy graphics" : nullptr;
}

void collect(uint64_t now) {
  if (!g_bound) return;
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_known_count = 0;
    g_scanned_at = 0;
    g_logged = 0;
    g_said_no_point = g_said_full = false;
  }
  Snapshot& s = g_work;
  s.count = 0;
  s.render_w = s.render_h = 0;
  s.side = 0;
  // Nothing to place - the master switch off, no drop chosen, no game: the pillars shown last are taken away, once.
  const bool wanted = cheats::enabled(cheats::kMapRunes) && track::any_pillars() && game::current_level() > 0;
  const bool hd = wanted && game::hd_graphics();
  if (wanted && (g_legacy != 0) == hd) {
    InterlockedExchange(&g_legacy, hd ? 0 : 1);
    logf("light pillars: %s graphics%s", hd ? "HD" : "legacy", hd ? "" : " - no pillars (the game has no screen projection there)");
  }
  if (!hd || !game::render_size(&s.render_w, &s.render_h)) {
    if (g_showing) publish(s, now);
    g_showing = false;
    g_known_count = 0;
    g_scanned_at = 0;
    return;
  }
  if (now - g_scanned_at >= kScanMs) {
    scan();
    g_scanned_at = now;
  }
  for (int i = 0; i < g_known_count; ++i) {
    const Known& k = g_known[i];
    // Still there, and still chosen: a tick taken off in the menu, or the item picked up, takes its pillar away at once.
    if (!chosen(k.rune, k.group)) continue;
    Unit* u = game::client_unit(k.id, game::kItem);
    if (!u || !maploot::on_ground(game::unit_mode(u))) continue;
    Pillar& p = s.pillar[s.count];
    if (!game::project_unit(u, false, &p.x, &p.y) && !game::project_above_head(u, &p.x, &p.y)) {
      // Off the screen, or the game gives no point for it (said once a game: it may be what an update changed).
      if (!g_said_no_point) {
        g_said_no_point = true;
        logf("light pillars: the game gave no place on screen for a chosen drop (item %u) - no pillar while it does not",
             k.id);
      }
      continue;
    }
    p.kind = k.kind;
    p.id = k.id;
    ++s.count;
    if (g_logged < kLogFirst) {
      ++g_logged;
      logf("light pillars: a pillar on a %s (item %u) at %.0f, %.0f of %d x %d projected pixels%s",
           k.kind == maploot::kItemRune ? "rune" : "rare drop", k.id, p.x, p.y, s.render_w, s.render_h,
           g_logged == kLogFirst ? "  (not logged again in this game)" : "");
    }
  }
  if (s.count == 0 && !g_showing) return;
  s.side = game::panel_side();
  // The nearer ones (lower on the screen) are drawn last, over the ones behind them.
  std::sort(s.pillar, s.pillar + s.count, [](const Pillar& a, const Pillar& b) { return a.y < b.y; });
  publish(s, now);
  g_showing = s.count > 0;
}

bool take_snapshot() {
  if (!g_cs_ready || !g_bound) return false;
  EnterCriticalSection(&g_cs);
  const bool fresh = g_published.at && g_published.count > 0 && GetTickCount64() - g_published.at <= kStaleMs;
  if (fresh) {
    g_view.at = g_published.at;
    g_view.render_w = g_published.render_w;
    g_view.render_h = g_published.render_h;
    g_view.side = g_published.side;
    g_view.count = g_published.count;
    std::memcpy(g_view.pillar, g_published.pillar, sizeof(Pillar) * static_cast<size_t>(g_published.count));
  }
  LeaveCriticalSection(&g_cs);
  if (!fresh) g_view.count = 0;
  return fresh;
}

void draw() {
  const Snapshot& s = g_view;
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (s.count <= 0 || s.render_w <= 0 || s.render_h <= 0 || size.x <= 0.0f || size.y <= 0.0f) return;
  // The projection's pixels and the window's differ with resolution scaling.
  const float sx = size.x / static_cast<float>(s.render_w), sy = size.y / static_cast<float>(s.render_h);
  const float unit = size.y / 1080.0f, half = size.x * 0.5f;
  const float reach = pillarart::kShadeHalf * unit, height = pillarart::kHeight * unit;
  const double seconds = static_cast<double>(GetTickCount64() % 3600000ull) / 1000.0;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  for (int i = 0; i < s.count; ++i) {
    const Pillar& p = s.pillar[i];
    const float x = p.x * sx, y = p.y * sy;
    // Its foot may be a little below the screen's edge with the beam still in sight; anything else off the screen is not drawn.
    if (x < -reach || x > size.x + reach || y < 0.0f || y > size.y + height) continue;
    // An open inventory or character panel covers its half of the screen.
    if (s.side == 3 || (s.side == 1 && x >= half) || (s.side == 2 && x < half)) continue;
    const pillarart::Color c = p.kind == maploot::kItemRune ? pillarart::kRuneColor : pillarart::kOtherColor;
    pillarart::beam(dl, x, y, unit, c, seconds + static_cast<double>(p.id % 97u) * 0.173);
  }
}

void game_left() {
  InterlockedExchange(&g_reset, 1);
  if (!g_cs_ready) return;
  EnterCriticalSection(&g_cs);
  g_published.at = 0;
  g_published.count = 0;
  LeaveCriticalSection(&g_cs);
}

}  // namespace d2rcc::pillars
