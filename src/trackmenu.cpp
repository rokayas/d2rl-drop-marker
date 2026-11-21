// The runes-only build's menu (trackmenu.h): a local addition.
#include "trackmenu.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "chatline.h"
#include "imgui.h"
#include "named.h"
#include "track.h"

namespace d2rcc::trackmenu {
namespace {

// A row of buttons over the runes: all of them, none, or one and every higher. `what` is the row's name, `id` what
// tells its buttons from the other row's ("All##track", "All##pillar").
void rune_buttons(const char* what, const char* id, uint64_t* runes, float scale) {
  char label[64];
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%-13s", what);  // a fixed-width font: the two rows' buttons stand under each other
  ImGui::SameLine();
  std::snprintf(label, sizeof(label), "All##%s", id);
  if (ImGui::Button(label)) *runes = track::kAllRunes;
  ImGui::SameLine();
  std::snprintf(label, sizeof(label), "None##%s", id);
  if (ImGui::Button(label)) *runes = 0;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(170.0f * scale);
  std::snprintf(label, sizeof(label), "##from_%s", id);
  if (ImGui::BeginCombo(label, "From a rune up...")) {
    for (int n = 1; n <= track::kRunes; ++n) {
      std::snprintf(label, sizeof(label), "%s and every higher rune##%s", track::kRuneNames[n - 1], id);
      if (ImGui::Selectable(label)) *runes = track::runes_from(n);
    }
    ImGui::EndCombo();
  }
}

int count(uint64_t mask) {
  int n = 0;
  for (int i = 0; i < track::kRunes; ++i) n += static_cast<int>((mask >> i) & 1);
  return n;
}

// --- the menu's parts ------------------------------------------------------------------------------------------------
// The menu has three parts, each a line that folds out: the runes and the other rare drops (open as the menu comes
// up), the unique items, the set items. One is open at a time - opening a part folds the one that was open - so the
// menu is never taller than its tallest part, on any screen.
enum Part : int { kNoPart = -1, kRunesPart = 0, kUniquePart, kSetPart };
int g_part = kRunesPart;

// A part's line. True while the part is open.
bool part(int which, const char* label) {
  ImGui::SetNextItemOpen(g_part == which, ImGuiCond_Always);
  const bool open = ImGui::CollapsingHeader(label);
  if (open != (g_part == which)) g_part = open ? which : kNoPart;
  return g_part == which;
}

// --- the unique and set items ---------------------------------------------------------------------------------------
// Closed, a part is its line with what is chosen in it. Open, it is a search box and a list that scrolls, an entry a
// row with the two ticks a rune has; the entries stand under their kind (a unique item's) or their set, and the row
// of a kind or set ticks all of it that is shown.
constexpr int kListRows = 16;          // rows of the list before it scrolls
constexpr float kListWidth = 700.0f;   // at scale 1
constexpr const char* kSectionId[named::kKinds] = {"unique", "set"};
constexpr const char* kSectionTitle[named::kKinds] = {"Unique items", "Set items"};
constexpr const char* kSearchHint[named::kKinds] = {"a name, or a kind: ring, sword, helm ...", "an item's name, or its set's"};
char g_search[named::kKinds][48] = {};

// Whether `needle` is in `text`, in any case (the names' own letters; a name in another script is matched as typed).
bool has(const char* text, const char* needle) {
  const size_t n = std::strlen(needle);
  if (!n) return true;
  for (const char* p = text; *p; ++p)
    if (_strnicmp(p, needle, n) == 0) return true;
  return false;
}

// A row of buttons over a list: all that is shown, or none of it.
void shown_buttons(const char* what, const char* id, named::Kind kind, const int* order, int shown, named::Mask* mask) {
  char label[64];
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%-13s", what);
  ImGui::SameLine();
  std::snprintf(label, sizeof(label), "All shown##%s_%s", id, kSectionId[kind]);
  if (ImGui::Button(label))
    for (int k = 0; k < shown; ++k) mask->set(order[k], true);
  ImGui::SameLine();
  std::snprintf(label, sizeof(label), "None shown##%s_%s", id, kSectionId[kind]);
  if (ImGui::Button(label))
    for (int k = 0; k < shown; ++k) mask->set(order[k], false);
}

// One section. True when a choice changed.
bool named_section(named::Kind kind, float scale) {
  const named::Mask tracked_before = track::named_tracked(kind), lit_before = track::named_pillars(kind);
  named::Mask tracked = tracked_before, lit = lit_before;
  const int n = named::entries(kind);
  char label[160];
  std::snprintf(label, sizeof(label), "%s: %d of %d tracked, %d with a light pillar###named_%s", kSectionTitle[kind],
                tracked.count(), n, lit.count(), kSectionId[kind]);
  if (!part(kind == named::kUnique ? kUniquePart : kSetPart, label)) return false;
  char* search = g_search[kind];

  // What the search finds, in an entry's own name or in its kind's or set's, by kind or set and then by name.
  static int order[named::kMaxEntries];
  int shown = 0;
  for (int i = 0; i < n; ++i) {
    const named::Entry& e = named::entry(kind, i);
    if (has(named::display(kind, i), search) || has(e.item->english, search) ||
        has(named::group_display(kind, e.group), search) || has(named::group_english(kind, e.group), search))
      order[shown++] = i;
  }
  std::sort(order, order + shown, [kind](int a, int b) {
    const int ga = named::entry(kind, a).group, gb = named::entry(kind, b).group;
    if (ga != gb) return ga < gb;
    const int by_name = _stricmp(named::display(kind, a), named::display(kind, b));
    return by_name != 0 ? by_name < 0 : a < b;
  });

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%-13s", "Search");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(360.0f * scale);
  std::snprintf(label, sizeof(label), "##search_%s", kSectionId[kind]);
  ImGui::InputTextWithHint(label, kSearchHint[kind], search, sizeof(g_search[kind]));
  ImGui::SameLine();
  std::snprintf(label, sizeof(label), "Clear##search_%s", kSectionId[kind]);
  if (ImGui::Button(label)) search[0] = 0;
  ImGui::SameLine();
  ImGui::TextDisabled("%d shown", shown);
  shown_buttons("Track", "track", kind, order, shown, &tracked);
  shown_buttons("Light pillar", "pillar", kind, order, shown, &lit);

  const ImVec4 heading = kind == named::kUnique ? ImVec4(0.80f, 0.71f, 0.46f, 1.0f) : ImVec4(0.35f, 0.86f, 0.38f, 1.0f);
  std::snprintf(label, sizeof(label), "##list_%s", kSectionId[kind]);
  const ImVec2 size(kListWidth * scale, ImGui::GetFrameHeightWithSpacing() * (kListRows + 1));
  if (ImGui::BeginChild(label, size, ImGuiChildFlags_Borders)) {
    std::snprintf(label, sizeof(label), "##rows_%s", kSectionId[kind]);
    if (shown && ImGui::BeginTable(label, 3, ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("Track");
      ImGui::TableSetColumnIndex(1);
      ImGui::TextDisabled("Pillar");
      for (int at = 0; at < shown;) {
        const int group = named::entry(kind, order[at]).group;
        int end = at;
        bool all_tracked = true, all_lit = true;
        for (; end < shown && named::entry(kind, order[end]).group == group; ++end) {
          all_tracked = all_tracked && tracked.get(order[end]);
          all_lit = all_lit && lit.get(order[end]);
        }
        // The kind or set: its two ticks are for all of it that is shown.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::snprintf(label, sizeof(label), "##track_%s_group_%d", kSectionId[kind], group);
        if (ImGui::Checkbox(label, &all_tracked))
          for (int k = at; k < end; ++k) tracked.set(order[k], all_tracked);
        ImGui::TableSetColumnIndex(1);
        std::snprintf(label, sizeof(label), "##pillar_%s_group_%d", kSectionId[kind], group);
        if (ImGui::Checkbox(label, &all_lit))
          for (int k = at; k < end; ++k) lit.set(order[k], all_lit);
        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(heading, "%s  (%d)", named::group_display(kind, group), end - at);
        for (; at < end; ++at) {
          const int e = order[at];
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          bool on = tracked.get(e);
          std::snprintf(label, sizeof(label), "##track_%s_%d", kSectionId[kind], e);
          if (ImGui::Checkbox(label, &on)) tracked.set(e, on);
          ImGui::TableSetColumnIndex(1);
          bool pillar = lit.get(e);
          std::snprintf(label, sizeof(label), "##pillar_%s_%d", kSectionId[kind], e);
          if (ImGui::Checkbox(label, &pillar)) lit.set(e, pillar);
          ImGui::TableSetColumnIndex(2);
          ImGui::AlignTextToFramePadding();
          ImGui::Text("   %s", named::display(kind, e));
        }
      }
      ImGui::EndTable();
    }
    if (!shown) ImGui::TextDisabled("Nothing of that name.");
  }
  ImGui::EndChild();
  bool changed = false;
  if (tracked != tracked_before) {
    track::set_named_tracked(kind, tracked);
    changed = true;
  }
  if (lit != lit_before) {
    track::set_named_pillars(kind, lit);
    changed = true;
  }
  return changed;
}

}  // namespace

Result draw(float scale, const char* key, const char* pillar_note) {
  Result result;
  bool open = true;
  // In the middle of the screen each time it comes up (an ultrawide screen's sides are outside the game's picture); it
  // can be dragged by its title from there.
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (!ImGui::Begin(kTitle, &open, flags)) {
    ImGui::End();
    result.closed = !open;
    return result;
  }
  result.closed = !open;

  // How a tracked drop is shown: three switches, any of them, all of them or none.
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Show a tracked drop as:");
  bool icon = track::icons();
  const int mode_before = chatline::mode();
  bool chat = (mode_before & chatline::kChat) != 0, popup = (mode_before & chatline::kScreen) != 0;
  ImGui::SameLine();
  if (ImGui::Checkbox("Minimap icon", &icon)) {
    track::set_icons(icon);
    result.changed = true;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Chat line", &chat);
  ImGui::SameLine();
  ImGui::Checkbox("Pop-up on screen", &popup);
  const int mode = (chat ? chatline::kChat : 0) | (popup ? chatline::kScreen : 0);
  if (mode != mode_before) {
    chatline::set_mode(mode);
    result.changed = true;
  }
  if (chat && !chatline::available())
    ImGui::TextDisabled("(the chat cannot be reached with this loader: its lines are shown as pop-ups instead)");
  if (!icon && !chat && !popup) ImGui::TextDisabled("(all three are off: a tracked drop gets no icon and no message)");

  // The first part: the runes and the other rare drops.
  char part_label[160];
  int groups_tracked = 0;
  for (int g = 0; g < track::kGroups; ++g) groups_tracked += track::group(g) ? 1 : 0;
  std::snprintf(part_label, sizeof(part_label), "Runes and other drops: %d of %d runes tracked, %d of %d other drops###part_runes",
                count(track::runes()), track::kRunes, groups_tracked, static_cast<int>(track::kGroups));
  if (part(kRunesPart, part_label)) {
  // The runes: three columns of eleven, the lowest first. Each has two ticks: tracked (its name), and a light pillar.
  ImGui::SeparatorText("Runes");
  const uint64_t runes_before = track::runes(), lit_before = track::pillar_runes();
  uint64_t runes = runes_before, lit = lit_before;
  rune_buttons("Track", "track", &runes, scale);
  rune_buttons("Light pillar", "pillar", &lit, scale);
  constexpr int kRows = 11, kColumns = track::kRunes / kRows;
  if (ImGui::BeginTable("##runes", kColumns * 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableNextRow();
    for (int column = 0; column < kColumns; ++column) {
      ImGui::TableSetColumnIndex(column * 2);
      ImGui::TextDisabled("Track");
      ImGui::TableSetColumnIndex(column * 2 + 1);
      ImGui::TextDisabled("Pillar    ");  // the space after it: what sets the three pairs of columns apart
    }
    for (int row = 0; row < kRows; ++row) {
      ImGui::TableNextRow();
      for (int column = 0; column < kColumns; ++column) {
        const int n = column * kRows + row;  // 0 .. 32
        ImGui::TableSetColumnIndex(column * 2);
        bool on = ((runes >> n) & 1) != 0;
        char label[32];
        std::snprintf(label, sizeof(label), "%-8s", track::kRuneNames[n]);  // a fixed-width font: columns of one width
        if (ImGui::Checkbox(label, &on)) runes = on ? runes | (1ull << n) : runes & ~(1ull << n);
        ImGui::TableSetColumnIndex(column * 2 + 1);
        bool pillar = ((lit >> n) & 1) != 0;
        std::snprintf(label, sizeof(label), "##pillar_%s", track::kRuneNames[n]);
        if (ImGui::Checkbox(label, &pillar)) lit = pillar ? lit | (1ull << n) : lit & ~(1ull << n);
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("%d of %d runes tracked, %d with a light pillar", count(runes), track::kRunes, count(lit));
  if (runes != runes_before) {
    track::set_runes(runes);
    result.changed = true;
  }
  if (lit != lit_before) {
    track::set_pillar_runes(lit);
    result.changed = true;
  }

  // The other rare drops, a group each, with the same two ticks.
  ImGui::SeparatorText("Other drops");
  if (ImGui::BeginTable("##groups", 3, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("Track");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("Pillar");
    for (int g = 0; g < track::kGroups; ++g) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      bool on = track::group(g);
      if (ImGui::Checkbox(track::kGroupNames[g], &on)) {
        track::set_group(g, on);
        result.changed = true;
      }
      ImGui::TableSetColumnIndex(1);
      bool pillar = track::pillar_group(g);
      char label[48];
      std::snprintf(label, sizeof(label), "##pillar_%s", track::kGroupKeys[g]);
      if (ImGui::Checkbox(label, &pillar)) {
        track::set_pillar_group(g, pillar);
        result.changed = true;
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", track::kGroupItems[g]);
    }
    ImGui::EndTable();
  }

  }  // the first part

  // The unique and set items: a part each.
  if (named_section(named::kUnique, scale)) result.changed = true;
  if (named_section(named::kSetItem, scale)) result.changed = true;

  ImGui::Separator();
  ImGui::TextDisabled("Track: the drop is shown the ways ticked at the top. Not tracked: no icon and no message.");
  ImGui::TextDisabled("Pillar: a pillar of light stands on the drop while it lies on the ground near you, tracked or not.");
  if (pillar_note && pillar_note[0]) ImGui::TextDisabled("(no light pillars now: %s)", pillar_note);
  ImGui::TextDisabled("%s or Esc closes this menu. Choices are saved at once (rune-marker.toml).", key ? key : "");
  ImGui::End();
  return result;
}

}  // namespace d2rcc::trackmenu
