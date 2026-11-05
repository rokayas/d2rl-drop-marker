// Chests, unique, set and rune items on the map (src/maploot.h, src/mapicons.h): the rules, the plugin's icons and their
// file, where the automap puts a cell and a cross; and, when the game's tables are at hand (D2RCC_EXCEL = the loader's
// compiled excel folder), the chest rule over every Objects row (2 super chests, 6 quest chests, 46 other chests, 126
// other objects that open for loot, Act 3's stashes, the bookshelves and Wirt's body among them), what the game throws
// away as it lets a room go (of what opens for loot only Barrel3 has no Restore; the levels whose record keeps no units:
// the towns, Harem Level 1 and the null level) and the runes among the Items rows (the 33 of the rune item type).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mapicons.h"
#include "maploot.h"

using namespace d2rcc;
using namespace d2rcc::maploot;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

template <typename T>
static T at(const std::vector<uint8_t>& d, size_t offset) {
  T v{};
  if (offset + sizeof(T) <= d.size()) std::memcpy(&v, d.data() + offset, sizeof(T));
  return v;
}

// The rows of a compiled table (a D2RLBIN file: the first of its tables with rows of this size).
static bool rows_of(const std::string& path, uint32_t row_size, std::vector<uint8_t>* rows, uint32_t* count) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.size() < 0x60 || std::memcmp(d.data(), "D2RLBIN", 7) != 0) return false;
  const uint32_t tables = at<uint32_t>(d, 0x3C), size = at<uint32_t>(d, 0x40);
  for (uint32_t t = tables; t + 0x50 <= tables + size; t += 0x50) {
    const uint32_t n = at<uint32_t>(d, t + 12), each = at<uint32_t>(d, t + 16), where = at<uint32_t>(d, t + 36);
    if (each != row_size || static_cast<uint64_t>(where) + static_cast<uint64_t>(n) * each > d.size()) continue;
    rows->assign(d.begin() + where, d.begin() + where + static_cast<size_t>(n) * each);
    *count = n;
    return true;
  }
  return false;
}

