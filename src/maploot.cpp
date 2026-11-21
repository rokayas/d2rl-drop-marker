#include "maploot.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mapicons.h"
#include "mapmarks.h"
#include "mem.h"
#include "perf.h"
#include "sites.h"
#include "chatline.h"
#include "toast.h"
#include "track.h"

namespace d2rcc::maploot {
namespace {

constexpr int kBanks = 4;  // by a unit's data-table bank, 1 classic .. 3 rotw
constexpr int kMaxObjects = 1024;
constexpr int kMaxLevels = 1024;
constexpr uint32_t kMaxItems = 4096;
constexpr uint32_t kItemsRow = 0x1C0;   // the compiled Items row (weapons, armor and misc in one table)
constexpr uintptr_t kItemsType = 0x12E;  // int16: its ItemTypes row
// A local addition (the rune drop message): the row's code, and the runes' names kept for the message.
constexpr uintptr_t kItemsCode = 0x80;   // four characters, "r01 " and so on (spaces after a shorter code)
constexpr int kMaxRuneNames = 128;
constexpr size_t kRuneNameLen = 48;
constexpr size_t kMaxChestMarks = 8192;  // chests and openables known in a game
constexpr size_t kMaxItemMarks = 2048;   // items on the ground marked at once
constexpr int kWhiteCross = 6;           // the automap's unit crosses: frame 6 is white (an NPC's), tinted here
constexpr int kLogFirst = 4;
constexpr uint64_t kIconRetryMs = 5000;  // a load the game refused is not asked again sooner

// What the two read of a bank's tables: written once by the game thread, then only read (any thread).
struct Tables {
  volatile LONG ready;
  int objects;
  uint8_t kind[kMaxObjects];   // ObjectKind
  int16_t cell[kMaxObjects];   // the class's own automap icon (0 none)
  int sparkle_cell;            // the quest chests' icon: the sparkle chest
  int coin_cell;               // the stash's icon: the coin chest
  int counts[5];               // classes of each kind
  uint8_t restores[kMaxObjects];  // the class's Restore: the game keeps such an object as it lets its room go
  int fleeting_classes;           // classes that open for loot and are not kept so, ...
  char fleeting_first[kObjectNameLen + 1];  // ... the first of them
  int levels;                     // the LevelDefs rows read (0: none, and every level is taken to keep its objects)
  uint8_t keeps_units[kMaxLevels];  // the level's record keeps its units as the game lets one of its rooms go
  int levels_keeping_none;
  uint32_t items;
  uint8_t rune[kMaxItems];     // the Items row is a rune
  int runes;
  uint8_t rune_name_at[kMaxItems];               // a rune's name in rune_names, plus one (0: none kept)
  char rune_names[kMaxRuneNames][kRuneNameLen];  // as the game shows it ("Ber Rune")
  // What is tracked (track.h, a local addition): a rune's number, 1 .. 33 (0: not one of r01 .. r33), and for one of the
  // other rare drops its place in track::kSpecials, plus one (0: none). Their names are kept with the runes' above.
  uint8_t rune_no[kMaxItems];
  uint8_t special[kMaxItems];
  int specials;
  // The row's code (a local addition): a unique or set item is told from its file index, and that the row there is
  // the item this build lists is checked by its base item's code (named.h).
  uint32_t code[kMaxItems];
};

// A chest (or anything that opens for loot) known in this game, by where it stands (its level and its pixels: an object
// does not move, and the server gives it another id when it makes it again, its room let go and built again: mapmarks.h
// RoomBox): what it is, and whether it was seen opened.
struct ChestMark {
  uint8_t bank = 0;
  uint8_t kind = kNotOpenable;
  bool opened = false;
  int level = -1;
  int32_t px = 0, py = 0;
  uint16_t cls = 0;
};
// An item on the ground, by its id.
struct ItemMark {
  uint8_t bank = 0;
  uint8_t kind = kItemNone;
  int level = -1;
  int32_t px = 0, py = 0;
  uint32_t cls = UINT32_MAX;  // its Items row, when known (the rune drop message names the rune by it)
  int32_t named = -1;         // a unique or set item's file index: its row in the game's table of them
};
// An area's objects or items as the server read them, waiting for the automap's next draw.
struct Pending {
  bool items = false;  // else objects
  uint8_t bank = 0;
  int level = -1;
  std::vector<ServerObject> objects;
  std::vector<uint64_t> fleeting;  // where the read found objects the game throws away as it lets their room go
  std::vector<ServerItem> ground;
  std::vector<mapmarks::RoomBox> rooms;  // the rooms read, built and filled (a mark elsewhere is not the read's to take away)
};

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const D2RL::ResourceService* g_resources = nullptr;
Tables g_banks[kBanks];
volatile LONG g_chests_ok = 0, g_items_ok = 0, g_dropped = 1, g_opened = 0, g_reset = 0;
volatile LONG g_row_warned = 0, g_full_warned = 0, g_chest_logged = 0, g_item_logged = 0, g_pickup_logged = 0,
              g_opened_logged = 0, g_draw_logged = 0, g_defs_warned = 0, g_gone_logged = 0, g_fleeting_logged = 0;
volatile LONG g_stat_items_seen = 0, g_stat_picked = 0, g_stat_reads = 0, g_stat_chests = 0, g_stat_open = 0,
              g_stat_items = 0, g_stat_gone = 0, g_stat_fleeting = 0, g_stat_fleeting_near = 0;
CRITICAL_SECTION g_cs;  // the pending reads
bool g_cs_ready = false;
std::vector<Pending> g_pending;  // under g_cs
std::vector<uint32_t> g_picked;  // under g_cs: the items the pick-up routine has taken off the ground since
volatile LONG g_pending_waiting = 0;
volatile LONG g_named_localized = 0;  // the unique and set items' names were asked of the game's strings
volatile LONG g_named_logged = 0;

// The icon files registered, and the sprite objects the game loaded them into (the UI thread loads, the automap's draw
// reads: the same thread in the game).
bool g_registered[mapicons::kIcons] = {};
bool g_registered_file[mapicons::kIcons][2] = {};  // the HD file and the low-end one
bool g_registered_tried = false;  // every file was registered, or refused for good
alignas(16) uint8_t g_icon[mapicons::kIcons][16] = {};
volatile LONG g_icons_loaded = 0;
const char* volatile g_icons_why = nullptr;  // why the icons are not loaded (null: not tried yet, or loaded)
uint64_t g_icons_tried = 0;
int g_icon_tries = 0;
// They are held only for a game: let go as it ends (a sprite still held when the game shuts its UI down is an assertion
// box in d2rloader.log, "s_loadedSpriteAssetPool.IsEmpty()"), loaded again in the next one. On the UI thread, where they
// are loaded: at once when the game's end is announced there, else at the next UI callback.
DWORD g_ui_thread = 0;
volatile LONG g_release = 0, g_release_logged = 0;

// A local addition: the runes whose drop was said in this game, by place_key and by unit id. Any thread, under
// g_said_lock: the server's read of the ground says a rune (the game's thread), and so does the client's sight of one
// (the automap's draw thread), whichever comes first.
SRWLOCK g_said_lock = SRWLOCK_INIT;
std::unordered_set<uint64_t> g_said;
std::unordered_set<uint32_t> g_said_ids;

// The automap's draw thread only.
std::unordered_map<uint64_t, ChestMark> g_chests;  // by place_key
std::unordered_map<uint32_t, ItemMark> g_items;
std::unordered_map<uint32_t, bool> g_items_counted;  // the items marked in this game (each counted once)
// The objects that open for loot and that the game throws away as it lets their room go, by the client's id: drawn only
// while the automap's pass hands them over (maploot.h kept_when_let_go, fleeting_shown).
struct Fleeting {
  ChestMark mark;
  uint32_t seen_at = 0;  // the draw the pass last handed it over in
};
std::unordered_map<uint32_t, Fleeting> g_fleeting;
std::unordered_set<uint64_t> g_fleeting_places;  // where the server's reads found one, in this game (a count)
uint32_t g_draw = 0;  // the automap's draws: the local player's own calls

const Tables* tables_of(uint8_t bank) {
  return bank >= 1 && bank < kBanks && g_banks[bank].ready ? &g_banks[bank] : nullptr;
}

// Whether the game throws an object away as it lets its room go: its class has no Restore, or its level's record keeps
// no units (a level the rows do not have is taken to keep them).
bool fleeting(const Tables* t, uint32_t cls, int level) {
  const bool restores = cls >= static_cast<uint32_t>(t->objects) || t->restores[cls] != 0;
  const bool keeps = level < 0 || level >= t->levels || t->keeps_units[level] != 0;
  return !kept_when_let_go(restores, keeps);
}

// A chest's place as a key: its level and its pixels (24 bits each, ample for any level's pixels).
uint64_t place_key(int level, int32_t px, int32_t py) {
  return (static_cast<uint64_t>(static_cast<uint16_t>(level)) << 48) |
         (static_cast<uint64_t>(static_cast<uint32_t>(px) & 0xFFFFFFu) << 24) |
         (static_cast<uint64_t>(static_cast<uint32_t>(py) & 0xFFFFFFu));
}

// What a server object is to the picker: its class, made a super chest by its sparkle byte while that is trusted.
volatile LONG g_sparkle_bad = 0;
ObjectKind kind_of(ObjectKind class_kind, int sparkle) {
  return g_sparkle_bad ? class_kind : with_sparkle(class_kind, sparkle);
}

// --- the rune drop message (a local addition) -----------------------------------------------------------------------

// The game's colour codes ("ÿc" and a character) taken out of a name, and the name cut at its first line.
void plain_text(char* s) {
  char* w = s;
  for (const char* r = s; *r;) {
    if (static_cast<unsigned char>(r[0]) == 0xC3 && static_cast<unsigned char>(r[1]) == 0xBF && r[2] == 'c' && r[3]) {
      r += 4;
      continue;
    }
    if (*r == '\n' || *r == '\r') break;
    *w++ = *r++;
  }
  *w = 0;
}

// A tracked item's name from its item code: the game's own string of that key ("r30" is "Ber Rune", "pk1" is "Key of
// Terror", in the game's language), else the English name of a stock rune r01 .. r33 or of one of track.h's other
// items (`special`: its place in track::kSpecials, or -1), else nothing (the message then says "A rune").
void item_name(uint32_t code, int special, char* out, size_t cap) {
  out[0] = 0;
  char key[5] = {};
  for (int i = 0; i < 4; ++i) {
    const char c = static_cast<char>((code >> (8 * i)) & 0xFF);
    if (c == ' ' || c == 0) break;
    key[i] = c;
  }
  if (!key[0]) return;
  if (mapmarks::game_string(key, out, cap)) {
    plain_text(out);
    if (out[0]) return;
  }
  if (special >= 0 && special < track::kSpecialCount) std::snprintf(out, cap, "%s", track::kSpecials[special].english);
  else if (const int n = track::rune_number(code)) std::snprintf(out, cap, "%s Rune", track::kRuneNames[n - 1]);
}

// --- the tables (game thread) ---------------------------------------------------------------------------------------

bool read_bank(int b, Tables* t) {
  using namespace D2RL::DataTables;
  const auto bank = static_cast<Bank>(b);
  TableView objects{}, items{}, types{};
  objects.structSize = items.structSize = types.structSize = TableViewSize;
  if (!g_tables || !g_tables->getTable || g_tables->getTable(g_ctx, bank, TableId::Objects, &objects) != Result::Success ||
      !objects.rows || !objects.rowCount ||
      g_tables->getTable(g_ctx, bank, TableId::Items, &items) != Result::Success || !items.rows || !items.rowCount ||
      g_tables->getTable(g_ctx, bank, TableId::ItemTypes, &types) != Result::Success || !types.rows || !types.rowCount)
    return false;
  if (objects.rowSize != kObjectsRow || objects.rowCount > static_cast<uint32_t>(kMaxObjects) ||
      items.rowSize != kItemsRow || items.rowCount > kMaxItems) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("map: bank %d: Objects rows of %u bytes (%u), Items rows of %u bytes (%u), not %u and %u - no chest "
               "or item is put on the map",
               b, objects.rowSize, objects.rowCount, items.rowSize, items.rowCount, kObjectsRow, kItemsRow);
    return false;
  }
  // The objects: what each class is to the chests' switch, and the icons the automap has for chests.
  const int cell_at = sites::map_facts().object_cell_at;
  const int stash = sites::map_facts().stash_class;
  std::vector<uint8_t> rows(static_cast<size_t>(objects.rowCount) * kObjectsRow);
  if (!mem::copy_from(rows.data(), reinterpret_cast<uintptr_t>(objects.rows), rows.size())) return false;
  t->objects = static_cast<int>(objects.rowCount);
  t->sparkle_cell = 0;
  t->coin_cell = 0;
  std::memset(t->counts, 0, sizeof(t->counts));
  t->fleeting_classes = 0;
  std::memset(t->fleeting_first, 0, sizeof(t->fleeting_first));
  for (int i = 0; i < t->objects; ++i) {
    const uint8_t* r = rows.data() + static_cast<size_t>(i) * kObjectsRow;
    char name[kObjectNameLen + 1] = {};
    std::memcpy(name, r + kObjectName, kObjectNameLen);
    int32_t cell = 0;
    if (cell_at > 0 && static_cast<size_t>(cell_at) + 4 <= kObjectsRow) std::memcpy(&cell, r + cell_at, sizeof(cell));
    if (cell < 0 || cell > 0x7FFF) cell = 0;
    t->cell[i] = static_cast<int16_t>(cell);
    const ObjectKind k = object_kind(r[kObjectOperate], r[kObjectInit], r[kObjectSelectable] != 0, cell, name);
    t->kind[i] = k;
    ++t->counts[k];
    if (k == kQuestChest) t->sparkle_cell = !t->sparkle_cell || t->sparkle_cell == cell ? cell : -1;
    if (i == stash) t->coin_cell = cell;
    t->restores[i] = r[kObjectRestore] != 0 ? 1 : 0;
    if (k != kNotOpenable && !t->restores[i] && t->fleeting_classes++ == 0)
      std::memcpy(t->fleeting_first, name, sizeof(t->fleeting_first) - 1);
  }
  // The levels: whether the game keeps a level's units as it lets one of its rooms go. Without the rows every level is
  // taken to (the objects read are marked as they were before this was known).
  TableView defs{};
  defs.structSize = TableViewSize;
  t->levels = 0;
  t->levels_keeping_none = 0;
  if (g_tables->getTable(g_ctx, bank, TableId::LevelDefs, &defs) == Result::Success && defs.rows &&
      defs.rowSize == kLevelDefsRow && defs.rowCount && defs.rowCount <= static_cast<uint32_t>(kMaxLevels)) {
    for (uint32_t i = 0; i < defs.rowCount; ++i) {
      int32_t keeps = 1;
      mem::read_safe(reinterpret_cast<uintptr_t>(defs.rows) + static_cast<uintptr_t>(i) * kLevelDefsRow +
                         kLevelDefsKeepsUnits,
                     &keeps);
      t->keeps_units[i] = keeps != 0 ? 1 : 0;
      t->levels_keeping_none += keeps != 0 ? 0 : 1;
    }
    t->levels = static_cast<int>(defs.rowCount);
  } else if (log_once(&g_defs_warned, LogLevel::kWarning)) {
    log_warn("map: bank %d: no LevelDefs rows of %u bytes (%u of %u) - every level is taken to keep its objects as the "
             "game lets its rooms go",
             b, kLevelDefsRow, defs.rowCount, defs.rowSize);
  }
  // The runes: the Items rows of the rune item type.
  using namespace D2RL::DataTables;
  RowView rune_row{};
  rune_row.structSize = RowViewSize;
  int32_t rune_type = -1;
  if (g_tables->findRowByCode &&
      g_tables->findRowByCode(g_ctx, bank, TableId::ItemTypes, MakeFourCC('r', 'u', 'n', 'e'), &rune_row) ==
          Result::Success)
    rune_type = static_cast<int32_t>(rune_row.rowIndex);
  t->items = items.rowCount;
  t->runes = 0;
  t->specials = 0;
  std::memset(t->rune_name_at, 0, sizeof(t->rune_name_at));
  std::memset(t->rune_no, 0, sizeof(t->rune_no));
  std::memset(t->special, 0, sizeof(t->special));
  std::memset(t->code, 0, sizeof(t->code));
  int names = 0;
  for (uint32_t i = 0; i < items.rowCount; ++i) {
    const uintptr_t row = reinterpret_cast<uintptr_t>(items.rows) + static_cast<uintptr_t>(i) * kItemsRow;
    int16_t type = -1;
    mem::read_safe(row + kItemsType, &type);
    t->rune[i] = rune_type > 0 && type == rune_type ? 1 : 0;
    t->runes += t->rune[i];
    // Local additions: which rune it is, whether it is one of the other tracked drops (by its code), and its name for
    // the drop message.
    uint32_t code = 0;
    if (!mem::read_safe(row + kItemsCode, &code)) continue;
    t->code[i] = code;
    const int special = t->rune[i] ? -1 : track::special_index(code);
    if (t->rune[i]) t->rune_no[i] = static_cast<uint8_t>(track::rune_number(code));
    if (special >= 0) {
      t->special[i] = static_cast<uint8_t>(special + 1);
      ++t->specials;
    }
    if ((t->rune[i] || special >= 0) && names < kMaxRuneNames) {
      item_name(code, special, t->rune_names[names], kRuneNameLen);
      if (t->rune_names[names][0]) t->rune_name_at[i] = static_cast<uint8_t>(++names);
    }
  }
  // The unique and set items' names in the game's language, once a session (the strings are loaded by now).
  if (!InterlockedExchange(&g_named_localized, 1))
    named::localize([](const char* key, char* out, size_t cap) {
      if (!mapmarks::game_string(key, out, cap)) return false;
      plain_text(out);
      return out[0] != 0;
    });
  return true;
}

// --- the icons (the plugin's load, then the UI thread) ---------------------------------------------------------------

void icon_path(int icon, bool low_end, char* out, size_t cap) {
  std::snprintf(out, cap, "%s%s%s%s", mapicons::kLoaderRoot, mapicons::kSpriteFolder, mapicons::kIconNames[icon],
                low_end ? ".lowend.sprite" : ".sprite");
}

// The game's sprite loader, once a session (again after a while when the game had none of them): the plugin's three
// icons into objects of the plugin's, by the names their files were registered under. Only in HD graphics, which has
// sprites of this kind; the legacy graphics' items get the automap's cross.
void load_icons(uint64_t now) {
  if (g_icons_loaded || !game::has_map_sprites() || !game::hd_graphics()) return;
  if (g_icon_tries && now - g_icons_tried < kIconRetryMs) return;
  if (g_icon_tries >= 3) return;
  g_icons_tried = now;
  ++g_icon_tries;
  int loaded = 0;
  char names[160] = {};
  size_t at = 0;
  for (int i = 0; i < mapicons::kIcons; ++i) {
    if (!g_registered[i]) continue;
    char name[128];
    std::snprintf(name, sizeof(name), "%s%s", mapicons::kSpriteFolder, mapicons::kIconNames[i]);
    int32_t w = 0, h = 0;
    const bool ok = game::load_sprite(g_icon[i], name) && game::sprite_frames(g_icon[i]) == 1 &&
                    game::sprite_size(g_icon[i], &w, &h) && w == mapicons::kSize && h == mapicons::kSize;
    loaded += ok ? 1 : 0;
    if (at + 2 < sizeof(names))
      at += static_cast<size_t>(std::snprintf(names + at, sizeof(names) - at, "%s%s %s", at ? ", " : "",
                                              mapicons::kIconNames[i], ok ? "loaded" : "NOT loaded"));
  }
  if (loaded == mapicons::kIcons) {
    InterlockedExchange(&g_icons_loaded, 1);
    g_icons_why = nullptr;
    logf("map: the icons for unique, set and rune items and the enemies' dot loaded by the game's sprite loader (%s)",
         names);
  } else {
    g_icons_why = "the game's sprite loader did not load the plugin's icons";
    log_warn("map: the game's sprite loader did not load the plugin's icons (%s; try %d of 3) - unique, set and rune "
             "items get the automap's cross in their color",
             names, g_icon_tries);
  }
}

// The game's sprite loader handed an empty name for each: it lets go of what the object holds and loads nothing.
void release_icons() {
  InterlockedExchange(&g_icons_loaded, 0);  // the draw takes the crosses from here on
  int released = 0;
  for (int i = 0; i < mapicons::kIcons; ++i) released += game::release_sprite(g_icon[i]) ? 1 : 0;
  g_icon_tries = 0;
  g_icons_tried = 0;
  g_icons_why = nullptr;
  if (released && log_first(&g_release_logged, 1))
    logf("map: the map's icons let go as the game ended or its window closed (%d sprites; loaded again in the next "
         "game)",
         released);
}

// --- the marks (the automap's draw thread) ---------------------------------------------------------------------------

bool chests_on() { return g_chests_ok && cheats::enabled(cheats::kMapChests); }
bool item_on(ItemKind k) {
  if (!g_items_ok) return false;
#ifdef D2RCC_RUNES_ONLY
  // This build has one switch for all it marks. Which unique and set items those are is the menu's choice (track.h),
  // and while none is chosen their kinds are not looked at at all.
  if (k == kItemUnique || k == kItemSet) return cheats::enabled(cheats::kMapRunes) && track::any_named();
#endif
  return k == kItemUnique                        ? cheats::enabled(cheats::kMapUniques)
         : k == kItemSet                         ? cheats::enabled(cheats::kMapSets)
         : k == kItemRune || k == kItemSpecial   ? cheats::enabled(cheats::kMapRunes)
                                                 : false;
}

// What an item is to the switches, by its Items row and its quality: one of track.h's other rare drops first (told by
// its code), then a rune, a unique or a set item.
ItemKind kind_of_item(const Tables* t, uint32_t cls, int32_t quality) {
  if (cls < t->items && t->special[cls]) return kItemSpecial;
  return item_kind(quality, cls < t->items && t->rune[cls]);
}

// A local addition: whether the user tracks this item now (track.h: the menu's choices). A rune by its number (a rune
// that is none of r01 .. r33, a mod's own: tracked), another rare drop by its group; unique and set items have their
// own switches and no choice here. Asked as an item is drawn and as its drop is said, so a choice changed in the menu
// holds at once.
// The menu's entry of a unique or set item (named.h), -1 when it is none: the row its file index names must be an
// item this build lists, on the base item the unit is.
int named_entry(const Tables* t, uint8_t kind, uint32_t cls, int32_t file_index, named::Kind* of) {
  if ((kind != kItemUnique && kind != kItemSet) || !t || cls >= t->items || file_index < 0) return -1;
  *of = kind == kItemUnique ? named::kUnique : named::kSetItem;
  return named::entry_of(*of, file_index, t->code[cls]);
}

bool tracked(const Tables* t, uint8_t kind, uint32_t cls, int32_t file_index) {
#ifdef D2RCC_RUNES_ONLY
  if (kind == kItemUnique || kind == kItemSet) {
    named::Kind of = named::kUnique;
    const int entry = named_entry(t, kind, cls, file_index, &of);
    return entry >= 0 && track::named(of, entry);
  }
#else
  (void)file_index;
#endif
  if (kind == kItemRune) return !t || cls >= t->items || track::rune(t->rune_no[cls]);
  if (kind == kItemSpecial) {
    const int at = t && cls < t->items ? t->special[cls] : 0;
    return at > 0 && track::group(track::kSpecials[at - 1].group);
  }
  return true;
}

void count_chest(const ChestMark& m, bool was_known, bool was_open) {
  if (m.opened && !was_open) {
    InterlockedIncrement(&g_stat_open);
    if (was_known && log_first(&g_opened_logged, kLogFirst))
      logf("chests: object class %u (level %d) seen opened - its icon goes", m.cls, m.level);
  }
}

// Whether the chest was not known before.
bool note_chest(const ChestMark& m) {
  const uint64_t key = place_key(m.level, m.px, m.py);
  auto it = g_chests.find(key);
  if (it == g_chests.end()) {
    if (g_chests.size() >= kMaxChestMarks) {
      if (log_once(&g_full_warned, LogLevel::kWarning))
        log_warn("chests: %zu marks in this game - no more are kept", kMaxChestMarks);
      return false;
    }
    g_chests.emplace(key, m);
    count_chest(m, false, false);
    return true;
  }
  const bool was_open = it->second.opened;
  it->second.cls = m.cls;
  it->second.opened = was_open || m.opened;  // a chest does not close
  if (m.kind == kSuperChest) it->second.kind = kSuperChest;  // only the server's read knows it; the client's sight keeps it
  count_chest(it->second, true, was_open);
  return false;
}

// A local addition: a rune noted for the first time in a game is said (chatline.h: a line in the game's chat, or on
// screen), once an item and once a place - the server makes an item again with another id when its room was let go and
// built again, and that is the same rune lying where it was. Any thread: the server's read of the ground says it as
// the rune drops, whether the automap is drawn or not; the client's sight says one the server's read did not bring (a
// rune the player put down).
// (`kind`: a rune, said in the game's orange, or one of track.h's other rare drops, said in its gold. One the user does
// not track is not said, and not taken as said: tracked later while it still lies there, it is said then.)
void say_item(uint32_t id, uint8_t bank, int level, int32_t px, int32_t py, uint32_t cls, uint8_t kind,
              int32_t file_index, const char* from) {
  const Tables* t = tables_of(bank);
  if (!tracked(t, kind, cls, file_index)) return;
  AcquireSRWLockExclusive(&g_said_lock);
  const bool new_id = g_said_ids.insert(id).second;
  const bool new_place = g_said.insert(place_key(level, px, py)).second;
  ReleaseSRWLockExclusive(&g_said_lock);
  if (!new_id || !new_place) return;
  const char* name = t && cls < t->items && t->rune_name_at[cls] ? t->rune_names[t->rune_name_at[cls] - 1] : "";
  // A unique or set item is said by its own name, in the colour the game writes such an item's name in.
  named::Kind of = named::kUnique;
  const int entry = named_entry(t, kind, cls, file_index, &of);
  if (entry >= 0) name = named::display(of, entry);
  char text[96];
  std::snprintf(text, sizeof(text), "%s dropped", name[0] ? name : kind == kItemRune ? "A rune" : "A tracked item");
  chatline::announce(text, kind == kItemRune ? chatline::kOrange : kind == kItemSet ? chatline::kGreen : chatline::kGold);
  logf("items: \"%s\" (level %d, %s) handed on to be said (%s)", text, level, from,
       chatline::available() ? "the game's chat" : "on screen");
}

void note_item(uint32_t id, const ItemMark& m, const char* from) {
  if (g_items.size() >= kMaxItemMarks && !g_items.count(id)) {
    if (log_once(&g_full_warned, LogLevel::kWarning))
      log_warn("items: %zu items on the map at once - no more are kept", kMaxItemMarks);
    return;
  }
  g_items[id] = m;
  // Said at every sight of it, which says it once (say_item): also the first time after the menu made it a tracked one.
  if (m.kind != kItemNone && item_on(static_cast<ItemKind>(m.kind)))
    say_item(id, m.bank, m.level, m.px, m.py, m.cls, m.kind, m.named, from);
  if (g_items_counted.emplace(id, true).second) {
    // The first few unique and set items seen in a session, with what they were taken for.
    if ((m.kind == kItemUnique || m.kind == kItemSet) && log_first(&g_named_logged, 8)) {
      const Tables* t = tables_of(m.bank);
      named::Kind of = named::kUnique;
      const int entry = named_entry(t, m.kind, m.cls, m.named, &of);
      const uint32_t code = t && m.cls < t->items ? t->code[m.cls] : 0;
      logf("items: a %s item on the ground: Items row %u (code '%c%c%c%c'), file index %d - %s%s", m.kind == kItemUnique ? "unique" : "set",
           m.cls, code & 0xFF ? static_cast<char>(code & 0xFF) : ' ', (code >> 8) & 0xFF ? static_cast<char>((code >> 8) & 0xFF) : ' ',
           (code >> 16) & 0xFF ? static_cast<char>((code >> 16) & 0xFF) : ' ', (code >> 24) & 0xFF ? static_cast<char>((code >> 24) & 0xFF) : ' ',
           m.named, entry >= 0 ? named::display(of, entry) : "not an item this build lists (its row or its base item is another)",
           entry < 0 ? "" : track::named(of, entry) ? " (tracked)" : " (not tracked)");
    }
    InterlockedIncrement(&g_stat_items_seen);
    if (log_first(&g_item_logged, kLogFirst))
      logf("items: a %s on the ground (level %d) noted for the map, %s",
           m.kind == kItemUnique ? "unique item" : m.kind == kItemSet ? "set item" : m.kind == kItemRune ? "rune"
                                                                                                           : "tracked drop",
           m.level, from);
  }
}

void take_pending() {
  std::vector<Pending> all;
  std::vector<uint32_t> picked;
  EnterCriticalSection(&g_cs);
  all.swap(g_pending);
  picked.swap(g_picked);
  InterlockedExchange(&g_pending_waiting, 0);
  LeaveCriticalSection(&g_cs);
  for (const uint32_t id : picked) {
    const auto it = g_items.find(id);
    if (it == g_items.end()) continue;
    InterlockedIncrement(&g_stat_picked);
    if (log_first(&g_pickup_logged, kLogFirst)) logf("items: an item (level %d) picked up - its icon goes", it->second.level);
    // A mark of the same item read before its room was let go and built again (the server makes the item again with
    // another id) stands on the same spot: it goes too.
    const ItemMark gone = it->second;
    g_items.erase(it);
    for (auto other = g_items.begin(); other != g_items.end();)
      other = other->second.level == gone.level && other->second.px == gone.px && other->second.py == gone.py
                  ? g_items.erase(other)
                  : std::next(other);
  }
  for (const Pending& p : all) {
    const Tables* t = tables_of(p.bank);
    if (!t) continue;
    if (!p.items) {
      for (const uint64_t place : p.fleeting) g_fleeting_places.insert(place);
      InterlockedExchange(&g_stat_fleeting, static_cast<LONG>(g_fleeting_places.size()));
      // The level's marks in the rooms read that the read did not find: the server has them no more there (an object the
      // game threw away as it let the room go, made again without it), so they go. Those in other rooms stay: the server
      // let those rooms go, their objects saved.
      if (!p.rooms.empty()) {
        std::unordered_set<uint64_t> found;
        for (const ServerObject& o : p.objects) found.insert(place_key(p.level, o.px, o.py));
        for (auto it = g_chests.begin(); it != g_chests.end();) {
          const ChestMark& m = it->second;
          if (m.level != p.level || found.count(it->first) || !mapmarks::in_rooms(p.rooms, m.px, m.py)) {
            ++it;
            continue;
          }
          if (!m.opened) {
            InterlockedIncrement(&g_stat_gone);
            if (log_first(&g_gone_logged, kLogFirst))
              logf("chests: object class %u (level %d) is no longer where the server's read of its room looks - its "
                   "icon goes",
                   m.cls, m.level);
          }
          it = g_chests.erase(it);
        }
      }
      for (const ServerObject& o : p.objects) {
        if (o.cls >= static_cast<uint32_t>(t->objects) || t->kind[o.cls] == kNotOpenable) continue;
        ChestMark m;
        m.bank = p.bank;
        m.kind = kind_of(static_cast<ObjectKind>(t->kind[o.cls]), o.sparkle);
        m.opened = opened(o.mode);
        m.level = p.level;
        m.px = o.px;
        m.py = o.py;
        m.cls = static_cast<uint16_t>(o.cls);
        note_chest(m);
      }
      continue;
    }
    // The area's items read again: those of its marks that are no longer on its ground go (picked up), but for one the
    // client knows on the ground (the client's own sight wins) and for one outside the rooms read (its room let go by
    // the server, which keeps the item saved till a player comes near).
    for (auto it = g_items.begin(); it != g_items.end();) {
      bool still = it->second.level != p.level || !mapmarks::in_rooms(p.rooms, it->second.px, it->second.py);
      for (size_t k = 0; k < p.ground.size() && !still; ++k) still = p.ground[k].id == it->first;
      if (!still) {
        Unit* u = game::client_unit(it->first, game::kItem);
        still = u && on_ground(game::unit_mode(u));
      }
      it = still ? std::next(it) : g_items.erase(it);
    }
    for (const ServerItem& s : p.ground) {
      ItemMark m;
      m.named = s.named;
      m.bank = p.bank;
      m.kind = s.kind;
      m.level = p.level;
      m.px = s.px;
      m.py = s.py;
      m.cls = s.cls;
      note_item(s.id, m, "read from the server's rooms of the area");
    }
  }
}

// A unit the automap's pass hands over: an object that opens for loot, or an item on the ground, is noted as the
// client sees it (its place, whether it was opened or picked up).
void seen(Unit* u, uint32_t type) {
  const uint8_t bank = game::unit_table_bank(u);
  const Tables* t = tables_of(bank);
  if (!t) return;
  const uint32_t id = game::unit_id(u);
  const uint32_t cls = game::unit_class(u);
  if (type == game::kObject) {
    if (!chests_on() || cls >= static_cast<uint32_t>(t->objects) || t->kind[cls] == kNotOpenable) return;
    ChestMark m;
    m.bank = bank;
    m.kind = t->kind[cls];
    m.opened = opened(game::unit_mode(u));
    m.level = game::unit_level_id(u);
    m.cls = static_cast<uint16_t>(cls);
    if (m.level <= 0 || !game::unit_pixels(u, &m.px, &m.py)) return;
    if (fleeting(t, cls, m.level)) {
      // Drawn only while the pass hands it over: the game throws it away once its room is let go.
      auto [it, added] = g_fleeting.try_emplace(id);
      it->second.mark = m;
      it->second.seen_at = g_draw;
      if (added && log_first(&g_fleeting_logged, kLogFirst))
        logf("chests: object class %u (level %d) is one the game throws away as it lets its room go (%s) - its icon "
             "is drawn only while it is near you",
             cls, m.level, t->restores[cls] ? "its level keeps no object" : "its class has no Restore");
      return;
    }
    if (note_chest(m) && log_first(&g_chest_logged, kLogFirst))
      logf("chests: object class %u (level %d) seen by the client before the server's read, %s", cls, m.level,
           m.opened ? "open" : "closed");
    return;
  }
  if (!on_ground(game::unit_mode(u))) return;
  const ItemKind k = kind_of_item(t, cls, game::item_quality(u));
  if (k == kItemNone || !item_on(k)) return;
  ItemMark m;
  m.bank = bank;
  m.kind = k;
  m.cls = cls;
  if (k == kItemUnique || k == kItemSet) m.named = game::item_file_index(u);
  m.level = game::unit_level_id(u);
  if (m.level <= 0 || !game::unit_pixels(u, &m.px, &m.py)) return;
  note_item(id, m, "seen by the client");
}

// The level's automap layer, from the level's record: asked once a level a draw at the most.
struct Layers {
  int level[32];
  int layer[32];
  int n = 0;
};
int layer_of(uint8_t bank, int level, Layers* cache) {
  for (int i = 0; i < cache->n; ++i)
    if (cache->level[i] == level) return cache->layer[i];
  const int layer = game::level_layer(bank, level);
  if (cache->n < 32) {
    cache->level[cache->n] = level;
    cache->layer[cache->n++] = layer;
  }
  return layer;
}

// Once a draw (the local player's own call): every chest of the automap's layer the picker shows and not seen opened,
// and every item on its ground whose switch is on.
void draw_all(const void* view) {
  ++g_draw;
  const int layer = game::automap_layer();
  game::MapView mv;
  if (layer < 0 || !game::map_view(view, &mv)) return;
  const sites::SpriteFacts& sf = sites::sprite_facts();
  Layers layers;
  int drawn = 0;
  if (chests_on()) {
    const ChestTier t = tier();
    const auto draw_chest = [&](const ChestMark& m) {
      if (m.opened || !shown(static_cast<ObjectKind>(m.kind), t) || layer_of(m.bank, m.level, &layers) != layer)
        return false;
      uint64_t point = 0;
      const Tables* tb = tables_of(m.bank);
      if (!tb || !game::map_point(view, m.px, m.py, &point)) return false;
      const int cell = m.kind == kQuestChest && m.cls < tb->objects && tb->cell[m.cls] > 0 ? tb->cell[m.cls]
                       : m.kind == kSuperChest || m.kind == kQuestChest                    ? tb->sparkle_cell
                                                                                            : tb->coin_cell;
      int frame = 0;
      const void* sprite = cell > 0 ? game::map_tile_sprite(cell, &frame) : nullptr;
      if (!sprite) return false;
      const CellView cv{mv.cx, mv.cy, mv.ox, mv.oy, mv.sx, mv.sy};
      const CellPlace at = cell_place(cv, mapmarks::cell_x(m.px), mapmarks::cell_y(m.py), sf.cell_left, sf.cell_down,
                                      sf.tile_dy);
      const Tint tint = m.kind == kOpenable ? kOpenableTint : kNoTint;
      game::map_draw_sprite(sprite, frame, at.x, at.y, mv.scale, tint.r, tint.g, tint.b);
      ++drawn;
      return true;
    };
    for (const auto& [key, m] : g_chests) draw_chest(m);
    // What the game throws away as it lets its room go: only while the pass hands it over.
    LONG close_by = 0;
    for (auto it = g_fleeting.begin(); it != g_fleeting.end();) {
      if (fleeting_forgotten(it->second.seen_at, g_draw)) {
        it = g_fleeting.erase(it);
        continue;
      }
      if (fleeting_shown(it->second.seen_at, g_draw) && draw_chest(it->second.mark)) ++close_by;
      ++it;
    }
    InterlockedExchange(&g_stat_fleeting_near, close_by);
  }
  const bool icons = g_icons_loaded && game::hd_graphics();
  int32_t cross_w = 0, cross_h = 0;
  const void* crosses = icons ? nullptr : game::map_cross_sprite(&cross_w, &cross_h);
  for (const auto& [id, m] : g_items) {
    if (!item_on(static_cast<ItemKind>(m.kind)) || layer_of(m.bank, m.level, &layers) != layer) continue;
    if (!tracked(tables_of(m.bank), m.kind, m.cls, m.named)) continue;  // a drop the user does not track now
    // ... or tracks without its icon (the menu's "Minimap icon" switch; the marks themselves are kept, so the icons
    // are back the moment it is switched on).
#ifdef D2RCC_RUNES_ONLY
    if (!track::icons()) continue;
#else
    if ((m.kind == kItemRune || m.kind == kItemSpecial) && !track::icons()) continue;
#endif
    uint64_t point = 0;
    if (game::map_point(view, m.px, m.py, &point)) {
      const int32_t px = static_cast<int32_t>(point), py = static_cast<int32_t>(point >> 32);
      if (icons) {
        // One of track.h's other rare drops takes the unique items' gold star: this build has no unique items' marks.
        const int icon = m.kind == kItemUnique || m.kind == kItemSpecial ? mapicons::kUnique
                         : m.kind == kItemSet                           ? mapicons::kSet
                                                                        : mapicons::kRune;
        const CellPlace at = centred(px, py, mapicons::kSize, mapicons::kSize, mv.scale);
        game::map_draw_sprite(g_icon[icon], 0, at.x, at.y, mv.scale, 1.0f, 1.0f, 1.0f);
      } else if (crosses) {
        const Tint tint = item_tint(static_cast<ItemKind>(m.kind));
        const CellPlace at = centred(px, py, cross_w, cross_h, mv.scale);
        game::map_draw_sprite(crosses, kWhiteCross, at.x, at.y, mv.scale, tint.r, tint.g, tint.b);
      }
      ++drawn;
    }
  }
  if (drawn && log_first(&g_draw_logged, 1))
    logf("map: %d chest and item icons drawn on the automap's layer %d (%s)", drawn, layer,
         icons ? "the plugin's icons" : "the items as crosses");
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::ResourceService* resources) {
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_cs);
    g_cs_ready = true;
  }
  g_ctx = ctx;
  g_tables = tables;
  g_resources = resources;
}

