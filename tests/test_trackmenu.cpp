// The menu (src/trackmenu.cpp, a local addition), drawn and clicked without the game: Dear ImGui with no renderer, its
// test hooks giving every item's place by its label, mouse events put in as a player's would come, and a small software
// rasteriser writing what was drawn to BMP files to look at.
//
// Built by hand (Dear ImGui compiled with its test hooks, so not one of `make test`'s):
//   g++ -std=c++20 -O2 -DIMGUI_ENABLE_TEST_ENGINE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Isrc -Icontrib/imgui -static
//       -o tests/build/test_trackmenu.exe tests/test_trackmenu.cpp src/trackmenu.cpp src/track.cpp
//       contrib/imgui/imgui.cpp contrib/imgui/imgui_draw.cpp contrib/imgui/imgui_tables.cpp
//       contrib/imgui/imgui_widgets.cpp
//   test_trackmenu.exe [folder for the pictures] [screen width] [screen height]
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "../src/chatline.h"
#include "../src/track.h"
#include "../src/trackmenu.h"

using namespace d2rcc;

// --- what trackmenu.cpp links against besides track.cpp, stood in for ---------------------------------------------
namespace {
int g_mode = chatline::kChat;
bool g_chat_ok = true;
}  // namespace
namespace d2rcc::chatline {
int mode() { return g_mode; }
void set_mode(int m) { g_mode = m; }
bool available() { return g_chat_ok; }
}  // namespace d2rcc::chatline

// --- Dear ImGui's test hooks: every item of the frame, by its id ---------------------------------------------------
namespace {
struct Item {
  ImRect bb;
  std::string label;
};
std::map<ImGuiID, Item> g_items;
}  // namespace
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
  if (id) g_items[id].bb = bb;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
  if (id && label) g_items[id].label = label;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
  const auto it = g_items.find(id);
  return it == g_items.end() ? nullptr : it->second.label.c_str();
}

namespace {

constexpr float kScale = 1.6f;  // the overlay's at 1440 lines of screen
int g_failed = 0;
trackmenu::Result g_seen;  // what the menu answered since it was last asked (seen())

void check(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++g_failed;
}

void frame() {
  ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
  g_items.clear();
  ImGui::NewFrame();
  const trackmenu::Result r = trackmenu::draw(kScale, "F7");
  g_seen.changed = g_seen.changed || r.changed;
  g_seen.closed = g_seen.closed || r.closed;
  ImGui::Render();
}
trackmenu::Result seen() {
  const trackmenu::Result r = g_seen;
  g_seen = {};
  return r;
}

std::string trimmed(std::string s) {
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}
// An item of the last frame by its label (the spaces after a rune's name left out).
bool find(const char* label, ImRect* bb) {
  for (const auto& [id, item] : g_items)
    if (trimmed(item.label) == label && item.bb.GetWidth() > 0) {
      *bb = item.bb;
      return true;
    }
  return false;
}
// The mouse moved there, pressed and let go, a frame each as a player's would come.
void click_at(ImVec2 p) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(p.x, p.y);
  frame();
  io.AddMouseButtonEvent(0, true);
  frame();
  io.AddMouseButtonEvent(0, false);
  frame();
  frame();
}
bool click(const char* label) {
  ImRect bb;
  if (!find(label, &bb)) {
    std::printf("    (no item \"%s\" in the frame)\n", label);
    return false;
  }
  // Inside its left end: a checkbox's box, a button's or a list entry's body.
  click_at(ImVec2(bb.Min.x + std::min(bb.GetHeight(), bb.GetWidth()) * 0.5f, bb.GetCenter().y));
  return true;
}

// --- a picture of the frame -----------------------------------------------------------------------------------------
struct Canvas {
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;  // 3 bytes a pixel, top row first
};
const unsigned char* g_atlas = nullptr;
int g_atlas_w = 0, g_atlas_h = 0;

