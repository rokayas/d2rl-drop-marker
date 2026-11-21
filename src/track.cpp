// What the plugin tracks (track.h): the choices, read on the game's threads and set from the menu.
#include "track.h"

#include <windows.h>

namespace d2rcc::track {
namespace {

volatile LONG64 g_runes = static_cast<LONG64>(kAllRunes);
volatile LONG g_groups = static_cast<LONG>(kDefaultGroups);
volatile LONG g_icons = 1;
volatile LONG64 g_pillar_runes = static_cast<LONG64>(kDefaultPillarRunes);
volatile LONG g_pillar_groups = static_cast<LONG>(kDefaultPillarGroups);

}  // namespace

void init(uint64_t runes, uint32_t groups, bool icons) {
  InterlockedExchange64(&g_runes, static_cast<LONG64>(runes & kAllRunes));
  InterlockedExchange(&g_groups, static_cast<LONG>(groups & kAllGroups));
  InterlockedExchange(&g_icons, icons ? 1 : 0);
}

bool icons() { return g_icons != 0; }
void set_icons(bool on) { InterlockedExchange(&g_icons, on ? 1 : 0); }

uint64_t runes() { return static_cast<uint64_t>(g_runes) & kAllRunes; }
uint32_t groups() { return static_cast<uint32_t>(g_groups) & kAllGroups; }

bool rune(int number) {
  if (number < 1 || number > kRunes) return true;
  return ((static_cast<uint64_t>(g_runes) >> (number - 1)) & 1) != 0;
}

void set_rune(int number, bool on) {
  if (number < 1 || number > kRunes) return;
  const LONG64 bit = static_cast<LONG64>(1ull << (number - 1));
  if (on) InterlockedOr64(&g_runes, bit);
  else InterlockedAnd64(&g_runes, ~bit);
}

void set_runes(uint64_t mask) { InterlockedExchange64(&g_runes, static_cast<LONG64>(mask & kAllRunes)); }

bool group(int g) { return g >= 0 && g < kGroups && ((static_cast<uint32_t>(g_groups) >> g) & 1) != 0; }

void set_group(int g, bool on) {
  if (g < 0 || g >= kGroups) return;
  const LONG bit = static_cast<LONG>(1u << g);
  if (on) InterlockedOr(&g_groups, bit);
  else InterlockedAnd(&g_groups, ~bit);
}

void init_pillars(uint64_t runes, uint32_t groups) {
  InterlockedExchange64(&g_pillar_runes, static_cast<LONG64>(runes & kAllRunes));
  InterlockedExchange(&g_pillar_groups, static_cast<LONG>(groups & kAllGroups));
}

uint64_t pillar_runes() { return static_cast<uint64_t>(g_pillar_runes) & kAllRunes; }
uint32_t pillar_groups() { return static_cast<uint32_t>(g_pillar_groups) & kAllGroups; }

bool pillar_rune(int number) {
  return number >= 1 && number <= kRunes && ((static_cast<uint64_t>(g_pillar_runes) >> (number - 1)) & 1) != 0;
}

void set_pillar_runes(uint64_t mask) { InterlockedExchange64(&g_pillar_runes, static_cast<LONG64>(mask & kAllRunes)); }

bool pillar_group(int g) { return g >= 0 && g < kGroups && ((static_cast<uint32_t>(g_pillar_groups) >> g) & 1) != 0; }

void set_pillar_group(int g, bool on) {
  if (g < 0 || g >= kGroups) return;
  const LONG bit = static_cast<LONG>(1u << g);
  if (on) InterlockedOr(&g_pillar_groups, bit);
  else InterlockedAnd(&g_pillar_groups, ~bit);
}

// --- the unique and set items ---------------------------------------------------------------------------------------
namespace {
// [kind][0 tracked, 1 with a pillar]: a word at a time, each read and written whole.
volatile LONG64 g_named[named::kKinds][2][named::kWords] = {};

named::Mask named_get(int kind, int which) {
  named::Mask m;
  if (kind < 0 || kind >= named::kKinds) return m;
  for (int i = 0; i < named::kWords; ++i) m.w[i] = static_cast<uint64_t>(g_named[kind][which][i]);
  return m;
}
void named_put(int kind, int which, const named::Mask& m) {
  if (kind < 0 || kind >= named::kKinds) return;
  for (int i = 0; i < named::kWords; ++i) InterlockedExchange64(&g_named[kind][which][i], static_cast<LONG64>(m.w[i]));
}
bool named_bit(int kind, int which, int entry) {
  if (kind < 0 || kind >= named::kKinds || entry < 0 || entry >= named::kMaxEntries) return false;
  return ((static_cast<uint64_t>(g_named[kind][which][entry >> 6]) >> (entry & 63)) & 1) != 0;
}
bool named_any(int which) {
  for (int kind = 0; kind < named::kKinds; ++kind)
    for (int i = 0; i < named::kWords; ++i)
      if (g_named[kind][which][i]) return true;
  return false;
}
}  // namespace

void init_named(int kind, const named::Mask& tracked, const named::Mask& pillars) {
  named_put(kind, 0, tracked);
  named_put(kind, 1, pillars);
}
named::Mask named_tracked(int kind) { return named_get(kind, 0); }
named::Mask named_pillars(int kind) { return named_get(kind, 1); }
void set_named_tracked(int kind, const named::Mask& mask) { named_put(kind, 0, mask); }
void set_named_pillars(int kind, const named::Mask& mask) { named_put(kind, 1, mask); }
bool named(int kind, int entry) { return named_bit(kind, 0, entry); }
bool named_pillar(int kind, int entry) { return named_bit(kind, 1, entry); }
bool any_named() { return named_any(0) || named_any(1); }

bool any_pillars() { return pillar_runes() != 0 || pillar_groups() != 0 || named_any(1); }

}  // namespace d2rcc::track
