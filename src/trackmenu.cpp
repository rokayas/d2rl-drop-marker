// The runes-only build's menu (trackmenu.h): a local addition.
#include "trackmenu.h"

#include <cstdint>
#include <cstdio>

#include "chatline.h"
#include "imgui.h"
#include "track.h"

namespace d2rcc::trackmenu {

Result draw(float scale, const char* key) {
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
  if (!icon && !chat && !popup) ImGui::TextDisabled("(all three are off: a tracked drop is not shown at all)");

  // The runes: three columns of eleven, the lowest first.
  ImGui::SeparatorText("Runes");
  const uint64_t runes_before = track::runes();
  uint64_t runes = runes_before;
  if (ImGui::Button("All")) runes = track::kAllRunes;
  ImGui::SameLine();
  if (ImGui::Button("None")) runes = 0;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(170.0f * scale);
  if (ImGui::BeginCombo("##from", "From a rune up...")) {
    for (int n = 1; n <= track::kRunes; ++n) {
      char label[48];
      std::snprintf(label, sizeof(label), "%s and every higher rune", track::kRuneNames[n - 1]);
      if (ImGui::Selectable(label)) runes = track::runes_from(n);
    }
    ImGui::EndCombo();
  }
  constexpr int kRows = 11, kColumns = track::kRunes / kRows;
  if (ImGui::BeginTable("##runes", kColumns, ImGuiTableFlags_SizingFixedFit)) {
    for (int row = 0; row < kRows; ++row) {
      ImGui::TableNextRow();
      for (int column = 0; column < kColumns; ++column) {
        const int n = column * kRows + row;  // 0 .. 32
        ImGui::TableSetColumnIndex(column);
        bool on = ((runes >> n) & 1) != 0;
        char label[32];
        std::snprintf(label, sizeof(label), "%-8s", track::kRuneNames[n]);  // a fixed-width font: columns of one width
        if (ImGui::Checkbox(label, &on)) runes = on ? runes | (1ull << n) : runes & ~(1ull << n);
      }
    }
    ImGui::EndTable();
  }
  int tracked = 0;
  for (int n = 0; n < track::kRunes; ++n) tracked += static_cast<int>((runes >> n) & 1);
  ImGui::TextDisabled("%d of %d runes tracked", tracked, track::kRunes);
  if (runes != runes_before) {
    track::set_runes(runes);
    result.changed = true;
  }

  // The other rare drops, a group each.
  ImGui::SeparatorText("Other drops");
  for (int g = 0; g < track::kGroups; ++g) {
    bool on = track::group(g);
    if (ImGui::Checkbox(track::kGroupNames[g], &on)) {
      track::set_group(g, on);
      result.changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", track::kGroupItems[g]);
  }

  ImGui::Separator();
  ImGui::TextDisabled("A tracked drop is shown the ways ticked at the top. What is not tracked is never shown.");
  ImGui::TextDisabled("%s or Esc closes this menu. Choices are saved at once (rune-marker.toml).", key ? key : "");
  ImGui::End();
  return result;
}

}  // namespace d2rcc::trackmenu