void texel(float u, float v, float out[4]) {  // bilinear
  const float x = u * static_cast<float>(g_atlas_w) - 0.5f, y = v * static_cast<float>(g_atlas_h) - 0.5f;
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
  for (int c = 0; c < 4; ++c) out[c] = 0.0f;
  for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx) {
      const int xx = std::clamp(x0 + dx, 0, g_atlas_w - 1), yy = std::clamp(y0 + dy, 0, g_atlas_h - 1);
      const float wgt = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
      const unsigned char* p = g_atlas + (static_cast<size_t>(yy) * static_cast<size_t>(g_atlas_w) + static_cast<size_t>(xx)) * 4;
      for (int c = 0; c < 4; ++c) out[c] += wgt * static_cast<float>(p[c]);
    }
}

void paint(const ImDrawData* dd, Canvas* cv) {
  for (const ImDrawList* list : dd->CmdLists) {
    const ImDrawVert* vtx = list->VtxBuffer.Data;
    const ImDrawIdx* idx = list->IdxBuffer.Data;
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback) continue;
      const int cx0 = std::max(0, static_cast<int>(cmd.ClipRect.x)), cy0 = std::max(0, static_cast<int>(cmd.ClipRect.y));
      const int cx1 = std::min(cv->w, static_cast<int>(cmd.ClipRect.z)), cy1 = std::min(cv->h, static_cast<int>(cmd.ClipRect.w));
      for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
        const ImDrawVert& a = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i]];
        const ImDrawVert& b = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i + 1]];
        const ImDrawVert& c = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i + 2]];
        const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
        if (area == 0.0f) continue;
        const int x0 = std::max(cx0, static_cast<int>(std::floor(std::min({a.pos.x, b.pos.x, c.pos.x}))));
        const int x1 = std::min(cx1, static_cast<int>(std::ceil(std::max({a.pos.x, b.pos.x, c.pos.x}))));
        const int y0 = std::max(cy0, static_cast<int>(std::floor(std::min({a.pos.y, b.pos.y, c.pos.y}))));
        const int y1 = std::min(cy1, static_cast<int>(std::ceil(std::max({a.pos.y, b.pos.y, c.pos.y}))));
        const ImDrawVert* v[3] = {&a, &b, &c};
        float col[3][4];
        for (int k = 0; k < 3; ++k) {
          col[k][0] = static_cast<float>(v[k]->col & 0xFF);
          col[k][1] = static_cast<float>((v[k]->col >> 8) & 0xFF);
          col[k][2] = static_cast<float>((v[k]->col >> 16) & 0xFF);
          col[k][3] = static_cast<float>((v[k]->col >> 24) & 0xFF);
        }
        for (int y = y0; y < y1; ++y)
          for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
            const float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
            const float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            float t[4];
            texel(w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x, w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y, t);
            float src[4];
            for (int k = 0; k < 4; ++k) src[k] = (w0 * col[0][k] + w1 * col[1][k] + w2 * col[2][k]) * t[k] / 255.0f;
            const float alpha = src[3] / 255.0f;
            uint8_t* d = &cv->rgb[(static_cast<size_t>(y) * static_cast<size_t>(cv->w) + static_cast<size_t>(x)) * 3];
            for (int k = 0; k < 3; ++k)
              d[k] = static_cast<uint8_t>(std::clamp(src[k] * alpha + static_cast<float>(d[k]) * (1.0f - alpha), 0.0f, 255.0f));
          }
      }
    }
  }
}

