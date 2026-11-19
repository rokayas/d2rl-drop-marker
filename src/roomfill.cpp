#include "roomfill.h"

#include <windows.h>

#include <vector>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "maploot.h"
#include "mapmarks.h"
#include "mem.h"
#include "perf.h"
#include "sites.h"

namespace d2rcc::roomfill {
namespace {

constexpr int kMaxRooms = 8192;          // an area's room list is walked no further
constexpr int kMaxRoomUnits = 4096;      // ... nor a room's units
constexpr int kRoomsPerTick = 8;         // rooms built in one server frame at the most
constexpr double kBudgetMs = 2.0;        // ... and for this long at the most
constexpr ULONGLONG kFillWaitMs = 2000;  // the game's fill pass gets this long for the rooms built (it fills them in
                                         // the frame they are built; the server lets a room no player is near go
                                         // some 5 s after: mapmarks.h RoomBox)
constexpr uint32_t kSafetyTicks = 25;    // a look once a second (server frames) besides what announces an area

// The area being filled (the server thread only). The level pointer is only compared: the room list is walked again
// from the level the character stands in at each tick.
struct Job {
  void* level = nullptr;
  void* drlg = nullptr;
  int id = -1;
  uint8_t bank = 0;
  int built = 0, failed = 0;
  bool all_built = false;
  ULONGLONG started = 0, built_at = 0;
  std::vector<void*> refused;  // rooms the game could not build: not asked again
};
// What a full read of an area brings (its rooms built for the game to fill, then every room of it read): the objects
// the landmarks put on, the named enemies, the others too ("All enemies"), the chests.
constexpr uint8_t kReadLandmarks = 1, kReadNamed = 2, kReadAll = 4, kReadChests = 8;
struct Done {
  void* drlg;
  int id;
  uint8_t read;  // what its full reads have brought so far (a switch that asks for more has it read in full again)
};

Job g_job;
std::vector<Done> g_done;  // the areas filled (or with nothing to fill) on a DRLG of this game
void* g_read_drlg = nullptr;  // the area whose named enemies were read last (an area entered is read again)
int g_read_id = -1;
void* g_items_drlg = nullptr;  // the area whose items on the ground were read last
int g_items_id = -1;
volatile LONG g_ready = 0, g_armed = 1, g_reset = 0;
volatile LONG g_areas = 0, g_rooms = 0, g_objects = 0, g_monsters = 0, g_chests = 0, g_reads_logged = 0,
              g_items_logged = 0, g_opened_logged = 0;
LARGE_INTEGER g_freq{};

Done* done_of(void* drlg, int id) {
  for (Done& d : g_done)
    if (d.drlg == drlg && d.id == id) return &d;
  return nullptr;
}

double ms_since(const LARGE_INTEGER& t0) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return g_freq.QuadPart ? static_cast<double>(now.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart)
                         : 0.0;
}

bool refused(void* room) {
  for (void* r : g_job.refused)
    if (r == room) return true;
  return false;
}

// Whether the game's fill pass has gone over a built room: its filled bit, and its monsters' bit when known (a room
// built again after the server let it go has the filled bit from the start, and the pass then makes its saved units
// again and sets the other).
bool filled(void* active) {
  const sites::FillFacts& f = sites::fill_facts();
  const uint32_t bits = f.filled_bit | f.monsters_bit;
  uint32_t state = 0;
  return mem::read_safe(reinterpret_cast<uintptr_t>(active) + static_cast<uintptr_t>(f.room_state_at), &state) &&
         (state & bits) == bits;
}

// Some rooms built: as many as the frame's share allows. True when no room is left to build.
bool build_some() {
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  int now = 0, n = 0;
  for (void* room = game::level_first_room(g_job.level); room && n < kMaxRooms; room = game::room_next(room), ++n) {
    if (game::room_built(room) || refused(room)) continue;
    if (now >= kRoomsPerTick || ms_since(t0) >= kBudgetMs) return false;
    ++now;
    if (game::build_room(g_job.bank, room)) {
      ++g_job.built;
    } else {
      ++g_job.failed;
      g_job.refused.push_back(room);
    }
  }
  return true;
}

// Whether every built room of the area has been filled by the game's pass.
bool all_filled() {
  int n = 0;
  for (void* room = game::level_first_room(g_job.level); room && n < kMaxRooms; room = game::room_next(room), ++n)
    if (void* active = game::room_built(room); active && !filled(active)) return false;
  return true;
}

// What the area's built rooms hold: its objects (those with an icon go on the map; and for the chests, each with its id
// and mode), its live monsters (the named ones are marked) and its items on the ground (the unique, set and rune items
// the switches show), and where those rooms are (the server keeps the units of the others saved: what a read does not
// see there is not gone). A room built and not filled yet is not among them: the game has not made its units again (a
// room let go and built again gets its saved units back in the fill pass's frame step). Returns how many rooms are
// built, and how many of them the game has not filled.
struct Found {
  std::vector<mapmarks::PlacedObject>* objects = nullptr;
  std::vector<Unit*>* monsters = nullptr;
  std::vector<maploot::ServerObject>* chests = nullptr;
  std::vector<maploot::ServerItem>* items = nullptr;
  std::vector<mapmarks::RoomBox>* rooms = nullptr;
  int* unfilled = nullptr;
};
int units_of(void* level, const Found& want) {
  const sites::FillFacts& f = sites::fill_facts();
  int n = 0, rooms = 0;
  for (void* room = game::level_first_room(level); room && n < kMaxRooms; room = game::room_next(room), ++n) {
    void* active = game::room_built(room);
    if (!active) continue;
    ++rooms;
    const bool done = filled(active);
    if (want.unfilled && !done) ++*want.unfilled;
    int32_t x = 0, y = 0, w = 0, h = 0;
    if (want.rooms && done && game::room_tiles(room, &x, &y, &w, &h))
      want.rooms->push_back(mapmarks::RoomBox{x * 5, y * 5, (x + w) * 5, (y + h) * 5});
    uintptr_t unit = mem::read_ptr(reinterpret_cast<uintptr_t>(active) + static_cast<uintptr_t>(f.room_units_at));
    for (int k = 0; unit && k < kMaxRoomUnits; ++k) {
      Unit* u = reinterpret_cast<Unit*>(unit);
      const uint32_t type = game::unit_type(u);
      int32_t px = 0, py = 0;
      if (type == game::kObject && (want.objects || want.chests) && game::unit_pixels(u, &px, &py)) {
        if (want.objects) want.objects->push_back(mapmarks::PlacedObject{game::unit_class(u), px, py});
        if (want.chests)
          want.chests->push_back(maploot::ServerObject{game::unit_id(u), game::unit_class(u), game::unit_mode(u), px, py,
                                                       game::object_sparkle(u)});
      } else if (want.monsters && type == game::kMonster && !game::unit_is_dead(u)) {
        want.monsters->push_back(u);
      } else if (want.items && type == game::kItem) {
        if (const maploot::ItemKind kind = maploot::ground_item_kind(u);
            kind != maploot::kItemNone && game::unit_pixels(u, &px, &py))
          want.items->push_back(maploot::ServerItem{game::unit_id(u), kind, px, py, game::unit_class(u)});
      }
      unit = mem::read_ptr(unit + static_cast<uintptr_t>(f.unit_next_at));
    }
  }
  return rooms;
}

// What a read of an area's objects left off the map: those the game throws away as it lets their room go (maploot.h
// kept_when_let_go), for the log's line.
struct Note {
  char s[112] = {};
};
Note fleeting_note(const maploot::ObjectsFound& found) {
  Note n;
  if (found.fleeting)
    std::snprintf(n.s, sizeof(n.s), "; %d more the game throws away as it lets their rooms go, drawn only while near you",
                  found.fleeting);
  return n;
}

// The enemies and the chests of an area read in full before, read again as it is entered or a switch changes (monsters
// may have died, or been joined by others: a terror zone's Heralds come with kills; chests may have been opened): from
// the rooms the server has built now, those near the character; the marks elsewhere stay as they were.
void read_again(void* level, int id, uint8_t bank, bool enemies, bool chests) {
  std::vector<Unit*> monsters;
  std::vector<maploot::ServerObject> objects;
  std::vector<mapmarks::RoomBox> rooms;
  Found want;
  want.monsters = enemies ? &monsters : nullptr;
  want.chests = chests ? &objects : nullptr;
  want.rooms = &rooms;
  units_of(level, want);
  const int named = enemies ? mapmarks::monsters_found(bank, id, monsters.data(), monsters.size(), rooms) : 0;
  const maploot::ObjectsFound loot =
      chests ? maploot::objects_found(bank, id, objects.data(), objects.size(), rooms) : maploot::ObjectsFound{};
  if (log_first(&g_reads_logged, 4))
    logf("%s: level %d entered again: %d of its %zu monsters named, %d of its objects open for loot (%d super "
         "chests%s), read from the %zu rooms the server has built and filled now",
         enemies ? "named enemies" : "chests", id, named, monsters.size(), loot.handed, loot.supers,
         fleeting_note(loot).s, rooms.size());
}

// The chests of the area the character is in, read again as one was opened (an object's drop): their modes say which.
void read_chests(void* level, int id, uint8_t bank) {
  std::vector<maploot::ServerObject> objects;
  std::vector<mapmarks::RoomBox> rooms;
  Found want;
  want.chests = &objects;
  want.rooms = &rooms;
  units_of(level, want);
  const maploot::ObjectsFound loot = maploot::objects_found(bank, id, objects.data(), objects.size(), rooms);
  if (log_first(&g_opened_logged, 4))
    logf("chests: level %d: an object opened (it dropped) - the area's %d objects that open for loot read again from "
         "the server%s",
         id, loot.handed, fleeting_note(loot).s);
}

// The items on the ground of the area the character is in (its built rooms: an item lies only where the game has been
// near): the ones an item switch shows go to the map, and those of the area's earlier read that are not among them go.
void read_items(void* level, int id, uint8_t bank, const char* why) {
  std::vector<maploot::ServerItem> items;
  std::vector<mapmarks::RoomBox> boxes;
  Found want;
  want.items = &items;
  want.rooms = &boxes;
  const int rooms = units_of(level, want);
  maploot::items_found(bank, id, items.data(), items.size(), boxes);
  if (log_first(&g_items_logged, 4))
    logf("items: level %d (%s): %zu unique, set and rune items on the ground of its %d built rooms, read from the "
         "server",
         id, why, items.size(), rooms);
}

// A full read: every object in the area's rooms, all built now, handed to the map (those with an icon go on it; for the
// chests, those that open for loot), and every monster (the named ones are marked, under "All enemies" the others too).
// `again`: the area was read in full before, and a switch asks for more (the rooms the server had let go since were
// built again, and the game made their saved units again).
void report(ULONGLONG now, uint8_t need, bool again) {
  const bool landmarks = (need & kReadLandmarks) != 0, enemies = (need & kReadNamed) != 0,
             chests = (need & kReadChests) != 0;
  std::vector<mapmarks::PlacedObject> objects;
  std::vector<Unit*> monsters;
  std::vector<maploot::ServerObject> loot;
  std::vector<mapmarks::RoomBox> boxes;
  int unfilled = 0;
  Found want;
  want.objects = landmarks ? &objects : nullptr;
  want.monsters = enemies ? &monsters : nullptr;
  want.chests = chests ? &loot : nullptr;
  want.rooms = &boxes;
  want.unfilled = &unfilled;
  const int rooms = units_of(g_job.level, want);
  const int icons = landmarks ? mapmarks::objects_found(g_job.bank, g_job.id, objects.data(), objects.size()) : 0;
  const int named =
      enemies ? mapmarks::monsters_found(g_job.bank, g_job.id, monsters.data(), monsters.size(), boxes) : 0;
  const maploot::ObjectsFound found =
      chests ? maploot::objects_found(g_job.bank, g_job.id, loot.data(), loot.size(), boxes) : maploot::ObjectsFound{};
  InterlockedIncrement(&g_areas);
  InterlockedExchangeAdd(&g_rooms, g_job.built);
  InterlockedExchangeAdd(&g_objects, icons);
  InterlockedExchangeAdd(&g_monsters, named);
  InterlockedExchangeAdd(&g_chests, found.handed);
  char what[384] = {};
  size_t at = 0;
  if (landmarks)
    at += static_cast<size_t>(std::snprintf(what + at, sizeof(what) - at, "%zu objects in the area, %d with an icon "
                                            "handed to the map", objects.size(), icons));
  if (enemies && at < sizeof(what))
    at += static_cast<size_t>(std::snprintf(what + at, sizeof(what) - at, "%s%zu monsters in the area, %d of them named",
                                            at ? "; " : "", monsters.size(), named));
  if (chests && at < sizeof(what))
    std::snprintf(what + at, sizeof(what) - at, "%s%d objects that open for loot (%d super chests%s)", at ? "; " : "",
                  found.handed, found.supers, fleeting_note(found).s);
  logf("%s: level %d: %d rooms built ahead for the game to fill (%d were built already, %d could not be), filled in "
       "%llu ms%s%s; %s",
       landmarks ? "landmarks" : enemies ? "named enemies" : "chests", g_job.id, g_job.built,
       rooms > g_job.built ? rooms - g_job.built : 0, g_job.failed, static_cast<unsigned long long>(now - g_job.started),
       unfilled ? " (some not filled in time)" : "",
       again ? " - read in full again, a switch asking for more than its last full read" : "", what);
}

}  // namespace

