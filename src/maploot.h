#pragma once

#include <D2RLPlugin/api.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mapmarks.h"

namespace d2rcc::game {
struct Unit;
}

// Chests, unique items, set items and runes on the map: four display switches, icons only (asked 2026-11-02: "Add a
// new cheat for making chests visible on the map. Also make a cheats to show: unique, set, and rune as different
// cheat options", "I want all of these to show as icons only", "it's important that each cheat has a unique icon for
// what it shows"; the chests' switch has a picker: super chests only, all chests, everything openable).
//
// What is drawn, and where. The automap draws its object icons (a shrine's, a waypoint's) from its own tiles, through
// the game's sprite draw, and its unit crosses through the same routine. A chest gets the automap's own chest: the
// sparkle chest for a super chest (and a quest chest, whose own icon it is), the coin chest (the stash's icon) for any
// other chest, and the coin chest in grey for whatever else opens for loot - drawn where the game draws an object's
// icon, from the time it is known until it is seen opened. An item on the ground gets the plugin's own icon (mapicons.h:
// a gold star, a green gem, an orange rune stone), centred where the game would put its cross, until it is seen picked
// up. The drawing is the game's: inside the hook on the automap's draw of one unit, at the local player's own call (once
// a draw), its sprite draw with the automap's view; the plugin's icons are loaded by the game's own sprite loader from
// files registered with D2RLoader. In legacy graphics (no HD sprites) an item gets the automap's white cross in its
// color instead. Nothing is written.
//
// Which chests and items. An area's chests are read from the server's side as it is entered: its rooms are built for the
// game's own fill pass, as for the named enemies (roomfill.cpp), and every object in them that the switch's choice
// shows is handed over with whether it is open; the client's own objects say when one is opened. Items on the ground
// are read from the server's rooms of the area the character is in when something has dropped (the drop core), when an
// area is entered and when a switch changes; the client's own items say where one is and when it is picked up.
namespace d2rcc::maploot {

using game::Unit;

// --- the rules (tests/test_maploot.cpp) ---------------------------------------------------------------------------

// The chests' picker.
enum class ChestTier : int { kSuper = 0, kChests = 1, kOpenable = 2 };
constexpr int kTiers = 3;
constexpr const char* kTierNames[kTiers] = {"Super chests only", "All chests", "Everything openable"};

// What an object class is to the chests' switch, by its Objects row.
enum ObjectKind : uint8_t {
  kNotOpenable = 0,  // nothing that opens for loot (shrines, doors, waypoints, decoration)
  kOpenable,         // opens for loot, and is no chest: an urn, a body, a log, a rack, a tomb, a barrel, a stash, a bookshelf
  kChest,            // a chest (the data names it a chest or a casket) the game opens with its chest routine
  kQuestChest,       // a chest with an automap icon of its own: the Horadric Cube's, Scroll's and Staff's, Khalim's
  kSuperChest,       // a chest that sparkles: its init is the super chest's, or its unit's sparkle byte says so
};

// The Objects row's fields read (the loader's rows, the game's own, 0x168 bytes; the compiled objects.bin has the same
// layout): +0x02 the class's name, +0x85 Selectable0 (selectable in its first mode), +0x15C InitFn, +0x15E OperateFn.
// The icon's cell (+0x164) is where the automap's reveal reads it (sites::MapFacts object_cell_at).
constexpr uint32_t kObjectsRow = 0x168;
constexpr size_t kObjectName = 0x02, kObjectNameLen = 0x40;
constexpr size_t kObjectSelectable = 0x85;
constexpr size_t kObjectInit = 0x15C;
constexpr size_t kObjectOperate = 0x15E;
// +0x131 Restore: whether the game keeps the object, saved, when it lets its room go (below).
constexpr size_t kObjectRestore = 0x131;
// A level's record (LevelDefs: the loader's rows, the game's own, 0x9C bytes a level, the row its id; the compiled
// leveldefs.bin has the same layout): +0x94 SaveMonsters, whether the game keeps the level's units when it lets one of its
// rooms go (0x327830 asks it so).
constexpr uint32_t kLevelDefsRow = 0x9C;
constexpr size_t kLevelDefsKeepsUnits = 0x94;

// objects.txt's OperateFn: the index of the routine that runs when the object is operated, in the table 0x23A51C0. 4 is
// the chest's (0x58E2F0: the lock and the key, then the drop). An object opens for loot when its routine makes an item:
// - by the object drop (0x5930B0, the object's treasure class): 1 casket, 3 urn, 4 chest, 5 barrel, 14 breakable (bodies,
//   logs, rocks, jugs, crates ...), 24 the Tainted Sun altar, 39 / 40 / 41 the Horadric Cube's, Scroll's and Staff's
//   chests, 48 the jack-in-the-box, 57 / 58 / 59 Khalim's chests, 68 the evil urn; and 51 Act 3's stashes
//   (JunglePillar0..3), whose routine opens the object and sets off its trap and leaves the drop to the object's event
//   10 (0x593D90, which nothing else schedules), Parm1 frames later (11 to 34);
// - or an item of its own beside the object: 19 the armor stands, 20 the weapon racks, 26 the bookshelves (a Tome or a
//   Scroll of Town Portal or Identify, 0x43FBF0), 33 Wirt's body (his leg, then 10 to 20 piles of gold by its event).
// Not loot: the quest objects that hand over their quest's own item (12 the Inifuss tree, 21 the Malus, 28 Lam Esen's
// Tome: the landmarks' icons show them), 7 the exploding barrel and 30 the exploding cow and chest (they only explode),
// and whatever the player cannot select (the Countess's chest, which a missile of her death opens).
constexpr uint8_t kOperateChest = 4;
constexpr uint8_t kLootOperates[] = {1, 3, 4, 5, 14, 19, 20, 24, 26, 33, 39, 40, 41, 48, 51, 57, 58, 59, 68};
// objects.txt's InitFn 57 (0x50EAC0): the chest's own init (InitFn 3) and then the object's sparkle flag (object data
// +0xC8), which nothing else sets: a super chest (3.3: ConsolationChest "sparklychest" and SpecialChest100).
constexpr uint8_t kInitSuperChest = 57;

constexpr bool loot_operate(uint8_t operate) {
  for (const uint8_t o : kLootOperates)
    if (o == operate) return true;
  return false;
}

// "chest" or "casket" in a class name, in any case.
inline bool chest_name(const char* name) {
  char low[kObjectNameLen + 1] = {};
  for (size_t i = 0; i < kObjectNameLen && name && name[i]; ++i)
    low[i] = static_cast<char>(name[i] >= 'A' && name[i] <= 'Z' ? name[i] - 'A' + 'a' : name[i]);
  return std::strstr(low, "chest") || std::strstr(low, "casket");
}

constexpr bool selectable_kind(bool selectable, uint8_t operate) { return selectable && loot_operate(operate); }

inline ObjectKind object_kind(uint8_t operate, uint8_t init, bool selectable, int32_t cell, const char* name) {
  if (!selectable_kind(selectable, operate)) return kNotOpenable;
  if (init == kInitSuperChest) return kSuperChest;
  if (chest_name(name) && cell > 0) return kQuestChest;
  if (chest_name(name) && operate == kOperateChest) return kChest;
  return kOpenable;
}

// A super chest is a unit's, not only a class's. The game gives an object the sparkle through one setter (0x491910:
// the unit's byte +0xC8, game.cpp object_sparkle): every object's init clears it (0x510BA0), InitFn 57 sets it, and so
// does the placement of a layout's unique chest: a layout's PlaceUniqueChest (class 580) is made into a chest of its
// act's own list by the room's seed (0x510E10 -> 0x50FB20: Lower Kurast's are JungleChest and JungleChest2, Travincal's
// its four, the Arcane Sanctuary's its four, Act 2's tomb chests, thirteen of Acts 1, 4 and 5) and then given the
// sparkle (but in the Maggot Lair's three levels, one in two). Opening a chest leaves the byte. The client is never told
// it (its object packet, 0x51, has no such field), so only the server's read of an area says it.
constexpr ObjectKind with_sparkle(ObjectKind k, int sparkle) {
  return sparkle == 1 && (k == kChest || k == kOpenable) ? kSuperChest : k;
}
// The byte read as the game has it: 0 or 1 on whatever opens for loot (2 is only an object no chest's, class 162), and
// 1 on a class whose init sparkles. Anything else, and the byte is not trusted for the session.
constexpr bool sparkle_holds(ObjectKind class_kind, int sparkle) {
  return class_kind == kNotOpenable || ((sparkle == 0 || sparkle == 1) && (class_kind != kSuperChest || sparkle == 1));
}

constexpr bool shown(ObjectKind k, ChestTier tier) {
  switch (k) {
    case kSuperChest: return true;
    case kQuestChest:
    case kChest: return tier != ChestTier::kSuper;
    case kOpenable: return tier == ChestTier::kOpenable;
    default: return false;
  }
}

// An object is open once it has left its first mode (0: neutral; 1 opening, 2 open).
constexpr bool opened(uint32_t mode) { return mode != 0; }

// What the game keeps of a room it lets go (the server's room unloader, the record: a room's units as it is let go,
// 0x504260): an object is saved, to be made again when the room is built again, only when its level's record keeps its
// units (SaveMonsters; or the unit has flag 0x2000000, which only an item a player dropped and a monster a layout places
// dead get), its class's Restore is set, its sparkle byte has no bit 2 (the one object 0x592210 makes), and, for a class
// with RestoreVirgins, it is still in its first mode. Any other object is freed with the room. The server never fills a
// room again (a room built again takes the fill state its DRLG room kept; only the client's DRLG makes a room's objects
// again), so such an object is gone from then on. An area's rooms are built as it is entered (roomfill.cpp: the enemies'
// and the chests' fill), and the server lets the far ones go some 5 s later: what the game throws away there is made and
// gone before anyone comes near (3.3: Barrel3, "barrel wilderness", the one class that opens for loot with no Restore,
// rolled by the object groups 103 and 105 of Act 5's Bloody Foothills, Frigid Highlands and Arreat Plateau and placed by
// three of the last two's layouts; of the levels the fill reads, Harem Level 1 keeps no object, and nothing there opens
// for loot). So it is not marked from the server's reads; it is drawn only while the client has it (the automap's pass
// hands it over: its room is near the character, and the server keeps the room while it is).
constexpr bool kept_when_let_go(bool class_restores, bool level_keeps_units) {
  return class_restores && level_keeps_units;
}
// Such an object is drawn while the automap's pass has handed it over in one of its last two draws (the pass may come to
// it after the local player's call in one draw and before it in the next), and forgotten after a few more.
constexpr uint32_t kFleetingDraws = 2, kFleetingForget = 16;
constexpr bool fleeting_shown(uint32_t seen_at_draw, uint32_t draw) { return draw - seen_at_draw <= kFleetingDraws; }
constexpr bool fleeting_forgotten(uint32_t seen_at_draw, uint32_t draw) {
  return draw - seen_at_draw > kFleetingForget;
}

// The item switches. A rune is an item of the rune item type (ItemTypes "rune"); a unique or a set item is one of that
// quality (the item's unit data +0x00: 7 unique, 5 set). An item is on the ground in mode 3 (or 5, falling there).
// (kItemSpecial, a local addition: one of track.h's other rare drops - a key, an organ, an essence, a Worldstone Shard,
// a statue or a fragment - told by its Items row; it goes with the runes' switch.)
enum ItemKind : uint8_t { kItemNone = 0, kItemUnique, kItemSet, kItemRune, kItemSpecial, kItemKinds };
constexpr int32_t kQualitySet = 5, kQualityUnique = 7;
constexpr uint32_t kItemOnGround = 3, kItemDropping = 5;
constexpr ItemKind item_kind(int32_t quality, bool rune) {
  return rune ? kItemRune : quality == kQualityUnique ? kItemUnique : quality == kQualitySet ? kItemSet : kItemNone;
}
constexpr bool on_ground(uint32_t mode) { return mode == kItemOnGround || mode == kItemDropping; }

// The game's text colors for them (_profilehd.json: FontColorGoldYellow, FontColorGreen, FontColorOrange): the cross's
// tint in legacy graphics.
struct Tint {
  float r, g, b;
};
constexpr Tint kNoTint{1.0f, 1.0f, 1.0f};
constexpr Tint kOpenableTint{0.55f, 0.55f, 0.6f};  // a grey chest: what opens for loot and is no chest
constexpr Tint item_tint(ItemKind k) {
  return k == kItemUnique || k == kItemSpecial ? Tint{199 / 255.0f, 179 / 255.0f, 119 / 255.0f}
         : k == kItemSet                       ? Tint{0.0f, 252 / 255.0f, 0.0f}
         : k == kItemRune                      ? Tint{1.0f, 168 / 255.0f, 0.0f}
                                               : kNoTint;
}

// Where the automap draws a cell (its list draw, 0xD6F10, for each cell of a layer's lists): the cell's key is the
// unit's pixels / 10 plus (1, -3) (mapmarks.h cell_x / cell_y); the tile's place is the key less (left, tileset's dy -
// down), times the view's scale, less the view's centre times its scale, plus the view's origin, each product floored.
struct CellView {
  int32_t cx = 0, cy = 0;  // the view's centre (+0x10 / +0x14)
  int32_t ox = 0, oy = 0;  // ... its origin (+0x28 / +0x2C)
  float sx = 1.0f, sy = 1.0f;  // ... its scale (+0x30 / +0x34)
};
struct CellPlace {
  int32_t x = 0, y = 0;
};
inline CellPlace cell_place(const CellView& v, int32_t key_x, int32_t key_y, int left, int down, int32_t tile_dy) {
  const int32_t x = static_cast<int32_t>(std::floor(static_cast<float>(key_x - left) * v.sx));
  const int32_t y = static_cast<int32_t>(std::floor(static_cast<float>(key_y - tile_dy + down) * v.sy));
  const int32_t cx = static_cast<int32_t>(std::floor(static_cast<float>(v.cx) * v.sx));
  const int32_t cy = static_cast<int32_t>(std::floor(static_cast<float>(v.cy) * v.sy));
  return CellPlace{v.ox + x - cx, v.oy + y - cy};
}

// A sprite centred on a point, as the automap's marker draw places its cross: the point less half the frame (an int
// halved toward zero) times the scale, truncated.
inline CellPlace centred(int32_t px, int32_t py, int32_t width, int32_t height, float scale) {
  return CellPlace{px - static_cast<int32_t>(static_cast<float>(width / 2) * scale),
                   py - static_cast<int32_t>(static_cast<float>(height / 2) * scale)};
}

// --- the plugin ----------------------------------------------------------------------------------------------------

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::ResourceService* resources);
// At the plugin's load: the icon files registered with D2RLoader (the Resource service), under the plugin's folder; the
// ones the loader takes only once the plugin has loaded (it says so) again from the worker (last_try).
void register_icons(bool last_try);
bool icons_registered();
// After sites::derive() and game::bind() (ui_thread: the loader gives UI callbacks): the switches' why_not.
void bind(bool ui_thread);
bool chests_ready();
bool items_ready();