void register_icons(bool last_try) {
  if (!g_ctx || !g_resources || !g_resources->registerResource) {
    if (!last_try) return;
    g_icons_why = "no resource service from the loader";
    logf("map: no resource service from the loader - unique, set and rune items get the automap's cross in their "
         "color");
    return;
  }
  int done = 0;
  bool later = false;
  for (int i = 0; i < mapicons::kIcons; ++i) {
    if (g_registered_file[i][0] && g_registered_file[i][1]) {
      ++done;
      continue;
    }
    const std::vector<uint8_t> pixels = mapicons::paint(static_cast<mapicons::Icon>(i), mapicons::kSize);
    const std::vector<uint8_t> file = mapicons::sprite_file(pixels.data(), mapicons::kSize, mapicons::kSize);
    for (const bool low_end : {false, true}) {
      if (g_registered_file[i][low_end] || file.empty()) continue;
      char path[192];
      icon_path(i, low_end, path, sizeof(path));
      D2RL::Resources::ResourceRegistration r{};
      r.structSize = D2RL::Resources::ResourceRegistrationSize;
      r.flags = 0;
      r.path = path;
      r.bytes = file.data();
      r.byteCount = file.size();
      D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
      const D2RL::Resources::Result res = g_resources->registerResource(g_ctx, &r, &handle);
      if (res == D2RL::Resources::Result::Success) {
        g_registered_file[i][low_end] = true;
      } else if (res == D2RL::Resources::Result::OwnerInactive && !last_try) {
        later = true;  // the plugin still loading: again once it is loaded
      } else {
        log_warn("map: the loader refused the icon file %s (result %u)", path, static_cast<unsigned>(res));
      }
    }
    // The HD file is the one loaded (the low-end one only in the game's low-end texture mode).
    g_registered[i] = g_registered_file[i][0];
    done += g_registered[i] ? 1 : 0;
  }
  if (later) return;
  g_registered_tried = true;
  if (done < mapicons::kIcons) g_icons_why = "the loader refused the plugin's icon files";
  logf("map: %d of %d icon files registered with the loader (%s%s<unique|set|rune|enemy|named>.sprite and .lowend.sprite, %d x "
       "%d pixels)",
       done, mapicons::kIcons, mapicons::kLoaderRoot, mapicons::kSpriteFolder, mapicons::kSize, mapicons::kSize);
}

