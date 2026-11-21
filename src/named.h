#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "named_items.h"

// The unique and set items as the menu offers them (a local addition): the lists of named_items.h made into menu
// entries, a choice of entries as a mask, and that choice as the settings file writes it.
//
// An entry is one name. The game has eight unique jewels named Rainbow Facet, one for each element and trigger; they
// are one entry, ticked together. An item on the ground is told by its file index (its row in the game's UniqueItems
// or SetItems table), and that it is the item this build lists there is checked by its base item's code: with a mod
// that puts other items into those rows the item is simply not one of the entries.
namespace d2rcc::named {

enum Kind : int { kUnique = 0, kSetItem, kKinds };
constexpr int kMaxEntries = 512;  // a kind's entries at the most: what a mask holds
constexpr int kWords = kMaxEntries / 64;

struct Mask {
  uint64_t w[kWords] = {};
  bool get(int i) const { return i >= 0 && i < kMaxEntries && ((w[i >> 6] >> (i & 63)) & 1) != 0; }
  void set(int i, bool on) {
    if (i < 0 || i >= kMaxEntries) return;
    if (on) w[i >> 6] |= 1ull << (i & 63);
    else w[i >> 6] &= ~(1ull << (i & 63));
  }
  bool any() const {
    for (const uint64_t word : w)
      if (word) return true;
    return false;
  }
  int count() const {
    int n = 0;
    for (uint64_t word : w)
      for (; word; word &= word - 1) ++n;
    return n;
  }
  bool operator==(const Mask& o) const {
    for (int i = 0; i < kWords; ++i)
      if (w[i] != o.w[i]) return false;
    return true;
  }
  bool operator!=(const Mask& o) const { return !(*this == o); }
};

struct Entry {
  const Item* item = nullptr;  // the first item of that name
  int group = 0;               // a unique item's kind (kUniqueGroups), a set item's set
};

int entries(Kind kind);
const Entry& entry(Kind kind, int index);
Mask all(Kind kind);
int groups(Kind kind);
// A group's name in English, and for a set the key of its name in the game's strings (null for a unique item's kind).
const char* group_english(Kind kind, int group);
const char* group_key(Kind kind, int group);

// The entry of an item on the ground: by its file index, when the row is one this build lists and `base_code` is
// that item's base (four characters as an Items row has them, the first in the low byte, spaces or zeros after a
// shorter code). -1 otherwise.
int entry_of(Kind kind, int file_index, uint32_t base_code);
// ... without the check of the base (tests, and where no Items row is at hand).
int entry_of(Kind kind, int file_index);

// A name as the menu and the messages show it: the game's own, in the game's language, once localize() has run;
// until then, and where the game has none, the English name.
const char* display(Kind kind, int index);
const char* group_display(Kind kind, int group);
// Asks `lookup` (the game's string of a key into out; false when it has none) for every name. Any thread, once the
// game's strings are loaded; what display() answers changes in one step when it is done.
using Lookup = bool (*)(const char* key, char* out, size_t cap);
void localize(Lookup lookup);

// A choice as the settings file has it: "none", "all", or the English names apart by semicolons, in any case:
// "Harlequin Crest; Griffon's Eye". A name the list does not have is left out and counted in *unknown.
std::string to_text(Kind kind, const Mask& mask);
Mask from_text(Kind kind, const std::string& text, int* unknown = nullptr);

}  // namespace d2rcc::named