void bind() {
  QueryPerformanceFrequency(&g_freq);
  const sites::FillFacts& f = sites::fill_facts();
  const bool ok = f.known && game::has_room_build();
  InterlockedExchange(&g_ready, ok ? 1 : 0);
  logf("landmarks: the shrines the game rolls %s; named enemies %s; chests %s",
       ok ? "are put on the map ahead (the rooms of an area built as it is entered, for the game to fill)"
          : "are put on the map as the game shows them (its room fill not found as expected)",
       ok ? "are read from the server's side as an area is entered (its rooms filled the same way)"
          : "are marked as the client sees them",
       ok ? "too, and the items on the ground after each drop" : "and items as the client sees them");
}

bool ready() { return g_ready != 0; }
void arm() { InterlockedExchange(&g_armed, 1); }

void on_tick(void* game, Unit* player, uint32_t tick) {
  if (!g_ready) return;
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_job = Job{};
    g_done.clear();
    g_read_drlg = nullptr;
    g_read_id = -1;
    g_items_drlg = nullptr;
    g_items_id = -1;
  }
  const bool landmarks = cheats::enabled(cheats::kMapLandmarks) && mapmarks::landmarks_ready();
  const bool enemies = cheats::enabled(cheats::kMapEnemies) && mapmarks::enemies_ready();
  const bool chests = cheats::enabled(cheats::kMapChests) && maploot::chests_ready();
  const bool items = maploot::any_items();
  if (!landmarks && !enemies && !chests && !items) {
    g_job = Job{};
    g_read_drlg = nullptr;
    g_read_id = -1;
    g_items_drlg = nullptr;
    g_items_id = -1;
    return;
  }
  // Looked at when something says the character may stand somewhere new (or, for the items, that something dropped),
  // once a second besides, and at every frame while an area is being filled.
  const bool dropped = items && maploot::take_dropped();
  const bool opened = chests && maploot::take_opened();
  const bool armed = g_armed != 0;
  if (!g_job.level && !armed && !dropped && !opened && tick % kSafetyTicks != 0) return;
  if (armed) InterlockedExchange(&g_armed, 0);
  void* level = game::unit_level(player);
  void* drlg = game::level_drlg(level);
  const int id = game::level_id_of(level);
  if (!level || !drlg || id <= 0) return;
  const uint8_t bank = game::game_bank(game);
  // The items on the ground: read when something dropped, when an area is entered, when a switch changed.
  if (items && maploot::ready(bank) && (armed || dropped || drlg != g_items_drlg || id != g_items_id)) {
    read_items(level, id, bank, dropped ? "a drop" : armed ? "an area or a switch" : "entered");
    g_items_drlg = drlg;
    g_items_id = id;
  }
  // The chests of the area again when an object dropped: it was opened (its mode set before its drop), and the client
  // never shows that to the automap's pass.
  if (opened && maploot::ready(bank)) read_chests(level, id, bank);
  // The tables, in the first seconds of a game.
  const bool marks = mapmarks::ready(bank), loot = maploot::ready(bank);
  const bool lm = landmarks && marks, en = enemies && marks, ch = chests && loot;
  if (!lm && !en && !ch) {
    g_job = Job{};
    return;
  }
  // What a full read of this area is to bring. The landmarks: an area whose object groups can roll an object with an
  // icon. The enemies and the chests: every area but a town (its monsters are made, and its object groups rolled, as
  // its rooms are filled). None of them in a level whose record keeps no units (the towns, Harem Level 1): the game
  // throws away what its far rooms' fill made as it lets them go, some seconds after (maploot.h kept_when_let_go).
  const bool keeps = maploot::level_keeps_units(bank, id);
  uint8_t need = 0;
  if (lm && keeps && mapmarks::level_rolls_icons(bank, id)) need |= kReadLandmarks;
  if (keeps && !game::town_level(id)) {
    if (en) need |= kReadNamed;
    if (en && static_cast<mapmarks::EnemyTier>(cheats::enemy_tier()) == mapmarks::EnemyTier::kAll) need |= kReadAll;
    if (ch) need |= kReadChests;
  }
  Done* done = done_of(drlg, id);
  const uint8_t has = done ? done->read : 0;
  if (!(need & ~has)) {
    // Everything asked for here was read in full before: the enemies and the chests are read again as the area is
    // entered (a switch changed counts too), from the rooms the server has built now; a mark elsewhere stays, its unit
    // saved with its room (a read in full would build the area's rooms again, and is made only when a switch asks for
    // more than the last one brought: "All enemies", the chests, the landmarks switched on in an area read before).
    const bool entered = armed || drlg != g_read_drlg || id != g_read_id;
    const bool again_en = en && (has & kReadNamed), again_ch = ch && (has & kReadChests);
    if ((again_en || again_ch) && entered) read_again(level, id, bank, again_en, again_ch);
    g_read_drlg = drlg;
    g_read_id = id;
    g_job = Job{};
    return;
  }
  perf::Timer timer(perf::kRoomFill);
  const ULONGLONG now = GetTickCount64();
  if (g_job.level != level || g_job.drlg != drlg || g_job.id != id) {
    g_job = Job{};
    g_job.level = level;
    g_job.drlg = drlg;
    g_job.id = id;
    g_job.bank = bank;
    g_job.started = now;
  }
  if (!g_job.all_built) {
    if (!build_some()) return;  // more at the next frame
    g_job.all_built = true;
    g_job.built_at = now;
    return;  // the game fills them in its own pass
  }
  if (!all_filled() && now - g_job.built_at < kFillWaitMs) return;
  report(now, need, has != 0);
  if (done) done->read |= need;
  else g_done.push_back(Done{drlg, id, need});
  g_read_drlg = drlg;
  g_read_id = id;
  g_job = Job{};
}

void game_left() {
  InterlockedExchange(&g_reset, 1);
  InterlockedExchange(&g_armed, 1);
  for (volatile LONG* v : {&g_areas, &g_rooms, &g_objects, &g_monsters, &g_chests, &g_reads_logged, &g_items_logged, &g_opened_logged})
    InterlockedExchange(v, 0);
}

Stats stats() {
  Stats s;
  s.areas = static_cast<unsigned>(g_areas);
  s.rooms = static_cast<unsigned>(g_rooms);
  s.objects = static_cast<unsigned>(g_objects);
  s.monsters = static_cast<unsigned>(g_monsters);
  s.chests = static_cast<unsigned>(g_chests);
  return s;
}

}  // namespace d2rcc::roomfill