bool icons_registered() { return g_registered_tried; }

void bind(bool ui_thread) {
  const bool tables = g_tables && g_tables->getTable;
  const char* why = !ui_thread                     ? "no UI thread from the loader"
                    : !g_cs_ready                  ? "not set up"
                    : !game::has_map_draw()        ? "the automap's unit draw not found as expected"
                    : !mapmarks::draw_hooked()     ? "the automap's marker decision not hooked"
                    : !game::has_map_sprites()     ? "the automap's sprites not found as expected"
                    : !tables                      ? "no data table service from the loader"
                                                   : nullptr;
  cheats::set_why_not(cheats::kMapChests, why);
  cheats::set_why_not(cheats::kMapUniques, why);
  cheats::set_why_not(cheats::kMapSets, why);
  cheats::set_why_not(cheats::kMapRunes, why);
  InterlockedExchange(&g_chests_ok, why ? 0 : 1);
  InterlockedExchange(&g_items_ok, why ? 0 : 1);
  logf("map: chests %s, unique, set and rune items %s", why ? why : "ready", why ? why : "ready");
}

bool chests_ready() { return g_chests_ok != 0; }
bool items_ready() { return g_items_ok != 0; }

void collect() {
  if (!g_ctx || !g_tables || (!g_chests_ok && !g_items_ok)) return;
  for (int b = 1; b < kBanks; ++b) {
    Tables& t = g_banks[b];
    if (t.ready || !read_bank(b, &t)) continue;
    InterlockedExchange(&t.ready, 1);
    logf("map: bank %d: %d object classes - %d super chests, %d quest chests, %d other chests, %d other objects that "
         "open for loot (the chest icons: cell %d, the sparkle chest %d); %u Items rows, %d of them runes, %d of them "
         "the other rare drops that can be tracked (of %d known by their codes)",
         b, t.objects, t.counts[kSuperChest], t.counts[kQuestChest], t.counts[kChest], t.counts[kOpenable], t.coin_cell,
         t.sparkle_cell, t.items, t.runes, t.specials, track::kSpecialCount);
    if (t.coin_cell <= 0 || t.sparkle_cell <= 0)
      log_warn("map: bank %d: the automap's chest icons not found in the Objects rows (the stash's %d, the quest "
               "chests' %d) - the chests without one are not drawn",
               b, t.coin_cell, t.sparkle_cell);
    logf("map: bank %d: what the game throws away as it lets a room go - %d classes that open for loot have no Restore "
         "(%s%s%s), and %d of %d levels keep no object (towns and the like): drawn only while near you",
         b, t.fleeting_classes, t.fleeting_classes > 1 ? "the first " : "", t.fleeting_classes ? t.fleeting_first : "none",
         t.fleeting_classes > 1 ? ", ..." : "", t.levels_keeping_none, t.levels);
  }
}