// Game thread (the tick): the Objects and Items rows the two read, per table bank; once a bank is read it is kept.
void collect();
bool ready(uint8_t bank);
// Any thread: whether the game keeps a level's units as it lets one of its rooms go (its LevelDefs row's SaveMonsters;
// true while the bank's rows are not read, or for a level they do not have). A level that keeps none is not filled ahead
// (roomfill.cpp): what a read made in its far rooms would be thrown away before anyone came.
bool level_keeps_units(uint8_t bank, int level);
// The chests' picker, and whether any item switch is on (any thread).
ChestTier tier();
bool any_items();

// The server thread (roomfill.cpp). An area's objects as it is entered (its rooms built and filled): each one the
// switch's choice could show, with whether it is open and its sparkle byte (-1 unread); and the items on the ground in
// its built rooms. `rooms`: the rooms read (built and filled), whose objects these are: a mark of the area's in one of them
// that is not among them goes (the server has it no more), those in other rooms stay (the server let those rooms go, their
// objects saved). What the game throws away as it lets a room go is not handed over (kept_when_let_go). Returns how many
// were handed over, how many of them are super chests, and how many the game does not keep were left out.
struct ServerObject {
  uint32_t id = 0, cls = 0, mode = 0;
  int32_t px = 0, py = 0;
  int sparkle = -1;
};
struct ObjectsFound {
  int handed = 0, supers = 0, fleeting = 0;
};
ObjectsFound objects_found(uint8_t bank, int level, const ServerObject* objects, size_t count,
                           const std::vector<mapmarks::RoomBox>& rooms);
