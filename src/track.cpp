// What the plugin tracks (track.h): the choices, read on the game's threads and set from the menu.
#include "track.h"

#include <windows.h>

namespace d2rcc::track {
namespace {

volatile LONG64 g_runes = static_cast<LONG64>(kAllRunes);
volatile LONG g_groups = static_cast<LONG>(kDefaultGroups);
volatile LONG g_icons = 1;

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

}  // namespace d2rcc::track