bool ready(uint8_t bank) { return tables_of(bank) != nullptr; }

bool level_keeps_units(uint8_t bank, int level) {
  const Tables* t = tables_of(bank);
  return !t || level < 0 || level >= t->levels || t->keeps_units[level] != 0;
}

ChestTier tier() { return static_cast<ChestTier>(cheats::chest_tier()); }

bool any_items() {
  return g_items_ok &&
         (cheats::enabled(cheats::kMapUniques) || cheats::enabled(cheats::kMapSets) || cheats::enabled(cheats::kMapRunes));
}

ObjectsFound objects_found(uint8_t bank, int level, const ServerObject* objects, size_t count,
                           const std::vector<mapmarks::RoomBox>& rooms) {
  ObjectsFound found;
  const Tables* t = tables_of(bank);
  if (!t || !g_cs_ready || level <= 0) return found;
  Pending p;
  p.items = false;
  p.bank = bank;
  p.level = level;
  p.rooms = rooms;
  for (size_t i = 0; i < count; ++i) {
    if (objects[i].cls >= static_cast<uint32_t>(t->objects) || t->kind[objects[i].cls] == kNotOpenable) continue;
    // What the game throws away as it lets the room go: gone once the server does, which it does to the far rooms some
    // seconds after they were built for this read. The client's sight draws it while it is near.
    if (fleeting(t, objects[i].cls, level)) {
      ++found.fleeting;
      p.fleeting.push_back(place_key(level, objects[i].px, objects[i].py));
      continue;
    }
    const auto k = static_cast<ObjectKind>(t->kind[objects[i].cls]);
    // The sparkle byte as the game has it, or it is not looked at again in this session (the classes say then).
    if (!g_sparkle_bad && !sparkle_holds(k, objects[i].sparkle) && !InterlockedExchange(&g_sparkle_bad, 1))
      log_warn("chests: object class %u (level %d) has the sparkle byte %d, which is not what the game gives it - super "
               "chests are told by their class alone from here on (the sparkling chests of Lower Kurast and the like "
               "are shown as chests)",
               objects[i].cls, level, objects[i].sparkle);
    p.objects.push_back(objects[i]);
    if (kind_of(k, objects[i].sparkle) == kSuperChest) ++found.supers;
  }
  found.handed = static_cast<int>(p.objects.size());
  EnterCriticalSection(&g_cs);
  g_pending.push_back(std::move(p));
  InterlockedExchange(&g_pending_waiting, 1);
  LeaveCriticalSection(&g_cs);
  return found;
}

