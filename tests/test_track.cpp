// What is tracked (src/track.h, a local addition): the rules - codes, names, the settings text - and the runes-only
// build's settings file read and written back (src/config.cpp built with D2RCC_RUNES_ONLY).
//
// Built by hand:
//   g++ -std=c++20 -O2 -DD2RCC_RUNES_ONLY=1 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Isrc
//       -Icontrib/D2RLoader-PluginSDK/include -static -o tests/build/test_track.exe
//       tests/test_track.cpp src/track.cpp src/config.cpp src/named.cpp
//   test_track.exe [res/rune-marker.toml]
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/config.h"
#include "../src/track.h"

using namespace d2rcc;

namespace {
int g_failed = 0;
void check(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++g_failed;
}
uint32_t code4(const char* s) {  // as an Items row has it: spaces after a shorter code
  uint32_t c = 0;
  for (int i = 0; i < 4; ++i) c |= static_cast<uint32_t>(static_cast<uint8_t>(s[i] ? s[i] : ' ')) << (8 * i);
  return c;
}
}  // namespace

int main(int argc, char** argv) {
  std::printf("1. item codes\n");
  check(track::rune_number(code4("r01")) == 1 && track::rune_number(code4("r30")) == 30 &&
            track::rune_number(code4("r33")) == 33,
        "r01, r30 and r33 are runes 1, 30 and 33");
  check(track::rune_number(code4("r00")) == 0 && track::rune_number(code4("r34")) == 0 &&
            track::rune_number(code4("rin")) == 0 && track::rune_number(code4("r3")) == 0,
        "r00, r34, rin (a ring) and r3 are none");
  check(track::rune_number(track::make_code("r07")) == 7, "make_code gives a row's code");
  check(track::special_index(code4("pk1")) == 0 && track::kSpecials[track::special_index(code4("pk3"))].group == track::kKeys,
        "pk1 .. pk3 are the keys");
  check(track::kSpecials[track::special_index(code4("dhn"))].group == track::kOrgans &&
            track::kSpecials[track::special_index(code4("bey"))].group == track::kOrgans &&
            track::kSpecials[track::special_index(code4("mbr"))].group == track::kOrgans,
        "dhn, bey and mbr are the organs");
  check(track::kSpecials[track::special_index(code4("ceh"))].group == track::kEssences &&
            track::kSpecials[track::special_index(code4("fed"))].group == track::kEssences,
        "ceh and fed are essences");
  check(track::kSpecials[track::special_index(code4("xa1"))].group == track::kShards &&
            track::kSpecials[track::special_index(code4("xa5"))].group == track::kShards,
        "xa1 .. xa5 are the Worldstone Shards");
  check(track::kSpecials[track::special_index(code4("ua1"))].group == track::kStatues &&
            track::kSpecials[track::special_index(code4("ua5"))].group == track::kStatues,
        "ua1 .. ua5 are the statues");
  check(track::kSpecials[track::special_index(code4("um6"))].group == track::kFragments, "um1 .. um6 are the fragments");
  check(track::special_index(code4("key")) < 0 && track::special_index(code4("toa")) < 0 &&
            track::special_index(code4("std")) < 0 && track::special_index(code4("pk1x")) < 0 &&
            track::special_index(code4("r30")) < 0,
        "a plain key, the token, the standard, a four-letter code and a rune are none of them");
  int per_group[track::kGroups] = {};
  for (const track::Special& s : track::kSpecials) ++per_group[s.group];
  check(per_group[track::kKeys] == 3 && per_group[track::kOrgans] == 3 && per_group[track::kEssences] == 4 &&
            per_group[track::kShards] == 5 && per_group[track::kStatues] == 5 && per_group[track::kFragments] == 6 &&
            track::kSpecialCount == 26,
        "3 keys, 3 organs, 4 essences, 5 shards, 5 statues, 6 fragments: 26 in all");

  std::printf("2. the settings text of the runes\n");
  uint64_t m = 0;
  check(track::runes_from_text("all", &m) && m == track::kAllRunes, "\"all\"");
  check(track::runes_from_text("none", &m) && m == 0, "\"none\"");
  check(track::runes_from_text("", &m) && m == 0, "nothing at all is none");
  check(track::runes_from_text("Ber Jah", &m) && m == ((1ull << 29) | (1ull << 30)), "\"Ber Jah\"");
  check(track::runes_from_text("  zod,EL ;  shael ", &m) && m == ((1ull << 32) | 1ull | (1ull << 12)),
        "any case, commas and spaces");
  m = 123;
  check(!track::runes_from_text("Ber Bur", &m) && m == 123, "a word that is no rune's name: refused, the mask left alone");
  check(!track::runes_from_text("all Ber", &m) && !track::runes_from_text("none all", &m), "\"all\" with anything else: refused");
  check(track::runes_to_text(track::kAllRunes) == "all" && track::runes_to_text(0) == "none", "all and none are written so");
  check(track::runes_to_text((1ull << 20) | (1ull << 32) | (1ull << 23)) == "Pul Ist Zod", "a few are written lowest first");
  bool round = true;
  for (uint64_t mask : {0x1ull, 0x155555555ull, 0x1FFF00000ull, track::kAllRunes >> 1, 0x100000000ull}) {
    uint64_t back = ~0ull;
    round = round && track::runes_from_text(track::runes_to_text(mask), &back) && back == mask;
  }
  check(round, "what is written reads back the same");
  check(track::runes_from(1) == track::kAllRunes && track::runes_from(34) == 0 &&
            track::runes_to_text(track::runes_from(31)) == "Jah Cham Zod" &&
            track::runes_to_text(track::runes_from(21)).rfind("Pul Um Mal", 0) == 0,
        "\"from a rune up\"");

  std::printf("3. the choices\n");
  track::init(track::kAllRunes, track::kDefaultGroups);
  check(track::rune(1) && track::rune(33) && track::rune(0) && track::rune(40), "all runes on; a number that is no stock rune counts as tracked");
  check(track::group(track::kKeys) && track::group(track::kStatues) && !track::group(track::kFragments),
        "the default groups: all but the fragments");
  track::set_rune(1, false);
  track::set_rune(30, false);
  track::set_rune(30, true);
  check(!track::rune(1) && track::rune(2) && track::rune(30) && track::runes() == (track::kAllRunes & ~1ull), "one rune off");
  track::set_runes(track::runes_from(24));
  check(!track::rune(23) && track::rune(24) && track::rune(33), "\"Ist and every higher rune\"");
  track::set_group(track::kFragments, true);
  track::set_group(track::kKeys, false);
  check(track::group(track::kFragments) && !track::group(track::kKeys) && !track::group(-1) && !track::group(99),
        "groups on and off; a number that is no group is not tracked");
  check(track::icons(), "the icons are on unless said otherwise");
  track::set_icons(false);
  check(!track::icons() && track::rune(33), "the icons off: what is tracked stays tracked");
  track::init(track::kAllRunes, track::kDefaultGroups, false);
  check(!track::icons(), "... and off from the settings");
  track::set_icons(true);
  // The light pillars: a choice of their own, beside what is tracked.
  check(track::pillar_runes() == track::runes_from(26) && track::pillar_groups() == 0 && track::any_pillars(),
        "a light pillar on Vex and every higher rune and on no group, unless said otherwise");
  check(!track::pillar_rune(25) && track::pillar_rune(26) && track::pillar_rune(33) && !track::pillar_rune(0) &&
            !track::pillar_rune(40),
        "... Gul has none, Vex and Zod have one; a number that is no stock rune has none");
  track::set_runes(0);
  check(track::pillar_rune(30) && !track::rune(30), "a rune can have its pillar without being tracked");
  track::set_pillar_runes(track::runes_from(30) | 1ull);
  track::set_pillar_group(track::kKeys, true);
  track::set_group(track::kKeys, false);
  check(track::pillar_rune(1) && !track::pillar_rune(29) && track::pillar_rune(30) && track::pillar_group(track::kKeys) &&
            !track::pillar_group(track::kOrgans) && !track::pillar_group(-1) && !track::pillar_group(99) &&
            !track::group(track::kKeys),
        "pillars set by rune and by group, apart from what is tracked");
  track::set_pillar_runes(0);
  track::set_pillar_group(track::kKeys, false);
  check(!track::any_pillars(), "none chosen: no pillars at all");
  track::init_pillars(track::kDefaultPillarRunes, track::kDefaultPillarGroups);
  track::init(track::kAllRunes, track::kDefaultGroups);

  std::printf("4. the settings file\n");
  config::Settings s;
  check(config::parse("", &s) == 0 && s.track_runes == track::kAllRunes && s.track_groups == track::kDefaultGroups &&
            s.toggle_key == 0x76 && s.map_runes && s.map_icons && s.rune_message == 1 &&
            s.pillar_runes == track::kDefaultPillarRunes && s.pillar_groups == track::kDefaultPillarGroups,
        "an empty file: everything tracked but the fragments, shown as an icon and a chat line, the menu on F7, a "
        "light pillar on Vex and higher");
  check(config::parse("[rune-marker]\npillar_runes = \"Ber Jah\"\npillar_keys = true\npillar_statues = true\n", &s) == 3 &&
            s.pillar_runes == ((1ull << 29) | (1ull << 30)) &&
            s.pillar_groups == ((1u << track::kKeys) | (1u << track::kStatues)) && s.track_runes == track::kAllRunes,
        "the pillars' keys: the runes by name, a switch a group; what is tracked is not touched");
  check(config::parse("[rune-marker]\npillar_runes = \"none\"\n", &s) == 1 && s.pillar_runes == 0 &&
            config::parse("[rune-marker]\npillar_runes = \"Ber Bur\"\npillar_keys = perhaps\n", &s) == 0 &&
            s.pillar_runes == track::kDefaultPillarRunes && s.pillar_groups == 0,
        "\"none\"; and values that make no sense keep the defaults");
  const char* hand = "[rune-marker]\nrunes = \"Vex Ohm Lo Sur Ber Jah Cham Zod\"\nkeys = false\nfragments = true\n"
                     "menu_key = \"F9\"\nrune_message = \"screen\"\n";
  check(config::parse(hand, &s) == 5 && s.track_runes == track::runes_from(26) &&
            !((s.track_groups >> track::kKeys) & 1) && ((s.track_groups >> track::kFragments) & 1) &&
            ((s.track_groups >> track::kOrgans) & 1) && s.toggle_key == 0x78 && s.rune_message == 2,
        "a file written by hand");
  check(config::parse("[rune-marker]\nrune_message = \"both\"\nmap_icons = false\n", &s) == 2 && s.rune_message == 3 &&
            !s.map_icons,
        "\"both\" (the chat line and the pop-up), and the icons off");
  check(config::parse("[rune-marker]\nrune_message = \"off\"\n", &s) == 1 && s.rune_message == 0 && s.map_icons,
        "\"off\": no line, the icon as it was");
  check(config::parse("[rune-marker]\nrunes = \"Ber Bur\"\nkeys = maybe\n", &s) == 0 && s.track_runes == track::kAllRunes &&
            s.track_groups == track::kDefaultGroups,
        "values that make no sense keep the defaults");

  if (argc > 1) {
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string shipped = ss.str();
    config::Settings d;
    const int keys = config::parse(shipped, &d);
    std::printf("    %s: %d keys\n", argv[1], keys);
    check(keys == 26, "the shipped file has the 26 keys, all understood");
    check(d.enabled && d.map_runes && d.track_runes == track::kAllRunes && d.track_groups == track::kDefaultGroups &&
              d.map_icons && d.rune_message == 1 && d.greeting && d.toggle_key == 0x76 && d.logging &&
              d.pillar_runes == track::kDefaultPillarRunes && d.pillar_groups == track::kDefaultPillarGroups,
          "... and says what the defaults are");
    check(config::rewrite(shipped, d) == shipped, "written back unchanged, it is the same file byte for byte");
    // The menu's choices written into it: only those lines change.
    config::Settings c = d;
    c.track_runes = track::runes_from(21);
    c.track_groups = (c.track_groups | (1u << track::kFragments)) & ~(1u << track::kEssences);
    c.rune_message = 3;
    c.map_icons = false;
    c.pillar_runes = track::runes_from(30);
    c.pillar_groups = 1u << track::kOrgans;
    const std::string after = config::rewrite(shipped, c);
    config::Settings back;
    config::parse(after, &back);
    check(back.track_runes == c.track_runes && back.track_groups == c.track_groups && back.rune_message == 3 &&
              !back.map_icons && back.toggle_key == d.toggle_key && back.greeting == d.greeting &&
              back.pillar_runes == c.pillar_runes && back.pillar_groups == c.pillar_groups,
          "the menu's choices are read back from the rewritten file");
    check(after.find("runes = \"Pul Um Mal Ist Gul Vex Ohm Lo Sur Ber Jah Cham Zod\"") != std::string::npos &&
              after.find("essences = false             # Twisted") != std::string::npos &&
              after.find("fragments = true           # Defender's Fire") != std::string::npos &&
              after.find("\nmap_icons = false\n") != std::string::npos &&
              after.find("rune_message = \"both\"") != std::string::npos &&
              after.find("\npillar_runes = \"Ber Jah Cham Zod\"\n") != std::string::npos &&
              after.find("\npillar_organs = true\n") != std::string::npos &&
              after.find("\npillar_keys = false\n") != std::string::npos,
          "the changed lines read as expected, their comments kept");
    int changed = 0;
    {
      std::istringstream a(shipped), b(after);
      std::string la, lb;
      while (std::getline(a, la) && std::getline(b, lb)) changed += la != lb;
    }
    check(changed == 7, "exactly seven lines differ from the shipped file");
    // Each of the four ways a line can be said, written and read back.
    bool modes = true;
    for (int mode = 0; mode <= 3; ++mode) {
      config::Settings m = d, r;
      m.rune_message = mode;
      config::parse(config::rewrite(shipped, m), &r);
      modes = modes && r.rune_message == mode;
    }
    check(modes, "\"off\", \"chat\", \"screen\" and \"both\" each come back as written");
    // A file from before the menu existed (it lacks the new keys): they are added at the table's end.
    const std::string old_file = "# mine\n[rune-marker]\nenabled = true\nmap_runes = true\nrune_message = \"chat\"\n"
                                 "greeting = true\nlogging = true\nlog_level = \"info\"\n";
    const std::string grown = config::rewrite(old_file, c);
    config::Settings g;
    config::parse(grown, &g);
    check(grown.rfind("# mine\n[rune-marker]\nenabled = true\n", 0) == 0 && g.track_runes == c.track_runes &&
              g.track_groups == c.track_groups && g.rune_message == 3 && !g.map_icons &&
              grown.find("menu_key = \"F7\"") != std::string::npos &&
              grown.find("map_icons = false   # an icon on the minimap") != std::string::npos &&
              g.pillar_runes == c.pillar_runes && g.pillar_groups == c.pillar_groups &&
              grown.find("pillar_runes = \"Ber Jah Cham Zod\"   # the runes with a pillar of light") != std::string::npos &&
              grown.find("pillar_organs = true   # a pillar of light on this group") != std::string::npos,
          "an older file gains the new keys, each with a word on what it is, and keeps what it had");
    check(config::rewrite(grown, g) == grown, "... and is then left as it is");
    // The unique and set items: none in the shipped file; a choice written into it and read back.
    check(!d.track_named[named::kUnique].any() && !d.track_named[named::kSetItem].any() &&
              !d.pillar_named[named::kUnique].any() && !d.pillar_named[named::kSetItem].any(),
          "the shipped file tracks no unique or set item and gives none a pillar");
    config::Settings n = d;
    n.track_named[named::kUnique] = named::from_text(named::kUnique, "harlequin crest; Griffon's Eye;The Stone of Jordan");
    n.pillar_named[named::kUnique] = named::from_text(named::kUnique, "Griffon's Eye");
    n.track_named[named::kSetItem] = named::all(named::kSetItem);
    const std::string named_file = config::rewrite(shipped, n);
    config::Settings nb;
    config::parse(named_file, &nb);
    check(nb.track_named[named::kUnique] == n.track_named[named::kUnique] && nb.track_named[named::kUnique].count() == 3 &&
              nb.pillar_named[named::kUnique].count() == 1 && nb.track_named[named::kSetItem] == named::all(named::kSetItem) &&
              !nb.pillar_named[named::kSetItem].any(),
          "a choice of unique and set items is read back from the rewritten file");
    check(named_file.find("\nunique_items = \"The Stone of Jordan; Harlequin Crest; Griffon's Eye\"\n") != std::string::npos &&
              named_file.find("\npillar_unique_items = \"Griffon's Eye\"\n") != std::string::npos &&
              named_file.find("\nset_items = \"all\"\n") != std::string::npos &&
              named_file.find("\npillar_set_items = \"none\"\n") != std::string::npos,
          "... written as names, \"all\" and \"none\"");
    const std::string named_grown = config::rewrite(grown, n);
    config::Settings ng;
    config::parse(named_grown, &ng);
    check(ng.track_named[named::kUnique] == n.track_named[named::kUnique] &&
              named_grown.find("unique_items = \"The Stone of Jordan; Harlequin Crest; Griffon's Eye\"   # the tracked unique items") !=
                  std::string::npos,
          "a file from before this version gains the four keys");
  }

  std::printf("7. the unique and set items (named.h)\n");
  {
    using namespace named;
    check(entries(kUnique) == 396 && entries(kSetItem) == 140 && groups(kUnique) == 20 && groups(kSetItem) == 35,
          "396 unique items by name (403 rows: the eight Rainbow Facets are one) and 140 set items in 35 sets");
    const auto by_name = [](Kind kind, const char* english) {
      for (int i = 0; i < entries(kind); ++i)
        if (std::strcmp(entry(kind, i).item->english, english) == 0) return i;
      return -1;
    };
    const int shako = by_name(kUnique, "Harlequin Crest"), eye = by_name(kUnique, "Griffon's Eye");
    const int facet = by_name(kUnique, "Rainbow Facet"), guard = by_name(kSetItem, "Tal Rasha's Guardianship");
    check(shako >= 0 && eye >= 0 && facet >= 0 && guard >= 0, "the items asked for below are in the lists");
    // An item on the ground: its file index and its base item's code as an Items row has it.
    check(entry_of(kUnique, 248, track::make_code("uap")) == shako, "file index 248 on a Shako is Harlequin Crest");
    check(entry_of(kUnique, 248, track::make_code("cap")) == -1, "... on another base item it is not (a mod's table)");
    check(entry_of(kUnique, 336, track::make_code("ci3")) == eye && entry_of(kUnique, 392, track::make_code("jew")) == facet &&
              entry_of(kUnique, 399, track::make_code("jew")) == facet,
          "Griffon's Eye, and two of the Rainbow Facets as the one entry");
    check(entry_of(kUnique, 200, track::make_code("xap")) == -1 && entry_of(kUnique, -1, 0) == -1 &&
              entry_of(kUnique, 5000, 0) == -1 && entry_of(kUnique, 295) == -1,
          "a row that is no item (a heading, a number outside the table, an empty row) is none");
    check(entry_of(kSetItem, 79, track::make_code("uth")) == guard && entry(kSetItem, guard).group >= 0 &&
              std::strcmp(group_english(kSetItem, entry(kSetItem, guard).group), "Tal Rasha's Wrappings") == 0,
          "set item 79 on a Lacquered Plate is Tal Rasha's Guardianship, of Tal Rasha's Wrappings");
    check(std::strcmp(group_english(kUnique, entry(kUnique, shako).group), "Helms") == 0 &&
              std::strcmp(group_english(kUnique, entry(kUnique, facet).group), "Jewels") == 0,
          "a unique item's kind is its base item's");
    // The settings text.
    Mask m;
    check(to_text(kUnique, m) == "none" && to_text(kUnique, all(kUnique)) == "all", "\"none\" and \"all\"");
    m.set(eye, true);
    m.set(shako, true);
    check(to_text(kUnique, m) == "Harlequin Crest; Griffon's Eye", "names apart by semicolons, in the list's order");
    int unknown = 0;
    check(from_text(kUnique, "  GRIFFON'S EYE ;harlequin crest;  ", &unknown) == m && unknown == 0, "read back in any case and spacing");
    check(from_text(kUnique, "Griffon's Eye; No Such Thing; Harlequin Crest", &unknown) == m && unknown == 1,
          "a name the list does not have is left out and counted");
    check(from_text(kUnique, "all", &unknown) == all(kUnique) && !from_text(kUnique, "none").any() && !from_text(kUnique, "").any(),
          "\"all\", \"none\" and nothing");
    check(from_text(kUnique, "Mindrend") == from_text(kUnique, "Skull Splitter") && from_text(kUnique, "Mindrend").count() == 1,
          "an item is also found by the game's own key of its name");
    // The names shown: English until the game's are asked for.
    check(std::strcmp(display(kUnique, shako), "Harlequin Crest") == 0 && std::strcmp(group_display(kSetItem, 0), "Civerb's Vestments") == 0,
          "English names before the game's strings are asked");
    localize([](const char* key, char* out, size_t cap) {
      if (std::strcmp(key, "Harlequin Crest") == 0) return std::snprintf(out, cap, "Harlekinskrone") > 0;
      if (std::strcmp(key, "Civerb's Vestments") == 0) return std::snprintf(out, cap, "Civerbs Gewand") > 0;
      return false;
    });
    check(std::strcmp(display(kUnique, shako), "Harlekinskrone") == 0 && std::strcmp(display(kUnique, eye), "Griffon's Eye") == 0 &&
              std::strcmp(group_display(kSetItem, 0), "Civerbs Gewand") == 0,
          "the game's names where it has them, English where it has none");
    check(to_text(kUnique, m) == "Harlequin Crest; Griffon's Eye", "the settings text stays English");
    // The choices themselves.
    track::init_named(kUnique, m, Mask{});
    check(track::named(kUnique, shako) && track::named(kUnique, eye) && !track::named(kUnique, facet) && track::any_named() &&
              !track::named_pillar(kUnique, shako),
          "tracked: the two chosen");
    Mask lit;
    lit.set(guard, true);
    track::set_named_pillars(kSetItem, lit);
    check(track::named_pillar(kSetItem, guard) && !track::named(kSetItem, guard) && track::any_pillars(), "a pillar without tracking");
    track::init_named(kUnique, Mask{}, Mask{});
    track::init_named(kSetItem, Mask{}, Mask{});
    check(!track::any_named(), "none again");
  }

  std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "all passed", g_failed);
  return g_failed ? 1 : 0;
}
