// What is tracked (src/track.h, a local addition): the rules - codes, names, the settings text - and the runes-only
// build's settings file read and written back (src/config.cpp built with D2RCC_RUNES_ONLY).
//
// Built by hand:
//   g++ -std=c++20 -O2 -DD2RCC_RUNES_ONLY=1 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Isrc
//       -Icontrib/D2RLoader-PluginSDK/include -static -o tests/build/test_track.exe
//       tests/test_track.cpp src/track.cpp src/config.cpp
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

  std::printf("4. the settings file\n");
  config::Settings s;
  check(config::parse("", &s) == 0 && s.track_runes == track::kAllRunes && s.track_groups == track::kDefaultGroups &&
            s.toggle_key == 0x76 && s.map_runes && s.map_icons && s.rune_message == 1,
        "an empty file: everything tracked but the fragments, shown as an icon and a chat line, the menu on F7");
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
    check(keys == 15, "the shipped file has the 15 keys, all understood");
    check(d.enabled && d.map_runes && d.track_runes == track::kAllRunes && d.track_groups == track::kDefaultGroups &&
              d.map_icons && d.rune_message == 1 && d.greeting && d.toggle_key == 0x76 && d.logging,
          "... and says what the defaults are");
    check(config::rewrite(shipped, d) == shipped, "written back unchanged, it is the same file byte for byte");
    // The menu's choices written into it: only those lines change.
    config::Settings c = d;
    c.track_runes = track::runes_from(21);
    c.track_groups = (c.track_groups | (1u << track::kFragments)) & ~(1u << track::kEssences);
    c.rune_message = 3;
    c.map_icons = false;
    const std::string after = config::rewrite(shipped, c);
    config::Settings back;
    config::parse(after, &back);
    check(back.track_runes == c.track_runes && back.track_groups == c.track_groups && back.rune_message == 3 &&
              !back.map_icons && back.toggle_key == d.toggle_key && back.greeting == d.greeting,
          "the menu's choices are read back from the rewritten file");
    check(after.find("runes = \"Pul Um Mal Ist Gul Vex Ohm Lo Sur Ber Jah Cham Zod\"") != std::string::npos &&
              after.find("essences = false             # Twisted") != std::string::npos &&
              after.find("fragments = true           # Defender's Fire") != std::string::npos &&
              after.find("\nmap_icons = false\n") != std::string::npos &&
              after.find("rune_message = \"both\"") != std::string::npos,
          "the changed lines read as expected, their comments kept");
    int changed = 0;
    {
      std::istringstream a(shipped), b(after);
      std::string la, lb;
      while (std::getline(a, la) && std::getline(b, lb)) changed += la != lb;
    }
    check(changed == 5, "exactly five lines differ from the shipped file");
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
              grown.find("map_icons = false   # an icon on the minimap") != std::string::npos,
          "an older file gains the new keys, each with a word on what it is, and keeps what it had");
    check(config::rewrite(grown, g) == grown, "... and is then left as it is");
  }

  std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "all passed", g_failed);
  return g_failed ? 1 : 0;
}