ItemKind ground_item_kind(Unit* item) {
  if (!item || game::unit_type(item) != game::kItem || !on_ground(game::unit_mode(item))) return kItemNone;
  const Tables* t = tables_of(game::unit_table_bank(item));
  if (!t) return kItemNone;
  const uint32_t cls = game::unit_class(item);
  return kind_of_item(t, cls, game::item_quality(item));
}

ItemKind ground_drop(Unit* item, int* rune_number, int* group, int* entry) {
  *rune_number = 0;
  *group = -1;
  *entry = -1;
  if (!item || !on_ground(game::unit_mode(item)) || game::unit_type(item) != game::kItem) return kItemNone;
  const Tables* t = tables_of(game::unit_table_bank(item));
  const uint32_t cls = game::unit_class(item);
  if (!t || cls >= t->items) return kItemNone;
  if (const int at = t->special[cls]) {
    *group = track::kSpecials[at - 1].group;
    return kItemSpecial;
  }
  if (t->rune[cls]) {
    *rune_number = t->rune_no[cls];
    return kItemRune;
  }
  // A unique or set item the menu lists (asked only while one of them is chosen at all).
  if (!track::any_named()) return kItemNone;
  const ItemKind kind = item_kind(game::item_quality(item), false);
  named::Kind of = named::kUnique;
  *entry = named_entry(t, static_cast<uint8_t>(kind), cls, game::item_file_index(item), &of);
  return *entry >= 0 ? kind : kItemNone;
}