struct ServerItem {
  uint32_t id = 0;
  ItemKind kind = kItemNone;
  int32_t px = 0, py = 0;
  uint32_t cls = UINT32_MAX;  // its Items row (a local addition: the rune drop message names the rune)
  int32_t named = -1;         // a unique or set item's file index (its row in the game's table of them)
};
// The items on the ground of a server unit, if it is one the switches can show (else kItemNone).
ItemKind ground_item_kind(Unit* item);
// A local addition (the light pillars, pillars.h). An item unit lying on the ground that is a rune or one of track.h's
// other rare drops: which of the two (else kItemNone), a rune's number (0: none of r01 .. r33) and another drop's group
// (-1: a rune). A unique or set item the menu lists (named.h) is kItemUnique or kItemSet with its entry there in
// *entry (-1 for the others). The client's units or the server's, any thread: the tables are only read once they
// are ready.
ItemKind ground_drop(Unit* item, int* rune_number, int* group, int* entry);
// The items of the rooms read (`rooms`: the area's built rooms, whose units these are): the area's earlier marks in those
// rooms that are not among them go; those in other rooms stay (the server let those rooms go, the items saved with them).
int items_found(uint8_t bank, int level, const ServerItem* items, size_t count,
                const std::vector<mapmarks::RoomBox>& rooms);
