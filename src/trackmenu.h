#pragma once

// The runes-only build's menu (a local addition, not CabbyCodes' own): what is tracked - which runes, which of the
// other rare drops (track.h) - and how a tracked drop is shown: its icon on the minimap, its line in the chat, its
// pop-up on screen, each a switch of its own (track.h, chatline.h). Dear ImGui, drawn by the overlay while
// the menu's key has it up. On its own here, with nothing of the overlay or the game in it, so that
// tests/test_trackmenu.cpp can draw it and click it without the game.
namespace d2rcc::trackmenu {

inline constexpr const char* kTitle = "Drop Marker - what to track";

struct Result {
  bool changed = false;  // a choice was changed in this frame: it holds already, the caller has it saved
  bool closed = false;   // the window's close box was pressed
};

// Between ImGui::NewFrame() and ImGui::Render(). `scale`: the overlay's style scale (1 at 900 lines of screen);
// `key`: the name of the key that takes the menu down, for its last line.
Result draw(float scale, const char* key);

}  // namespace d2rcc::trackmenu