int items_found(uint8_t bank, int level, const ServerItem* items, size_t count,
                const std::vector<mapmarks::RoomBox>& rooms) {
  if (!tables_of(bank) || !g_cs_ready || level <= 0) return 0;
  // A local addition: a rune this read finds is said now, on the game's thread, as it drops - its mark on the map waits
  // for the automap's next draw, and would wait for as long as the automap is not shown.
  for (size_t i = 0; i < count; ++i)
    if (items[i].kind != kItemNone && item_on(items[i].kind))
      say_item(items[i].id, bank, level, items[i].px, items[i].py, items[i].cls, items[i].kind, items[i].named,
               "read from the server's rooms");
  Pending p;
  p.items = true;
  p.bank = bank;
  p.level = level;
  p.ground.assign(items, items + count);
  p.rooms = rooms;
  EnterCriticalSection(&g_cs);
  // A newer read of the level takes the place of one still waiting, with what that one read in rooms this one did not.
  for (size_t k = 0; k < g_pending.size(); ++k)
    if (g_pending[k].items && g_pending[k].level == level) {
      const Pending& old = g_pending[k];
      for (const ServerItem& i : old.ground)
        if (!mapmarks::in_rooms(p.rooms, i.px, i.py)) p.ground.push_back(i);
      p.rooms.insert(p.rooms.end(), old.rooms.begin(), old.rooms.end());
      g_pending.erase(g_pending.begin() + static_cast<ptrdiff_t>(k));
      break;
    }
  g_pending.push_back(std::move(p));
  InterlockedExchange(&g_pending_waiting, 1);
  LeaveCriticalSection(&g_cs);
  InterlockedIncrement(&g_stat_reads);
  return static_cast<int>(count);
}