static void rules() {
  // What an object is: by the routine it is operated with, its init, whether it can be selected, its icon and its name.
  CHECK(object_kind(4, 3, true, 0, "LargeChestR") == kChest);
  CHECK(object_kind(4, 3, true, 0, "ArcaneCasket1") == kChest);        // a casket the chest routine opens
  CHECK(object_kind(1, 1, true, 0, "Casket5") == kOpenable);           // ... and one the casket routine opens
  CHECK(object_kind(4, 57, true, 0, "ConsolationChest") == kSuperChest);
  CHECK(object_kind(39, 31, true, 318, "HoradricCubeChest") == kQuestChest);
  CHECK(object_kind(57, 0, true, 318, "KhalimHeartChest") == kQuestChest);
  CHECK(object_kind(4, 3, true, 0, "DeadBarbarian") == kOpenable);     // the chest routine, no chest
  CHECK(object_kind(14, 3, true, 0, "RogueCorpse1") == kOpenable);
  CHECK(object_kind(20, 0, true, 0, "WeaponRack1") == kOpenable);
  CHECK(object_kind(19, 0, true, 0, "ArmorStand1") == kOpenable);
  CHECK(object_kind(51, 2, true, 0, "JunglePillar1") == kOpenable);  // Act 3's stash: its drop comes by its event 10
  CHECK(object_kind(26, 2, true, 0, "Bookshelf1") == kOpenable);     // a tome or a scroll of its own
  CHECK(object_kind(33, 0, true, 0, "Wirt") == kOpenable);           // his leg, then gold
  CHECK(object_kind(4, 0, false, 0, "MephistoBridge") == kNotOpenable);  // not selectable
  CHECK(object_kind(30, 0, true, 0, "ExplodingChest100") == kNotOpenable);  // a trap: no drop
  CHECK(object_kind(7, 0, true, 0, "BarrelExploding") == kNotOpenable);     // it only explodes
  CHECK(object_kind(12, 9, true, 313, "InifussTree") == kNotOpenable);      // a quest's own item
  CHECK(object_kind(21, 15, true, 316, "Malus") == kNotOpenable);
  CHECK(object_kind(28, 23, true, 427, "LamEsensTome") == kNotOpenable);
  CHECK(object_kind(2, 0, true, 310, "Shrine") == kNotOpenable);
  CHECK(object_kind(23, 0, true, 307, "Waypoint") == kNotOpenable);
  CHECK(chest_name("SNOWCLOTHCHESTL") && chest_name("woodchest2R") && !chest_name("BaalTomb1") && !chest_name(nullptr));
  // The picker.
  CHECK(shown(kSuperChest, ChestTier::kSuper) && !shown(kChest, ChestTier::kSuper) &&
        !shown(kQuestChest, ChestTier::kSuper) && !shown(kOpenable, ChestTier::kSuper));
  CHECK(shown(kSuperChest, ChestTier::kChests) && shown(kChest, ChestTier::kChests) &&
        shown(kQuestChest, ChestTier::kChests) && !shown(kOpenable, ChestTier::kChests));
  CHECK(shown(kSuperChest, ChestTier::kOpenable) && shown(kChest, ChestTier::kOpenable) &&
        shown(kOpenable, ChestTier::kOpenable) && !shown(kNotOpenable, ChestTier::kOpenable));
  CHECK(!opened(0) && opened(1) && opened(2));
  // A super chest by its unit's sparkle byte (the unique chest a layout places is an ordinary chest class).
  CHECK(with_sparkle(kChest, 1) == kSuperChest && with_sparkle(kOpenable, 1) == kSuperChest);
  CHECK(with_sparkle(kChest, 0) == kChest && with_sparkle(kChest, -1) == kChest && with_sparkle(kChest, 2) == kChest);
  CHECK(with_sparkle(kQuestChest, 1) == kQuestChest && with_sparkle(kNotOpenable, 1) == kNotOpenable);
  CHECK(with_sparkle(kSuperChest, 0) == kSuperChest);  // the class's own init sparkles
  CHECK(sparkle_holds(kChest, 0) && sparkle_holds(kChest, 1) && sparkle_holds(kSuperChest, 1));
  CHECK(!sparkle_holds(kSuperChest, 0) && !sparkle_holds(kChest, 2) && !sparkle_holds(kChest, -1));
  CHECK(sparkle_holds(kNotOpenable, 2) && sparkle_holds(kNotOpenable, -1));  // not looked at
  // What the game keeps as it lets a room go: an object of a class with Restore, in a level whose record keeps its units.
  CHECK(kept_when_let_go(true, true) && !kept_when_let_go(false, true) && !kept_when_let_go(true, false) &&
        !kept_when_let_go(false, false));
  // What it throws away is drawn while the automap's pass handed it over in one of the last two draws, then forgotten.
  CHECK(fleeting_shown(10, 10) && fleeting_shown(9, 10) && fleeting_shown(8, 10) && !fleeting_shown(7, 10));
  CHECK(!fleeting_forgotten(10, 26) && fleeting_forgotten(10, 27) && !fleeting_shown(10, 27));
  CHECK(fleeting_shown(0xFFFFFFFFu, 1u) && !fleeting_forgotten(0xFFFFFFFFu, 1u));  // the count wraps
  // The items.
  CHECK(item_kind(2, true) == kItemRune && item_kind(7, false) == kItemUnique && item_kind(5, false) == kItemSet);
  CHECK(item_kind(4, false) == kItemNone && item_kind(6, false) == kItemNone && item_kind(8, false) == kItemNone);
  CHECK(on_ground(3) && on_ground(5) && !on_ground(0) && !on_ground(4) && !on_ground(1));
  const Tint gold = item_tint(kItemUnique), green = item_tint(kItemSet), orange = item_tint(kItemRune);
  CHECK(std::fabs(gold.r - 199 / 255.0f) < 1e-6f && green.r == 0.0f && orange.r == 1.0f && orange.b == 0.0f);
}

