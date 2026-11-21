// The unique and set items as the menu offers them (named.h): a local addition.
#include "named.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace d2rcc::named {
namespace {

constexpr size_t kNameLen = 64;

struct Model {
  Entry entry[kMaxEntries];
  int count = 0;
  int16_t by_row[kMaxEntries * 2];  // a table row's entry, -1 none
  int rows = 0;
};

// The names as display() answers them: English until localize() has asked the game.
struct Names {
  char entry[kKinds][kMaxEntries][kNameLen];
  char group[kKinds][64][kNameLen];
};

const Item* items_of(Kind kind, int* count, int* rows) {
  *count = kind == kUnique ? kUniqueCount : kSetItemCount;
  *rows = kind == kUnique ? kUniqueRows : kSetItemRows;
  return kind == kUnique ? kUniques : kSetItems;
}

Model build(Kind kind) {
  Model m;
  int count = 0;
  const Item* items = items_of(kind, &count, &m.rows);
  if (m.rows > static_cast<int>(sizeof(m.by_row) / sizeof(m.by_row[0]))) m.rows = static_cast<int>(sizeof(m.by_row) / sizeof(m.by_row[0]));
  for (int16_t& e : m.by_row) e = -1;
  for (int i = 0; i < count; ++i) {
    int at = -1;
    for (int k = 0; k < m.count && at < 0; ++k)
      if (m.entry[k].group == items[i].group && std::strcmp(m.entry[k].item->english, items[i].english) == 0) at = k;
    if (at < 0) {
      if (m.count == kMaxEntries) break;
      at = m.count++;
      m.entry[at].item = &items[i];
      m.entry[at].group = items[i].group;
    }
    if (items[i].id < m.rows) m.by_row[items[i].id] = static_cast<int16_t>(at);
  }
  return m;
}

const Model& model(Kind kind) {
  static const Model models[kKinds] = {build(kUnique), build(kSetItem)};
  return models[kind == kSetItem ? kSetItem : kUnique];
}

void english_names(Names* n) {
  for (int k = 0; k < kKinds; ++k) {
    const Model& m = model(static_cast<Kind>(k));
    for (int i = 0; i < m.count; ++i) std::snprintf(n->entry[k][i], kNameLen, "%s", m.entry[i].item->english);
    for (int g = 0; g < groups(static_cast<Kind>(k)) && g < 64; ++g)
      std::snprintf(n->group[k][g], kNameLen, "%s", group_english(static_cast<Kind>(k), g));
  }
}

Names* english() {
  static Names* names = [] {
    auto* n = new Names{};
    english_names(n);
    return n;
  }();
  return names;
}
std::atomic<Names*> g_names{nullptr};

const Names* names() {
  const Names* n = g_names.load(std::memory_order_acquire);
  return n ? n : english();
}

// A base code as an Items row has it against the three characters of the list.
bool same_base(const char* base, uint32_t code) {
  for (int i = 0; i < 4; ++i) {
    const char have = static_cast<char>((code >> (8 * i)) & 0xFF);
    const char want = i < 4 ? base[i] : 0;
    const bool have_end = have == ' ' || have == 0, want_end = want == ' ' || want == 0;
    if (have_end && want_end) return true;
    if (have_end != want_end || have != want) return false;
    if (want == 0) return true;
  }
  return true;
}

bool same_text(const char* a, const char* b, size_t len) { return std::strlen(a) == len && _strnicmp(a, b, len) == 0; }

}  // namespace

int entries(Kind kind) { return model(kind).count; }

const Entry& entry(Kind kind, int index) {
  static const Entry none;
  const Model& m = model(kind);
  return index >= 0 && index < m.count ? m.entry[index] : none;
}

Mask all(Kind kind) {
  Mask mask;
  for (int i = 0; i < entries(kind); ++i) mask.set(i, true);
  return mask;
}

int groups(Kind kind) { return kind == kUnique ? kUniqueGroupCount : kSetCount; }