void dropped(Unit* source) {
  InterlockedExchange(&g_dropped, 1);
  if (source && g_chests_ok && game::unit_type(source) == game::kObject) InterlockedExchange(&g_opened, 1);
}

void item_made(const uint8_t* request, Unit* item) {
  // CreateItemEx's request: +0x00 the unit the item is made for or beside, +0x24 its mode (3 on the ground).
  constexpr uintptr_t kRequestSource = 0x00, kRequestMode = 0x24;
  if (!item || !request || !g_chests_ok || !cheats::enabled(cheats::kMapChests)) return;
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  uintptr_t source = 0;
  int32_t mode = 0;
  if (!mem::read_safe(at + kRequestSource, &source) || !mem::read_safe(at + kRequestMode, &mode) ||
      mode != static_cast<int32_t>(kItemOnGround))
    return;
  if (!source || game::unit_type(reinterpret_cast<Unit*>(source)) == game::kObject) InterlockedExchange(&g_opened, 1);
}

void picked_up(Unit* item) {
  if (!item || !g_cs_ready || !any_items() || game::unit_type(item) != game::kItem) return;
  const uint32_t id = game::unit_id(item);
  EnterCriticalSection(&g_cs);
  if (g_picked.size() < kMaxItemMarks) g_picked.push_back(id);
  InterlockedExchange(&g_pending_waiting, 1);
  LeaveCriticalSection(&g_cs);
}
bool take_dropped() { return g_dropped && InterlockedExchange(&g_dropped, 0); }
bool take_opened() { return g_opened && InterlockedExchange(&g_opened, 0); }