bool save_bmp(const Canvas& cv, const std::string& path) {
  const int row = (cv.w * 3 + 3) & ~3;
  const uint32_t size = 54 + static_cast<uint32_t>(row) * static_cast<uint32_t>(cv.h);
  std::vector<uint8_t> f(size, 0);
  f[0] = 'B';
  f[1] = 'M';
  std::memcpy(&f[2], &size, 4);
  const uint32_t off = 54, hdr = 40;
  std::memcpy(&f[10], &off, 4);
  std::memcpy(&f[14], &hdr, 4);
  std::memcpy(&f[18], &cv.w, 4);
  std::memcpy(&f[22], &cv.h, 4);
  const uint16_t planes = 1, bpp = 24;
  std::memcpy(&f[26], &planes, 2);
  std::memcpy(&f[28], &bpp, 2);
  for (int y = 0; y < cv.h; ++y) {
    uint8_t* out = &f[54 + static_cast<size_t>(cv.h - 1 - y) * static_cast<size_t>(row)];
    const uint8_t* in = &cv.rgb[static_cast<size_t>(y) * static_cast<size_t>(cv.w) * 3];
    for (int x = 0; x < cv.w; ++x) {
      out[x * 3 + 0] = in[x * 3 + 2];
      out[x * 3 + 1] = in[x * 3 + 1];
      out[x * 3 + 2] = in[x * 3 + 0];
    }
  }
  FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return false;
  const bool ok = std::fwrite(f.data(), 1, f.size(), fp) == f.size();
  std::fclose(fp);
  return ok;
}

