// The chat line (src/chatline.cpp, a local addition): its find-and-check of the loader's routine, run against a real
// D2RCore.dll mapped as data-with-relocations (none of its code runs), and its queue, run against a stand-in.
//
//   test_chatline.exe "<game folder>\D2RCore.dll"
//
// Built by hand (it needs the real file, so it is not one of `make test`'s):
//   g++ -std=c++20 -O2 -DD2RCC_CHATLINE_TEST -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Isrc
//       -Icontrib/D2RLoader-PluginSDK/include -static -o tests/build/test_chatline.exe
//       tests/test_chatline.cpp src/chatline.cpp src/mem.cpp
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/chatline.h"
#include "../src/log.h"
#include "../src/mem.h"
#include "../src/toast.h"

// --- what chatline.cpp links against, stood in for ---------------------------------------------------------------
namespace {
std::vector<std::string> g_log;        // every line logged
std::vector<std::string> g_screen;     // every line said on screen, "r,g,b text"
struct Pushed {
  uint32_t kind, color, unit;
  std::string text;
};
std::vector<Pushed> g_pushed;          // every call of the stand-in routine
uint64_t g_push_answer = 1;
int g_level = 1;

void keep(const char* tag, const char* fmt, va_list ap) {
  char b[1024];
  std::vsnprintf(b, sizeof(b), fmt, ap);
  g_log.push_back(std::string(tag) + b);
  std::printf("    %s%s\n", tag, b);
}
uint64_t fake_push(uint32_t kind, uint32_t color, uint32_t unit, const char* text) {
  g_pushed.push_back(Pushed{kind, color, unit, text ? text : "<null>"});
  return g_push_answer;
}
}  // namespace

namespace d2rcc {
void logf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  keep("[info] ", fmt, ap);
  va_end(ap);
}
void log_warn(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  keep("[warn] ", fmt, ap);
  va_end(ap);
}
bool log_first(volatile long* count, long limit, LogLevel) { return InterlockedIncrement(count) <= limit; }
namespace toast {
void push(const char* text, uint8_t r, uint8_t g, uint8_t b) {
  char line[256];
  std::snprintf(line, sizeof(line), "%u,%u,%u %s", r, g, b, text);
  g_screen.push_back(line);
}
}  // namespace toast
namespace game {
int current_level() { return g_level; }
}  // namespace game
namespace chatline {
void set_push_for_test(uint64_t (*fn)(uint32_t, uint32_t, uint32_t, const char*));
}
}  // namespace d2rcc

using namespace d2rcc;

