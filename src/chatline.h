#pragma once

#include <cstdint>

// A line in the game's own chat: the box at the lower left of the screen, and its history. A local addition, not
// CabbyCodes' own.
//
// D2RLoader puts its own notices there with a routine of D2RCore.dll's. The plugin calls that routine only where its
// code is, byte for byte but for the addresses the linker fills in, the code this was written against (chatline.cpp);
// with any other loader the lines are said on screen instead (toast.h), as they are when the chat does not take one.
namespace d2rcc::chatline {

// The game's text colours, as the chat takes them.
constexpr uint8_t kWhite = 0, kRed = 1, kGreen = 2, kBlue = 3, kGold = 4, kGrey = 5, kTan = 7, kOrange = 8,
                  kYellow = 9;

// Where a line goes (the settings' rune_message): a bit for the game's chat and a bit for the screen (a pop-up near
// the top for a few seconds), so either, both or neither. A line for the chat that the chat cannot be given - the
// loader's routine is not there, or does not take it - goes on screen instead, whatever the screen's bit says.
enum Mode : int { kOff = 0, kChat = 1, kScreen = 2, kBoth = kChat | kScreen };
void set_mode(int mode);  // any thread
int mode();

// Once, as the plugin binds (any thread): the loader's routine found and checked.
void bind();
bool available();        // lines can go to the chat
const char* why_not();   // ... or why they cannot (nullptr when they can)

// Any thread: a line, said by the UI thread's next callback once `delay_ms` have passed - in the chat, on screen, or
// both, as the mode has it (and on screen where the chat was asked and is not to be had or does not take it). Nothing
// in the mode "off".
void announce(const char* text, uint8_t color, uint32_t delay_ms = 0);

void on_ui();    // the UI thread, once a frame: says the lines whose time has come
void overdue();  // the present thread: a line the UI thread did not come for is said on screen
void clear();    // any thread: a game ended, what waits is dropped

}  // namespace d2rcc::chatline