// Where the automap's list draw puts a cell, and its marker draw a cross.
static void places() {
  CellView v;
  v.cx = 1000;
  v.cy = 500;
  v.ox = 640;
  v.oy = 360;
  v.sx = v.sy = 1.0f;
  CellPlace p = cell_place(v, 120, 60, 8, 5, 32);
  CHECK(p.x == 640 + 112 - 1000 && p.y == 360 + 33 - 500);
  // Floored, each product apart: a scale of 0.75 with odd numbers and a negative key.
  v.sx = v.sy = 0.75f;
  p = cell_place(v, 1, -3, 8, 5, 32);
  // x: floor(-7 * 0.75 = -5.25) = -6, floor(1000 * 0.75) = 750; y: floor(-30 * 0.75 = -22.5) = -23, floor(375) = 375.
  CHECK(p.x == 640 - 6 - 750 && p.y == 360 - 23 - 375);
  // A cross 70 x 36 at half scale, centred: 70 / 2 * 0.5 = 17.5 -> 17, 36 / 2 * 0.5 = 9.
  const CellPlace c = centred(100, 100, 70, 36, 0.5f);
  CHECK(c.x == 83 && c.y == 91);
  CHECK(centred(10, 10, 71, 35, 1.0f).x == 10 - 35 && centred(10, 10, 71, 35, 1.0f).y == 10 - 17);
}

// The plugin's icons: each a shape of its own, opaque in its middle and clear in its corners, in its color; and the
// game's sprite file around them (the header of the game's own el_rune.sprite: "SpA1", 31, 98, 98, 98, 0, 1, 0, 0,
// 0x9610, 4).
static void icons() {
  for (const int size : {mapicons::kSize, mapicons::kLowEndSize}) {
    std::vector<std::vector<uint8_t>> painted;
    for (int i = 0; i < mapicons::kIcons; ++i) {
      const std::vector<uint8_t> px = mapicons::paint(static_cast<mapicons::Icon>(i), size);
      CHECK(px.size() == static_cast<size_t>(size) * size * 4);
      const auto pixel = [&](int x, int y) { return px.data() + (static_cast<size_t>(y) * size + x) * 4; };
      CHECK(pixel(0, 0)[3] == 0 && pixel(size - 1, 0)[3] == 0 && pixel(0, size - 1)[3] == 0 &&
            pixel(size - 1, size - 1)[3] == 0);
      const uint8_t* mid = pixel(size / 2, size / 2 + 1);
      CHECK(mid[3] == 255);
      if (i == mapicons::kUnique) CHECK(mid[0] > 180 && mid[1] > 140 && mid[2] < mid[1]);  // gold
      if (i == mapicons::kSet) CHECK(mid[1] > 150 && mid[0] < mid[1] && mid[2] < mid[1]);  // green
      if (i == mapicons::kRune) {
        const uint8_t* stone = pixel(size * 62 / 100, size * 75 / 100);  // the stone, away from its glyph
        CHECK(stone[3] == 255 && stone[0] > 180 && stone[1] > 90 && stone[2] < 60);  // orange
      }
      if (i == mapicons::kEnemy || i == mapicons::kNamedEnemy) CHECK(mid[0] > 150 && mid[1] < 80 && mid[2] < 80);  // red
      if (i == mapicons::kNamedEnemy) {
        // The orange ring, between the red disc and the dark rim.
        const uint8_t* ring = pixel(size / 2 + static_cast<int>(size * 0.385f), size / 2);
        CHECK(ring[3] == 255 && ring[0] > 200 && ring[1] > 120 && ring[2] < 70);
      }
      int opaque = 0;
      for (size_t k = 3; k < px.size(); k += 4) opaque += px[k] > 128 ? 1 : 0;
      CHECK(opaque > size * size / 5 && opaque < size * size * 4 / 5);
      painted.push_back(px);
    }
    for (size_t a = 0; a < painted.size(); ++a)
      for (size_t b = a + 1; b < painted.size(); ++b) CHECK(painted[a] != painted[b]);  // a shape each
    // The two dots have the same red disc, drawn at one scale: along a radius the plain one turns dark where the named
    // one's disc ends too, and the named one alone goes on in orange beyond it.
    const std::vector<uint8_t>& plain = painted[mapicons::kEnemy];
    const std::vector<uint8_t>& named = painted[mapicons::kNamedEnemy];
    const auto red = [&](const std::vector<uint8_t>& p, int x) {
      const uint8_t* q = p.data() + (static_cast<size_t>(size / 2) * size + x) * 4;
      return q[3] == 255 && q[0] > 150 && q[1] < 100;
    };
    int plain_red = 0, named_red = 0;
    for (int x = size / 2; x < size && red(plain, x); ++x) ++plain_red;
    for (int x = size / 2; x < size && red(named, x); ++x) ++named_red;
    CHECK(plain_red > size / 5 && plain_red == named_red);
    int plain_opaque = 0, named_opaque = 0;
    for (size_t k = 3; k < plain.size(); k += 4) {
      plain_opaque += plain[k] > 128 ? 1 : 0;
      named_opaque += named[k] > 128 ? 1 : 0;
    }
    CHECK(named_opaque > plain_opaque * 3 / 2);  // the ring makes it the larger
  }
  const std::vector<uint8_t> px = mapicons::paint(mapicons::kRune, 98);
  const std::vector<uint8_t> f = mapicons::sprite_file(px.data(), 98, 98);
  CHECK(f.size() == 0x28 + 98 * 98 * 4);
  CHECK(std::memcmp(f.data(), "SpA1", 4) == 0 && at<uint16_t>(f, 4) == 31 && at<uint16_t>(f, 6) == 98);
  CHECK(at<uint32_t>(f, 8) == 98 && at<uint32_t>(f, 0x0C) == 98 && at<uint32_t>(f, 0x10) == 0 &&
        at<uint32_t>(f, 0x14) == 1 && at<uint32_t>(f, 0x18) == 0 && at<uint32_t>(f, 0x1C) == 0 &&
        at<uint32_t>(f, 0x20) == 0x9610 && at<uint32_t>(f, 0x24) == 4);
  CHECK(std::memcmp(f.data() + 0x28, px.data(), px.size()) == 0);
  CHECK(mapicons::sprite_file(nullptr, 4, 4).empty() && mapicons::sprite_file(px.data(), 0, 4).empty());
}

