#pragma once

#include <cstdint>
#include <cstring>
#include <string>

// What the plugin tracks (a local addition, not CabbyCodes' own): which of the 33 runes, and which groups of the other
// rare drops (the Pandemonium keys and organs, the essences, the Worldstone Shards, the Ancients' statues and
// fragments). Chosen in the plugin's menu, kept in the settings file, and asked by the map's marks and the drop
// messages (maploot.cpp). An item that is not tracked gets no icon and no message.
//
// The rules (names, codes, the settings text) are here for tests/test_track.cpp; the choices themselves are in
// track.cpp.
namespace d2rcc::track {

// --- the runes: r01 .. r33 ------------------------------------------------------------------------------------------
constexpr int kRunes = 33;
constexpr uint64_t kAllRunes = (1ull << kRunes) - 1;  // bit n - 1: rune n
constexpr const char* kRuneNames[kRunes] = {"El",  "Eld", "Tir",   "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn",
                                            "Sol", "Shael", "Dol", "Hel", "Io",  "Lum", "Ko",  "Fal", "Lem", "Pul",  "Um",
                                            "Mal", "Ist", "Gul",   "Vex", "Ohm", "Lo",  "Sur", "Ber", "Jah", "Cham", "Zod"};

// --- the other items, in groups -------------------------------------------------------------------------------------
enum Group : int { kKeys = 0, kOrgans, kEssences, kShards, kStatues, kFragments, kGroups };
constexpr uint32_t kAllGroups = (1u << kGroups) - 1;
// Tracked unless the settings say otherwise: all but the fragments (the user asked for the others by name).
constexpr uint32_t kDefaultGroups = kAllGroups & ~(1u << kFragments);
constexpr const char* kGroupNames[kGroups] = {"Keys", "Organs", "Essences", "Worldstone Shards", "Statues", "Fragments"};
// The settings file's key of each group.
constexpr const char* kGroupKeys[kGroups] = {"keys", "organs", "essences", "worldstone_shards", "statues", "fragments"};
// What is in each, for the menu.
constexpr const char* kGroupItems[kGroups] = {
    "Key of Terror, Key of Hate, Key of Destruction",
    "Diablo's Horn, Baal's Eye, Mephisto's Brain",
    "Twisted, Charged, Burning and Festering Essence",
    "Western, Eastern, Southern, Deep and Northern Worldstone Shard",
    "Talic's Anguish, Korlic's Pain, Madawc's Ire, Bul-Kathos' Nightmare, Worusk's End",
    "Defender's Fire and Bile, Protector's Frost and Stone, Guardian's Thunder and Light",
};

// An item of a group: its code in the Items rows (misc.txt of game build 3.4), and its English name for where the
// game's strings have none.
struct Special {
  char code[4];
  Group group;
  const char* english;
};
constexpr Special kSpecials[] = {
    {"pk1", kKeys, "Key of Terror"},
    {"pk2", kKeys, "Key of Hate"},
    {"pk3", kKeys, "Key of Destruction"},
    {"dhn", kOrgans, "Diablo's Horn"},
    {"bey", kOrgans, "Baal's Eye"},
    {"mbr", kOrgans, "Mephisto's Brain"},
    {"tes", kEssences, "Twisted Essence of Suffering"},
    {"ceh", kEssences, "Charged Essence of Hatred"},
    {"bet", kEssences, "Burning Essence of Terror"},
    {"fed", kEssences, "Festering Essence of Destruction"},
    {"xa1", kShards, "Western Worldstone Shard"},
    {"xa2", kShards, "Eastern Worldstone Shard"},
    {"xa3", kShards, "Southern Worldstone Shard"},
    {"xa4", kShards, "Deep Worldstone Shard"},
    {"xa5", kShards, "Northern Worldstone Shard"},
    {"ua1", kStatues, "Talic's Anguish"},
    {"ua2", kStatues, "Korlic's Pain"},
    {"ua3", kStatues, "Madawc's Ire"},
    {"ua4", kStatues, "Bul-Kathos' Nightmare"},
    {"ua5", kStatues, "Worusk's End"},
    {"um1", kFragments, "Defender's Fire"},
    {"um2", kFragments, "Defender's Bile"},
    {"um3", kFragments, "Protector's Frost"},
    {"um4", kFragments, "Protector's Stone"},
    {"um5", kFragments, "Guardian's Thunder"},
    {"um6", kFragments, "Guardian's Light"},
};
constexpr int kSpecialCount = static_cast<int>(sizeof(kSpecials) / sizeof(kSpecials[0]));

// --- an Items row's code --------------------------------------------------------------------------------------------
// Four characters as the row has them, the first in the low byte, spaces (or zeros) after a shorter code: "r07 ".
constexpr char code_char(uint32_t code, int i) { return static_cast<char>((code >> (8 * i)) & 0xFF); }
constexpr bool code_ends(uint32_t code, int from) {
  for (int i = from; i < 4; ++i)
    if (code_char(code, i) != ' ' && code_char(code, i) != 0) return false;
  return true;
}
// 1 .. 33 for r01 .. r33, else 0.
constexpr int rune_number(uint32_t code) {
  const char d1 = code_char(code, 1), d2 = code_char(code, 2);
  if (code_char(code, 0) != 'r' || d1 < '0' || d1 > '9' || d2 < '0' || d2 > '9' || !code_ends(code, 3)) return 0;
  const int n = (d1 - '0') * 10 + (d2 - '0');
  return n >= 1 && n <= kRunes ? n : 0;
}
// Its place in kSpecials, else -1.
constexpr int special_index(uint32_t code) {
  if (!code_ends(code, 3)) return -1;
  for (int i = 0; i < kSpecialCount; ++i)
    if (code_char(code, 0) == kSpecials[i].code[0] && code_char(code, 1) == kSpecials[i].code[1] &&
        code_char(code, 2) == kSpecials[i].code[2])
      return i;
  return -1;
}
constexpr uint32_t make_code(const char* three) {
  return static_cast<uint32_t>(static_cast<uint8_t>(three[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(three[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(three[2])) << 16) | (static_cast<uint32_t>(' ') << 24);
}

// --- the settings file's text for the runes -------------------------------------------------------------------------
// "all", "none", or the tracked runes' names apart by spaces or commas, in any case and order: "Pul Um Mal Ist".
inline int rune_by_name(const char* name, size_t len) {
  for (int i = 0; i < kRunes; ++i)
    if (std::strlen(kRuneNames[i]) == len && _strnicmp(kRuneNames[i], name, len) == 0) return i + 1;
  return 0;
}
// False where a word is no rune's name (the mask is then not to be used).
inline bool runes_from_text(const std::string& text, uint64_t* mask) {
  uint64_t m = 0;
  size_t words = 0;
  bool all = false, none = false;
  for (size_t i = 0; i < text.size();) {
    while (i < text.size() && (text[i] == ' ' || text[i] == ',' || text[i] == '\t' || text[i] == ';')) ++i;
    const size_t start = i;
    while (i < text.size() && text[i] != ' ' && text[i] != ',' && text[i] != '\t' && text[i] != ';') ++i;
    if (i == start) break;
    ++words;
    const char* w = text.c_str() + start;
    const size_t len = i - start;
    if (len == 3 && _strnicmp(w, "all", 3) == 0) {
      all = true;
    } else if (len == 4 && _strnicmp(w, "none", 4) == 0) {
      none = true;
    } else if (const int n = rune_by_name(w, len)) {
      m |= 1ull << (n - 1);
    } else {
      return false;
    }
  }
  if (all && (none || words != 1)) return false;
  if (none && words != 1) return false;
  *mask = all ? kAllRunes : m;  // no word at all: none
  return true;
}
inline std::string runes_to_text(uint64_t mask) {
  mask &= kAllRunes;
  if (mask == kAllRunes) return "all";
  if (mask == 0) return "none";
  std::string out;
  for (int i = 0; i < kRunes; ++i) {
    if (!((mask >> i) & 1)) continue;
    if (!out.empty()) out += ' ';
    out += kRuneNames[i];
  }
  return out;
}
// Rune n and every higher one.
constexpr uint64_t runes_from(int n) { return n < 1 ? kAllRunes : n > kRunes ? 0 : kAllRunes & ~((1ull << (n - 1)) - 1); }

// --- the choices (track.cpp; any thread) ----------------------------------------------------------------------------
void init(uint64_t runes, uint32_t groups, bool icons = true);  // from the settings, as the plugin loads
// How a tracked drop is shown has three switches, any of them on or off: its icon on the minimap (here), and its line
// in the chat and its pop-up on screen (chatline.h's mode, a bit each).
bool icons();
void set_icons(bool on);
uint64_t runes();
uint32_t groups();
bool rune(int number);  // 1 .. 33; any other number (a mod's own rune): tracked
void set_rune(int number, bool on);
void set_runes(uint64_t mask);
bool group(int g);
void set_group(int g, bool on);

}  // namespace d2rcc::track
