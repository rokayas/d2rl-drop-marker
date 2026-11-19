// The runes-only build's menu (trackmenu.h): a local addition.
#include "trackmenu.h"

#include <cstdint>
#include <cstdio>

#include "chatline.h"
#include "imgui.h"
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

  ImGui::Separator();
  ImGui::TextDisabled("Track: the drop is shown the ways ticked at the top. Not tracked: no icon and no message.");
  ImGui::TextDisabled("Pillar: a pillar of light stands on the drop while it lies on the ground near you, tracked or not.");
  if (pillar_note && pillar_note[0]) ImGui::TextDisabled("(no light pillars now: %s)", pillar_note);
  ImGui::TextDisabled("%s or Esc closes this menu. Choices are saved at once (rune-marker.toml).", key ? key : "");
  ImGui::End();
  return result;
}

}  // namespace d2rcc::trackmenu