// The rules over the game's own rows.
static void tables(const std::string& excel) {
  std::vector<uint8_t> objects;
  uint32_t n = 0;
  if (!rows_of(excel + "/objects.bin", kObjectsRow, &objects, &n)) {
    std::printf("objects.bin not read - the chest rule was not checked over the game's rows\n");
    return;
  }
  int count[5] = {};
  std::vector<int> supers, quests;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* r = objects.data() + static_cast<size_t>(i) * kObjectsRow;
    char name[kObjectNameLen + 1] = {};
    std::memcpy(name, r + kObjectName, kObjectNameLen);
    int32_t cell = 0;
    std::memcpy(&cell, r + 0x164, sizeof(cell));
    const ObjectKind k = object_kind(r[kObjectOperate], r[kObjectInit], r[kObjectSelectable] != 0, cell, name);
    ++count[k];
    if (k == kSuperChest) supers.push_back(static_cast<int>(i));
    if (k == kQuestChest) {
      quests.push_back(static_cast<int>(i));
      CHECK(cell == 318);  // the sparkle chest
    }
  }
  std::printf("objects: %u rows - %d super chests, %d quest chests, %d other chests, %d other objects that open\n", n,
              count[kSuperChest], count[kQuestChest], count[kChest], count[kOpenable]);
  CHECK(count[kSuperChest] == 2 && count[kQuestChest] == 6 && count[kChest] == 46 && count[kOpenable] == 126);
  CHECK(supers.size() == 2 && supers[0] == 397 && supers[1] == 455);  // ConsolationChest, SpecialChest100
  CHECK(quests.size() == 6 && quests[0] == 354 && quests[5] == 407);
  // The stash's icon, the coin chest (class 267).
  int32_t stash = 0;
  std::memcpy(&stash, objects.data() + 267u * kObjectsRow + 0x164, sizeof(stash));
  CHECK(stash == 319);

  // A layout's unique chest (PlaceUniqueChest, class 580; PlaceRandomTreasureChest 581 beside it) is made, as its room is
  // filled, into a chest of its act's list (0x50FB20, read from the dump: Act 2 the tomb chests, the Arcane Sanctuary
  // (level 74) its four, Act 3 JungleChest and JungleChest2, Travincal (83) its four, Acts 1, 4 and 5 thirteen) and
  // given the sparkle; each of those must be a chest the switch shows, which the byte makes a super chest.
  auto class_name = [&](uint32_t c) {
    char s[kObjectNameLen + 1] = {};
    if (c < n) std::memcpy(s, objects.data() + static_cast<size_t>(c) * kObjectsRow + kObjectName, kObjectNameLen);
    return std::string(s);
  };
  auto kind_of = [&](uint32_t c) {
    const uint8_t* r = objects.data() + static_cast<size_t>(c) * kObjectsRow;
    char name[kObjectNameLen + 1] = {};
    std::memcpy(name, r + kObjectName, kObjectNameLen);
    int32_t cell = 0;
    std::memcpy(&cell, r + 0x164, sizeof(cell));
    return object_kind(r[kObjectOperate], r[kObjectInit], r[kObjectSelectable] != 0, cell, name);
  };
  // What opens for loot without the routines that call the object drop themselves: Act 3's stashes (OperateFn 51: the
  // drop by the object's event 10; rolled by the object groups of 18 Act 3 areas and placed by the Kurast layouts), the
  // bookshelves of Act 1's Barracks and Jail (26: a tome or a scroll) and Wirt's body (33); and what does not: the
  // quest objects that hand over their quest's own item, the exploding barrel and chest, the Countess's chest (not
  // selectable: her death opens it).
  struct Named {
    uint32_t cls;
    const char* name;
    ObjectKind kind;
  };
  const Named named[] = {{185, "JunglePillar0", kOpenable}, {186, "JunglePillar1", kOpenable},
                         {187, "JunglePillar2", kOpenable}, {188, "JunglePillar3", kOpenable},
                         {179, "Bookshelf1", kOpenable},    {180, "Bookshelf2", kOpenable},
                         {268, "Wirt", kOpenable},          {104, "ArmorStand1", kOpenable},
                         {106, "WeaponRack1", kOpenable},   {30, "InifussTree", kNotOpenable},
                         {108, "Malus", kNotOpenable},      {193, "LamEsensTome", kNotOpenable},
                         {11, "BarrelExploding", kNotOpenable}, {454, "ExplodingChest100", kNotOpenable},
                         {371, "ForgottenTowerChest", kNotOpenable}};
  for (const Named& c : named) {
    const bool ok = class_name(c.cls) == c.name && kind_of(c.cls) == c.kind;
    if (!ok) std::printf("  object class %u (%s, wanted %s) is kind %d, not %d\n", c.cls, class_name(c.cls).c_str(), c.name,
                         static_cast<int>(kind_of(c.cls)), static_cast<int>(c.kind));
    CHECK(ok);
  }
  CHECK(class_name(580) == "PlaceUniqueChest" && class_name(581) == "PlaceRandomTreasureChest");
  CHECK(kind_of(580) == kNotOpenable && kind_of(581) == kNotOpenable);  // placeholders: never an object of their own
  const uint32_t made[] = {87,  88,  387, 389, 390, 391, 181, 183, 329, 330, 331, 332, 5,
                           6,   139, 140, 141, 144, 176, 177, 198, 240, 241, 242, 243};
  int upgraded = 0;
  for (const uint32_t c : made) {
    const bool ok = c < n && kind_of(c) == kChest && with_sparkle(kind_of(c), 1) == kSuperChest;
    if (!ok) std::printf("  the unique chest's class %u (%s) is not a chest the byte makes super\n", c, class_name(c).c_str());
    upgraded += ok ? 1 : 0;
  }
  std::printf("unique chests: %d of the %zu classes a layout's unique chest is made into are chests a sparkle makes "
              "super chests (Act 3's %s and %s among them)\n",
              upgraded, sizeof(made) / sizeof(made[0]), class_name(181).c_str(), class_name(183).c_str());
  CHECK(upgraded == static_cast<int>(sizeof(made) / sizeof(made[0])));

  // What the game throws away as it lets a room go (the room unloader's unit save, 0x504260: the Objects row's Restore,
  // +0x131, then RestoreVirgins, +0x132). Of what opens for loot only Barrel3 (Act 5's "barrel wilderness", rolled by
  // the object groups 103 and 105 of the Bloody Foothills, the Frigid Highlands and the Arreat Plateau) has none; the
  // exploding chest beside it in those groups and the town portal have none either, every chest has it.
  auto restores = [&](uint32_t c) { return objects[static_cast<size_t>(c) * kObjectsRow + kObjectRestore] != 0; };
  std::vector<uint32_t> fleeting;
  for (uint32_t i = 0; i < n; ++i)
    if (kind_of(i) != kNotOpenable && !restores(i)) fleeting.push_back(i);
  std::printf("restore: %zu classes that open for loot are thrown away as their room is let go (the first %s)\n",
              fleeting.size(), fleeting.empty() ? "none" : class_name(fleeting[0]).c_str());
  CHECK(fleeting.size() == 1 && fleeting[0] == 419 && class_name(419) == "Barrel3");
  CHECK(class_name(454) == "ExplodingChest100" && !restores(454) && class_name(59) == "TownPortal" && !restores(59));
  CHECK(restores(5) && restores(418) && restores(181) && restores(397) && restores(455));  // chests, Barrel2
  CHECK(objects[419u * kObjectsRow + kObjectRestore + 1] == 1);  // Barrel3's RestoreVirgins, the next byte
  // The levels whose record keeps no units (LevelDefs SaveMonsters, +0x94, which 0x327830 reads): the null level, the
  // five towns and Harem Level 1, the one of them the fill reads (nothing there opens for loot).
  std::vector<uint8_t> defs;
  uint32_t dn = 0;
  if (!rows_of(excel + "/leveldefs.bin", kLevelDefsRow, &defs, &dn)) {
    std::printf("leveldefs.bin not read - the levels' rule was not checked\n");
  } else {
    std::vector<uint32_t> none;
    for (uint32_t i = 0; i < dn; ++i)
      if (at<int32_t>(defs, static_cast<size_t>(i) * kLevelDefsRow + kLevelDefsKeepsUnits) == 0) none.push_back(i);
    std::printf("levels: %u LevelDefs rows, %zu keep no units as their rooms are let go\n", dn, none.size());
    CHECK(dn == 138 && none == (std::vector<uint32_t>{0, 1, 40, 50, 75, 103, 109}));
    CHECK(at<int32_t>(defs, 110u * kLevelDefsRow + 0x30) == 3);  // the Bloody Foothills' DRLG type: outdoor
  }

  // The runes: the misc rows of the rune item type (its ItemTypes row by its code).
  std::vector<uint8_t> types, misc;
  uint32_t tn = 0, mn = 0;
  if (!rows_of(excel + "/itemtypes.bin", 0xE8, &types, &tn) || !rows_of(excel + "/misc.bin", 0x1C0, &misc, &mn)) {
    std::printf("itemtypes.bin or misc.bin not read - the runes were not checked\n");
    return;
  }
  int rune = -1;
  for (uint32_t i = 0; i < tn; ++i)
    if (std::memcmp(types.data() + static_cast<size_t>(i) * 0xE8, "rune", 4) == 0) rune = static_cast<int>(i);
  int runes = 0;
  for (uint32_t i = 0; i < mn; ++i) runes += at<int16_t>(misc, static_cast<size_t>(i) * 0x1C0 + 0x12E) == rune ? 1 : 0;
  std::printf("items: the rune type is ItemTypes row %d, %d misc rows of it\n", rune, runes);
  CHECK(rune > 0 && runes == 33);
}

int main() {
  rules();
  places();
  icons();
  if (const char* excel = std::getenv("D2RCC_EXCEL")) tables(excel);
  else std::printf("D2RCC_EXCEL not set - the rules were not checked over the game's rows\n");
  if (g_failures) {
    std::printf("test_maploot: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_maploot: all passed\n");
  return 0;
}
