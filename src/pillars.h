#pragma once

#include <cstdint>

// The light pillars (a local addition, not CabbyCodes' own). The map's icon needs the map open, and the line said as a
// drop falls scrolls away; a pillar is seen on the ground itself by whoever walks by. So a pillar of light stands on a
// rune or another rare drop for as long as it lies on the ground near you.
// Which drops get one is the menu's choice, a tick of its own beside each rune
// and each group (track.h), apart from what is tracked for the map and the messages.
//
// How. Once a frame on the UI thread (the loader's UI callback, as the health bars of CabbyCodes do it) the client's own
// items on the ground are looked through for the chosen ones, and the game is asked where each is on screen: its own
// projection of a unit (game.h project_unit: the routine under the one its overhead text uses). The present thread
// then draws the pillars with the overlay (pillarart.h), under its windows. Nothing of the game is hooked for it and
// nothing is written: the pillar is a drawing over the game's picture, not a light in its world - it is not hidden by
// a wall in front of it, and it is not there in legacy graphics, where the game has no such projection.
namespace d2rcc::pillars {

// After sites::derive() and game::bind() (ui_thread: the loader gives UI callbacks).
void bind(bool ui_thread);
// Why no pillar can be shown now, for the menu (null: they can): what the game did not have, or legacy graphics.
const char* why_not();

// The UI thread, once a frame.
void collect(uint64_t now_ms);
// The present thread, the ImGui lock held: whether there are pillars to draw in this frame, and their drawing.
bool take_snapshot();
void draw();

void game_left();

}  // namespace d2rcc::pillars