void on_ui(uint64_t now) {
  if (!g_ui_thread) g_ui_thread = GetCurrentThreadId();
  if (g_release && InterlockedExchange(&g_release, 0)) release_icons();
  // Only in a game (an area entered), so that nothing is held at the menus, where the game shuts down. Wanted by an item
  // switch, or by the enemies on the map (their red dots).
  const bool items = g_items_ok && any_items();
  const bool dots = mapmarks::enemies_ready() && cheats::enabled(cheats::kMapEnemies);
  if (g_icons_loaded || (!items && !dots) || game::current_level() <= 0) return;
  load_icons(now);
}

bool enemy_dot(const void* view, uint64_t point, bool is_named) {
  if (!g_icons_loaded || !game::hd_graphics()) return false;
  const float scale = game::map_scale(view) * mapicons::kEnemyDotScale;
  const CellPlace at = centred(static_cast<int32_t>(point), static_cast<int32_t>(point >> 32), mapicons::kSize,
                               mapicons::kSize, scale);
  game::map_draw_sprite(g_icon[is_named ? mapicons::kNamedEnemy : mapicons::kEnemy], 0, at.x, at.y, scale, 1.0f, 1.0f,
                        1.0f);
  return true;
}

void window_closing() {
  // The game shuts down right after (its game, then its UI, which asserts that no sprite is held): no GameLeft reaches
  // the UI callback in time. On the UI thread, where the icons were loaded; else left for the next UI callback.
  if (g_ui_thread && GetCurrentThreadId() == g_ui_thread) release_icons();
  else InterlockedExchange(&g_release, 1);
}

void unit_drawn(Unit* unit, const void* view) {
  if (!unit || !view || (!g_chests_ok && !g_items_ok)) return;
  const bool chests = chests_on(), items = any_items();
  if (!chests && !items) return;
  perf::Timer timer(perf::kMapLootDraw);
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_chests.clear();
    g_items.clear();
    g_items_counted.clear();
    g_fleeting.clear();
    g_fleeting_places.clear();
  }
  const uint32_t type = game::unit_type(unit);
  if ((type == game::kObject && chests) || (type == game::kItem && items)) {
    seen(unit, type);
  } else if (type == game::kPlayer && game::is_local_player(unit)) {
    if (g_pending_waiting) take_pending();
    draw_all(view);
    InterlockedExchange(&g_stat_chests, static_cast<LONG>(g_chests.size()));
    InterlockedExchange(&g_stat_items, static_cast<LONG>(g_items.size()));
  }
}

void game_left() {
  chatline::clear();
  toast::clear();
  AcquireSRWLockExclusive(&g_said_lock);  // the next game's runes are said afresh
  g_said.clear();
  g_said_ids.clear();
  ReleaseSRWLockExclusive(&g_said_lock);
  InterlockedExchange(&g_reset, 1);
  InterlockedExchange(&g_dropped, 1);
  InterlockedExchange(&g_opened, 0);
  if (g_ui_thread && GetCurrentThreadId() == g_ui_thread) release_icons();
  else InterlockedExchange(&g_release, 1);
  if (g_cs_ready) {
    EnterCriticalSection(&g_cs);
    g_pending.clear();
    g_picked.clear();
    InterlockedExchange(&g_pending_waiting, 0);
    LeaveCriticalSection(&g_cs);
  }
  for (volatile LONG* v : {&g_stat_items_seen, &g_stat_picked, &g_stat_reads, &g_stat_chests, &g_stat_open, &g_stat_items,
                           &g_stat_gone, &g_stat_fleeting, &g_stat_fleeting_near})
    InterlockedExchange(v, 0);
}

Stats stats() {
  Stats s;
  s.chests = static_cast<unsigned>(g_stat_chests);
  s.chests_open = static_cast<unsigned>(g_stat_open);
  s.chests_gone = static_cast<unsigned>(g_stat_gone);
  s.fleeting = static_cast<unsigned>(g_stat_fleeting);
  s.fleeting_near = static_cast<unsigned>(g_stat_fleeting_near);
  s.items = static_cast<unsigned>(g_stat_items);
  s.items_seen = static_cast<unsigned>(g_stat_items_seen);
  s.picked_up = static_cast<unsigned>(g_stat_picked);
  s.reads = static_cast<unsigned>(g_stat_reads);
  s.icons_loaded = g_icons_loaded != 0;
  s.icons_why = g_icons_why;
  return s;
}

}  // namespace d2rcc::maploot