// The last frame as a picture: the part of the screen around the menu (and what hangs from it), on a dark ground.
void shoot(const std::string& folder, const char* name) {
  if (folder.empty()) return;
  const ImGuiIO& io = ImGui::GetIO();
  Canvas full;
  full.w = static_cast<int>(io.DisplaySize.x);
  full.h = static_cast<int>(io.DisplaySize.y);
  full.rgb.assign(static_cast<size_t>(full.w) * static_cast<size_t>(full.h) * 3, 0);
  for (size_t i = 0; i < full.rgb.size(); i += 3) {
    full.rgb[i] = 38;
    full.rgb[i + 1] = 44;
    full.rgb[i + 2] = 40;
  }
  paint(ImGui::GetDrawData(), &full);
  ImRect area(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
  for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
    if (w->WasActive && !w->Hidden && !(w->Flags & ImGuiWindowFlags_ChildWindow) && std::strncmp(w->Name, "Debug", 5) != 0)
      area.Add(w->Rect());
  area.Expand(24.0f);
  const int x0 = std::max(0, static_cast<int>(area.Min.x)), y0 = std::max(0, static_cast<int>(area.Min.y));
  const int x1 = std::min(full.w, static_cast<int>(area.Max.x)), y1 = std::min(full.h, static_cast<int>(area.Max.y));
  Canvas crop;
  crop.w = std::max(1, x1 - x0);
  crop.h = std::max(1, y1 - y0);
  crop.rgb.resize(static_cast<size_t>(crop.w) * static_cast<size_t>(crop.h) * 3);
  for (int y = 0; y < crop.h; ++y)
    std::memcpy(&crop.rgb[static_cast<size_t>(y) * static_cast<size_t>(crop.w) * 3],
                &full.rgb[(static_cast<size_t>(y + y0) * static_cast<size_t>(full.w) + static_cast<size_t>(x0)) * 3],
                static_cast<size_t>(crop.w) * 3);
  const std::string path = folder + "/" + name + ".bmp";
  std::printf("    picture %s (%d x %d)%s\n", path.c_str(), crop.w, crop.h, save_bmp(crop, path) ? "" : " NOT written");
}

ImGuiWindow* menu() { return ImGui::FindWindowByName(trackmenu::kTitle); }
// An item of the menu that tells the hooks no label (a combo box, the close box): by its id in the window.
bool click_id(const char* str_id) {
  const auto it = g_items.find(menu()->GetID(str_id));
  if (it == g_items.end() || it->second.bb.GetWidth() <= 0) {
    std::printf("    (no item with the id \"%s\" in the frame)\n", str_id);
    return false;
  }
  click_at(it->second.bb.GetCenter());
  return true;
}
const ImGuiWindow* open_popup() {
  for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
    if ((w->Flags & ImGuiWindowFlags_Popup) && w->Active && !w->Hidden) return w;  // begun in the last frame
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // every line out at once: what was said stands if a check brings it down
  const std::string folder = argc > 1 ? argv[1] : "";
  const float screen_w = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 5120.0f;
  const float screen_h = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 1440.0f;

  // The context as the overlay sets it up (overlay.cpp ensure_context), with no backend.
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(screen_w, screen_h);
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  io.Fonts->AddFontDefault();
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.ScaleAllSizes(kScale);
  style.FontScaleMain = kScale;
  unsigned char* pixels = nullptr;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &g_atlas_w, &g_atlas_h);
  g_atlas = pixels;
  ImGui::GetCurrentContext()->TestEngineHookItems = true;

  track::init(track::kAllRunes, track::kDefaultGroups);
  io.AddMousePosEvent(5.0f, 5.0f);
  for (int i = 0; i < 4; ++i) frame();  // a window that sizes itself is hidden for its first frames
  seen();

  std::printf("1. the menu as it comes up (%g x %g screen, scale %.1f)\n", screen_w, screen_h, kScale);
  const ImGuiWindow* w = menu();
  check(w && w->WasActive && !w->Hidden, "the window is up");
  if (!w) return 1;
  std::printf("    window at (%.0f, %.0f), %.0f x %.0f\n", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y);
  check(w->Pos.x >= 0 && w->Pos.y >= 0 && w->Pos.x + w->Size.x <= screen_w && w->Pos.y + w->Size.y <= screen_h,
        "it is on the screen whole");
  check(std::fabs((w->Pos.x + w->Size.x * 0.5f) - screen_w * 0.5f) < 2.0f &&
            std::fabs((w->Pos.y + w->Size.y * 0.5f) - screen_h * 0.5f) < 2.0f,
        "in the middle of it");
  check(w->Size.x <= 0.6f * 3040.0f, "no wider than six tenths of a 19:9 picture 1440 lines high");
  int runes = 0, groups = 0;
  ImRect bb;
  for (int n = 0; n < track::kRunes; ++n) runes += find(track::kRuneNames[n], &bb) ? 1 : 0;
  for (int g = 0; g < track::kGroups; ++g) groups += find(track::kGroupNames[g], &bb) ? 1 : 0;
  check(runes == track::kRunes, "a checkbox for each of the 33 runes");
  check(groups == track::kGroups, "a checkbox for each group of the other drops");
  {
    ImRect el, sol, mal, amn;
    const bool ok = find("El", &el) && find("Sol", &sol) && find("Mal", &mal) && find("Amn", &amn);
    check(ok && el.Min.y == sol.Min.y && sol.Min.y == mal.Min.y && el.Min.x < sol.Min.x && sol.Min.x < mal.Min.x &&
              amn.Min.y > el.Min.y && amn.Min.x == el.Min.x,
          "three columns: El .. Amn, Sol .. Um, Mal .. Zod");
  }
  check(!seen().changed, "nothing counts as changed before anything is clicked");
  shoot(folder, "1-as-it-comes-up");

  std::printf("2. the runes\n");
  check(click("None") && track::runes() == 0 && seen().changed, "\"None\" clears them all, and says so");
  check(click("Ber") && track::runes() == (1ull << 29) && seen().changed, "a click on Ber's box tracks Ber alone");
  check(click("Jah") && click("Zod") && track::rune(31) && track::rune(33) && !track::rune(32), "Jah and Zod too, not Cham");
  check(click("Ber") && !track::rune(30) && track::rune(31), "a second click on Ber's box lets Ber go");
  seen();
  check(click("All") && track::runes() == track::kAllRunes && seen().changed, "\"All\" tracks them all");
  frame();
  check(!seen().changed, "a frame with no click changes nothing");

  std::printf("3. \"from a rune up\"\n");
  check(click_id("##from") && open_popup() != nullptr, "a click opens the list");
  shoot(folder, "2-the-list-open");
  check(click("Tir and every higher rune") && track::runes() == track::runes_from(3) && open_popup() == nullptr,
        "an entry in sight: Tir and higher, and the list closes");
  check(click_id("##from") && open_popup() != nullptr, "the list again");
  {
    // An entry out of sight: the wheel, over the list, until it is in it.
    const ImGuiWindow* pop = open_popup();
    bool in_sight = false;
    ImRect entry;
    if (pop) {
      io.AddMousePosEvent(pop->Rect().GetCenter().x, pop->Rect().GetCenter().y);
      frame();
    }
    for (int i = 0; i < 60 && !in_sight && pop; ++i) {
      pop = open_popup();
      in_sight = pop && find("Ist and every higher rune", &entry) && pop->InnerClipRect.Contains(entry.GetCenter());
      if (in_sight) break;
      io.AddMouseWheelEvent(0.0f, -1.0f);
      frame();
      frame();
    }
    check(in_sight, "the wheel brings \"Ist and every higher rune\" into sight");
    check(click("Ist and every higher rune") && track::runes() == track::runes_from(24) && !track::rune(23) && track::rune(24),
          "... and a click on it tracks Ist and higher");
  }
  seen();
  shoot(folder, "3-ist-and-higher");

  std::printf("4. the other drops\n");
  check(click("Fragments") && track::group(track::kFragments) && seen().changed, "Fragments on");
  check(click("Essences") && !track::group(track::kEssences) && track::group(track::kKeys), "Essences off, Keys as they were");
  seen();

  std::printf("5. how a tracked drop is shown: three switches\n");
  {
    ImRect icon, chat, popup;
    const bool ok = find("Minimap icon", &icon) && find("Chat line", &chat) && find("Pop-up on screen", &popup);
    check(ok && icon.Min.y == chat.Min.y && chat.Min.y == popup.Min.y && icon.Min.x < chat.Min.x && chat.Min.x < popup.Min.x,
          "a box each for the minimap icon, the chat line and the pop-up, in one row");
  }
  check(track::icons() && g_mode == chatline::kChat, "as it comes up: the icon and the chat line, no pop-up");
  check(click("Pop-up on screen") && g_mode == chatline::kBoth && track::icons() && seen().changed,
        "the pop-up ticked as well: all three on");
  shoot(folder, "4-all-three-on");
  check(click("Minimap icon") && !track::icons() && g_mode == chatline::kBoth && seen().changed,
        "the minimap icon off: the chat line and the pop-up");
  check(click("Chat line") && g_mode == chatline::kScreen && !track::icons(), "the chat line off too: the pop-up alone");
  check(click("Pop-up on screen") && g_mode == chatline::kOff && !track::icons() && seen().changed, "all three off: none");
  shoot(folder, "5-none");
  check(click("Chat line") && g_mode == chatline::kChat && !track::icons(), "chat only");
  check(click("Chat line") && click("Minimap icon") && g_mode == chatline::kOff && track::icons(), "minimap only");
  check(click("Chat line") && g_mode == chatline::kChat && track::icons(), "both: the minimap icon and the chat line");
  seen();
  frame();
  check(!seen().changed, "a frame with no click changes nothing");
  g_chat_ok = false;
  frame();
  frame();
  shoot(folder, "6-chat-out-of-reach");
  g_chat_ok = true;
  frame();

  std::printf("6. taking it down\n");
  seen();
  check(click_id("#CLOSE"), "the window has a close box");
  check(seen().closed, "a click on it says \"closed\"");
  check(track::runes() == track::runes_from(24) && track::group(track::kFragments) && !track::group(track::kEssences) &&
            track::icons() && g_mode == chatline::kChat,
        "the choices stand as they were made");

  ImGui::DestroyContext();
  std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "all passed", g_failed);
  return g_failed ? 1 : 0;
}