// The server thread, the drop core's hook: something dropped, so the items of the area may be read again; and when its
// source is an object, that object was opened (the object drop 0x5930B0 runs inside its operate routine, after the
// routine set its mode), so the area's chests are read again too. The client is never told a chest opened in a way
// the automap's pass shows (its object state packet 0x0E is a stub in 3.3), so this is what takes an opened chest's
// icon off.
void dropped(Unit* source);
// The server thread, the hook on item creation: an item the game has just made (the request it was made from). One made
// on the ground for an object, or for nothing, is an object's own item, made without the drop core: a rack's (its maker
// names no source), a bookshelf's or Wirt's body's (the game's item maker 0x43FBF0 names the object); that object was
// opened, so the area's chests are read again as for a drop.
void item_made(const uint8_t* request, Unit* item);
// The server thread, the hook on the item notice: the pick-up routine's notice of the item it took off the ground.
void picked_up(Unit* item);
bool take_dropped();  // the server thread: whether something dropped since it last asked
bool take_opened();   // the server thread: whether an object was opened (it dropped) since it last asked

// The UI thread (the loader's UI callback, once a frame): the plugin's icons loaded by the game's sprite loader, the
// first time an item switch is on in HD graphics.
void on_ui(uint64_t now_ms);
// The automap's draw thread: an enemy's red dot (the plugin's icon, by the game's sprite draw) centred on an automap
// point, a named enemy's ringed in orange; false when the icons are not loaded (legacy graphics), and the caller draws
// its cross.
bool enemy_dot(const void* view, uint64_t point, bool is_named);
// The game's window is closing (its WM_CLOSE, overlay.cpp's subclass): the icons let go now, as the game shuts its UI
// down right after (an icon still held is an assertion box in d2rloader.log; GameLeft does not reach the UI first).
void window_closing();

// Each unit the automap's draw of one unit is handed (the thread that draws the automap), from the hook on the
// marker decision it asks first: after the game's decision, before the game draws the unit's own marker.
void unit_drawn(Unit* unit, const void* view);

void game_left();

struct Stats {
  unsigned chests = 0;        // chest marks known in this game (each its object), of them ...
  unsigned chests_open = 0;   // ... seen opened
  unsigned chests_gone = 0;   // ... taken away: a read of their room did not find them
  unsigned fleeting = 0;      // objects the game throws away as it lets their room go, left out of the server's reads
  unsigned fleeting_near = 0;  // ... and of them, drawn now (the client has them)
  unsigned items = 0;         // items on the ground marked now
  unsigned items_seen = 0;    // ... marked in this game (each once)
  unsigned picked_up = 0;     // ... seen picked up
  unsigned reads = 0;         // the server's reads of an area's items
  bool icons_loaded = false;  // the plugin's icons were loaded by the game's sprite loader
  const char* icons_why = nullptr;  // ... and if not, why
};
Stats stats();

}  // namespace d2rcc::maploot
