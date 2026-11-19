#pragma once

#include <cstdint>

struct ImFont;

// A line said on screen for a few seconds: near the top of the screen, centred, drawn by the overlay together with the
// health bars. Local addition for the rune drop message; not part of upstream CabbyCodes 1.4.2.
namespace d2rcc::toast {

// Any thread. `text` is UTF-8; the colour is 0 .. 255 a channel.
void push(const char* text, uint8_t r, uint8_t g, uint8_t b);
// The present thread, the ImGui lock held: whether a line is still up ...
bool any();
// ... and the lines, between ImGui::NewFrame() and ImGui::Render().
void draw(ImFont* font);
// Any thread: nothing is left up (a game ended).
void clear();

}  // namespace d2rcc::toast