const char* group_english(Kind kind, int group) {
  if (group < 0 || group >= groups(kind)) return "";
  return kind == kUnique ? kUniqueGroups[group] : kSetEnglish[group];
}

const char* group_key(Kind kind, int group) {
  return kind == kSetItem && group >= 0 && group < kSetCount ? kSetKeys[group] : nullptr;
}

int entry_of(Kind kind, int file_index) {
  const Model& m = model(kind);
  return file_index >= 0 && file_index < m.rows ? m.by_row[file_index] : -1;
}

int entry_of(Kind kind, int file_index, uint32_t base_code) {
  const int at = entry_of(kind, file_index);
  if (at < 0) return -1;
  // The row's own item (not the entry's first: the eight Rainbow Facets share a base, but a row is checked as itself).
  int count = 0, rows = 0;
  const Item* items = items_of(kind, &count, &rows);
  for (int i = 0; i < count; ++i)
    if (items[i].id == file_index) return same_base(items[i].base, base_code) ? at : -1;
  return -1;
}

const char* display(Kind kind, int index) {
  if (index < 0 || index >= entries(kind)) return "";
  return names()->entry[kind == kSetItem ? kSetItem : kUnique][index];
}

const char* group_display(Kind kind, int group) {
  if (group < 0 || group >= groups(kind) || group >= 64) return "";
  return names()->group[kind == kSetItem ? kSetItem : kUnique][group];
}

void localize(Lookup lookup) {
  if (!lookup) return;
  auto* n = new Names{};
  english_names(n);
  char text[256];
  for (int k = 0; k < kKinds; ++k) {
    const Model& m = model(static_cast<Kind>(k));
    for (int i = 0; i < m.count; ++i)
      if (lookup(m.entry[i].item->key, text, sizeof(text)) && text[0]) std::snprintf(n->entry[k][i], kNameLen, "%s", text);
    for (int g = 0; g < groups(static_cast<Kind>(k)) && g < 64; ++g)
      if (const char* key = group_key(static_cast<Kind>(k), g))
        if (lookup(key, text, sizeof(text)) && text[0]) std::snprintf(n->group[k][g], kNameLen, "%s", text);
  }
  // The names before are left where they are: a reader may still hold them (they are small, and this runs once a
  // language at the most).
  g_names.store(n, std::memory_order_release);
}

std::string to_text(Kind kind, const Mask& mask) {
  const int n = entries(kind);
  int on = 0;
  for (int i = 0; i < n; ++i) on += mask.get(i) ? 1 : 0;
  if (on == 0) return "none";
  if (on == n) return "all";
  std::string out;
  for (int i = 0; i < n; ++i) {
    if (!mask.get(i)) continue;
    if (!out.empty()) out += "; ";
    out += entry(kind, i).item->english;
  }
  return out;
}

Mask from_text(Kind kind, const std::string& text, int* unknown) {
  Mask mask;
  int strangers = 0, words = 0;
  bool every = false;
  const int n = entries(kind);
  for (size_t i = 0; i < text.size();) {
    while (i < text.size() && (text[i] == ';' || text[i] == ' ' || text[i] == '\t')) ++i;
    size_t end = i;
    while (end < text.size() && text[end] != ';') ++end;
    size_t len = end - i;
    while (len && (text[i + len - 1] == ' ' || text[i + len - 1] == '\t')) --len;
    if (len) {
      ++words;
      const char* word = text.c_str() + i;
      if (same_text("all", word, len)) {
        every = true;
      } else if (!same_text("none", word, len)) {
        int at = -1;
        for (int k = 0; k < n && at < 0; ++k)
          if (same_text(entry(kind, k).item->english, word, len) || same_text(entry(kind, k).item->key, word, len)) at = k;
        if (at >= 0) mask.set(at, true);
        else ++strangers;
      }
    }
    i = end;
  }
  if (every && words == 1) mask = all(kind);
  if (unknown) *unknown = strangers;
  return mask;
}

}  // namespace d2rcc::named