namespace {
int g_failed = 0;
void check(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++g_failed;
}
void reset() {
  g_screen.clear();
  g_pushed.clear();
  g_push_answer = 1;
  g_level = 1;
  chatline::clear();
  chatline::set_mode(chatline::kChat);
}
// One byte of the mapped image changed and put back (the image is ours alone: mapped, never run).
bool poke(uintptr_t at, uint8_t value, uint8_t* was) {
  DWORD old = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(at), 1, PAGE_EXECUTE_READWRITE, &old)) return false;
  *was = *reinterpret_cast<uint8_t*>(at);
  *reinterpret_cast<uint8_t*>(at) = value;
  VirtualProtect(reinterpret_cast<void*>(at), 1, old, &old);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: test_chatline <path to D2RCore.dll>\n");
    return 2;
  }
  mem::guard_install();

  std::printf("1. no loader in the process\n");
  chatline::bind();
  check(!chatline::available(), "not available without D2RCore.dll");
  check(chatline::why_not() && std::strstr(chatline::why_not(), "not loaded"), "says the DLL is not loaded");

  std::printf("2. the real D2RCore.dll, mapped (its code does not run)\n");
  wchar_t path[MAX_PATH];
  MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, path, MAX_PATH);
  const HMODULE core = LoadLibraryExW(path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!core) {
    std::printf("  cannot map %s (error %lu)\n", argv[1], GetLastError());
    return 2;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(core);
  check(GetModuleHandleW(L"D2RCore.dll") == core, "the mapped file answers to the name D2RCore.dll");
  chatline::bind();
  check(chatline::available(), "bind() finds and accepts the loader's routine");
  check(chatline::why_not() == nullptr, "no reason given when it is accepted");
  const uintptr_t entry = reinterpret_cast<uintptr_t>(GetProcAddress(core, "PushClientChatEntry"));
  std::printf("    PushClientChatEntry at D2RCore.dll+0x%llX\n", static_cast<unsigned long long>(entry - base));

  std::printf("3. a loader whose code differs is refused\n");
  uint8_t was = 0;
  // In the export: the instruction that takes the colour from the second argument (89 D3, mov ebx, edx).
  check(poke(entry + 0x29, 0x8B, &was) && was == 0x89, "one byte of the export changed");
  chatline::bind();
  check(!chatline::available(), "refused: the export is not the code written against");
  poke(entry + 0x29, was, &was);
  chatline::bind();
  check(chatline::available(), "accepted again with the byte put back");
  // In the routine itself: where it takes its text (4D 89 CE, mov r14, r9), 0x26 bytes in.
  const uintptr_t push = mem::rip_target(entry + 0x89, 1, 5);
  std::printf("    the routine at D2RCore.dll+0x%llX\n", static_cast<unsigned long long>(push - base));
  check(poke(push + 0x28, 0xC6, &was) && was == 0xCE, "one byte of the routine changed");
  chatline::bind();
  check(!chatline::available(), "refused: the routine is not the code written against");
  poke(push + 0x28, was, &was);
  // An address the linker fills in may differ: the rel32 of the export's first call.
  uint8_t was2 = 0;
  check(poke(entry + 0x53, static_cast<uint8_t>(*reinterpret_cast<uint8_t*>(entry + 0x53) ^ 0xFF), &was2),
        "an address inside the export changed (a call's rel32)");
  chatline::bind();
  check(chatline::available(), "still accepted: addresses are left open");
  poke(entry + 0x53, was2, &was2);
  chatline::bind();
  check(chatline::available(), "accepted with everything put back");

  // From here on the stand-in is what gets called: the real routine must not run outside the game.
  chatline::set_push_for_test(&fake_push);

  std::printf("4. a line goes to the chat\n");
  reset();
  chatline::announce("Ber Rune dropped", chatline::kOrange);
  check(g_pushed.empty() && g_screen.empty(), "nothing is said by the thread that announces");
  chatline::on_ui();
  check(g_pushed.size() == 1 && g_pushed[0].text == "Ber Rune dropped" && g_pushed[0].color == 8 &&
            g_pushed[0].kind == 0 && g_pushed[0].unit == 0,
        "the UI thread hands it to the routine: (0, 8, 0, \"Ber Rune dropped\")");
  check(g_screen.empty(), "and nothing goes on screen");
  chatline::on_ui();
  check(g_pushed.size() == 1, "it is said once");

  std::printf("5. the chat does not take it\n");
  reset();
  g_push_answer = 0;
  chatline::announce("Jah Rune dropped", chatline::kOrange);
  chatline::on_ui();
  check(g_pushed.size() == 1, "the routine was asked");
  check(g_screen.size() == 1 && g_screen[0] == "255,168,0 Jah Rune dropped", "it is said on screen, in orange");

  std::printf("6. not in a game\n");
  reset();
  g_level = 0;
  chatline::announce("El Rune dropped", chatline::kOrange);
  chatline::on_ui();
  check(g_pushed.empty(), "the routine is not called outside a game");
  check(g_screen.size() == 1, "the line goes on screen");

  std::printf("7. the modes\n");
  reset();
  chatline::set_mode(chatline::kScreen);
  chatline::announce("Sol Rune dropped", chatline::kOrange);
  check(g_screen.size() == 1 && g_pushed.empty(), "\"screen\": on screen at once, the chat not asked");
  chatline::on_ui();
  check(g_screen.size() == 1 && g_pushed.empty(), "... and not a second time");
  reset();
  chatline::set_mode(chatline::kOff);
  chatline::announce("Sol Rune dropped", chatline::kOrange);
  chatline::on_ui();
  check(g_screen.empty() && g_pushed.empty(), "\"off\": nothing anywhere");
  check(chatline::kBoth == (chatline::kChat | chatline::kScreen) && chatline::kOff == 0, "the modes are a bit each: chat 1, screen 2, both 3");
  reset();
  chatline::set_mode(chatline::kBoth);
  check(chatline::mode() == chatline::kBoth, "\"both\" is kept as the mode");
  chatline::announce("Lo Rune dropped", chatline::kOrange);
  check(g_screen.empty() && g_pushed.empty(), "\"both\": nothing before the UI thread comes");
  chatline::on_ui();
  check(g_pushed.size() == 1 && g_pushed[0].text == "Lo Rune dropped" && g_screen.size() == 1 &&
            g_screen[0] == "255,168,0 Lo Rune dropped",
        "\"both\": the line in the chat and the pop-up on screen, once each");
  chatline::on_ui();
  check(g_pushed.size() == 1 && g_screen.size() == 1, "... and neither a second time");
  reset();
  chatline::set_mode(chatline::kBoth);
  g_push_answer = 0;
  chatline::announce("Sur Rune dropped", chatline::kOrange);
  chatline::on_ui();
  check(g_pushed.size() == 1 && g_screen.size() == 1, "\"both\", the chat refusing: on screen once, not twice");
  reset();
  chatline::set_mode(chatline::kBoth);
  g_level = 0;
  chatline::announce("Um Rune dropped", chatline::kOrange);
  chatline::on_ui();
  check(g_pushed.empty() && g_screen.size() == 1, "\"both\", not in a game: on screen once, the chat not asked");
  reset();
  chatline::set_mode(chatline::kBoth);
  chatline::announce("Pul Rune dropped", chatline::kOrange);
  Sleep(2700);
  chatline::overdue();
  chatline::on_ui();
  check(g_pushed.empty() && g_screen.size() == 1, "\"both\", the UI thread never coming: on screen once");
  reset();
  chatline::set_mode(7);
  check(chatline::mode() == chatline::kChat, "a mode that is none of the four counts as \"chat\"");

  std::printf("8. a line with a delay (the greeting)\n");
  reset();
  chatline::announce("Drop Marker is on", chatline::kGold, 300);
  chatline::on_ui();
  chatline::overdue();
  check(g_pushed.empty() && g_screen.empty(), "not said before its time");
  Sleep(350);
  chatline::overdue();
  check(g_screen.empty(), "not overdue the moment its time has come");
  chatline::on_ui();
  check(g_pushed.size() == 1 && g_pushed[0].color == 4, "said in gold once its time has come");

  std::printf("9. the UI thread never comes\n");
  reset();
  chatline::announce("Zod Rune dropped", chatline::kOrange);
  Sleep(1000);
  chatline::overdue();
  check(g_screen.empty(), "not given up on after a second");
  Sleep(1700);
  chatline::overdue();
  check(g_screen.size() == 1 && g_pushed.empty(), "after 2.5 s it is said on screen instead");
  chatline::on_ui();
  check(g_pushed.empty(), "and not in the chat as well");

  std::printf("10. a game ends, and many lines at once\n");
  reset();
  chatline::announce("Ith Rune dropped", chatline::kOrange);
  chatline::clear();
  chatline::on_ui();
  check(g_pushed.empty() && g_screen.empty(), "what waited when the game ended is dropped");
  reset();
  for (int i = 0; i < 10; ++i) {
    char t[32];
    std::snprintf(t, sizeof(t), "rune %d", i);
    chatline::announce(t, chatline::kOrange);
  }
  check(g_screen.size() == 2 && g_screen[0] == "255,168,0 rune 0" && g_screen[1] == "255,168,0 rune 1",
        "of ten waiting lines the two oldest give way, on screen");
  chatline::on_ui();
  check(g_pushed.size() == 8 && g_pushed.front().text == "rune 2" && g_pushed.back().text == "rune 9",
        "the other eight reach the chat, in order");

  std::printf("11. a long line\n");
  reset();
  const std::string longer(400, 'x');
  chatline::announce(longer.c_str(), chatline::kWhite);
  chatline::on_ui();
  check(g_pushed.size() == 1 && g_pushed[0].text.size() == 159, "cut to 159 bytes, well under the routine's 305");

  std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "all passed", g_failed);
  return g_failed ? 1 : 0;
}
