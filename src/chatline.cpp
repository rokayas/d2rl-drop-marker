// A line in the game's own chat (chatline.h): a local addition, not CabbyCodes' own.
#include "chatline.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "game.h"
#include "log.h"
#include "mem.h"
#include "toast.h"

namespace d2rcc::chatline {
namespace {

// D2RLoader 1.3.1-beta's D2RCore.dll puts its own notices into the chat with one routine:
//
//   uint64_t push(uint32_t kind, uint32_t color, uint32_t unit_id, const char* text)
//
// It copies the text (up to 305 bytes), asks the game for its chat, hands the game's own chat routine the line with
// no sender, and answers 0 where there is no chat or the text is too long. `kind` is only written to the loader's log;
// unit_id 0 is a line nobody says. The loader calls it with (0, 4, 0, text) for a notice in gold and (0, 1, 0, text)
// for one in red.
//
// The routine is not exported. The export PushClientChatEntry is - the game's own client messages, which the loader
// reimplements - and it ends in a call of the routine, at kPushCallAt, with (0, color, 0, text). So the export is
// looked up by name, compared with kExportShape, its call followed, and what it leads to compared with kPushShape.
// Both shapes are the code this was written against with every address the linker fills in left open (the rel32 of a
// call or a jump, the displacement of a RIP-relative operand): a loader whose code differs in anything else is not
// called, and the lines are said on screen.
constexpr const char* kExport = "PushClientChatEntry";
constexpr size_t kPushCallAt = 0x89;  // E8 rel32
constexpr const char* kExportShape =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 38 02 00 00 48 8D AC 24 80 00 00 00 48 C7 85 B0 01 "
    "00 00 FE FF FF FF 4C 89 CE 89 D3 48 89 CF 4C 8B BD 30 02 00 00 4C 8B A5 28 02 00 00 4C 8B AD 20 "
    "02 00 00 4C 8D 75 BE 41 B8 32 01 00 00 4C 89 F1 31 D2 E8 ?? ?? ?? ?? 4C 89 7C 24 30 4C 89 64 24 "
    "28 4C 89 6C 24 20 BA 32 01 00 00 4C 89 F1 49 89 F8 49 89 F1 E8 ?? ?? ?? ?? 84 C0 74 16 0F B6 D3 "
    "4C 8D 4D BE 31 C9 45 31 C0 E8 ?? ?? ?? ?? E9 ?? ?? ?? ??";
constexpr const char* kPushShape =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 18 02 00 00 48 8D AC 24 80 00 00 00 48 C7 85 90 01 "
    "00 00 FE FF FF FF 4D 89 CE 44 89 C6 89 D7 89 CB 4C 8D 7D E8 4C 89 7D D0 48 C7 45 D8 00 00 00 00 "
    "41 B8 32 01 00 00 4C 89 F9 31 D2 E8 ?? ?? ?? ?? 49 BC 31 01 00 00 00 00 00 80 4C 89 65 E0 4D 85 "
    "F6 0F 84 ?? ?? ?? ?? 41 80 3E 00 0F 84 ?? ?? ?? ?? 4C 89 F1 E8 ?? ?? ?? ?? 48 3D 32 01 00 00 0F "
    "83 ?? ?? ?? ?? 49 89 C5 48 FF C0 4C 89 F9 4C 89 F2 49 89 C0 E8 ?? ?? ?? ?? 4C 89 7D D0 4C 89 6D "
    "D8 4C 89 65 E0 FF 15 ?? ?? ?? ?? 90 48 85 C0 0F 84 ?? ?? ?? ?? 48 8D 8D 80 01 00 00 0F 57 C0 0F "
    "11 85 80 01 00 00 48 C7 85 60 01 00 00 04 00 00 00 48 89 8D 68 01 00 00 48 C7 85 70 01 00 00 00 "
    "00 00 00 48 B9 0F 00 00 00 00 00 00 80 48 89 8D 78 01 00 00 0F 29 85 20 01 00 00 0F 29 85 30 01 "
    "00 00 0F 29 85 40 01 00 00 0F 29 85 50 01 00 00 8B 0D ?? ?? ?? ?? F7 D1 F6 C1 81 75 20 0F B6 D3 "
    "4C 89 74 24 20 48 8D 0D ?? ?? ?? ?? 41 89 F0 41 89 F9 48 89 C6 E8 ?? ?? ?? ?? 48 89 F0 C6 80 F0 "
    "3A 0C 00 01 4C 8B 15 ?? ?? ?? ?? 48 8D 8D 50 01 00 00 48 89 4C 24 40 48 8D 8D 40 01 00 00 48 89 "
    "4C 24 38 48 8D 8D 30 01 00 00 48 89 4C 24 30 48 8D 8D 20 01 00 00 48 89 4C 24 28 48 8D 8D 60 01 "
    "00 00 48 89 4C 24 20 C6 44 24 48 00 48 8D 55 D0 48 89 C1 41 89 F8 45 31 C9 41 FF D2 90 48 83 7D "
    "D8 00 74 0A C7 05 ?? ?? ?? ?? B0 04 00 00 48 83 F8 01 48 83 D0 00 EB 20 8B 05 ?? ?? ?? ?? F7 D0 "
    "A8 81 75 12 0F B6 D3 48 8D 0D ?? ?? ?? ?? 41 89 F0 E8 ?? ?? ?? ?? 31 C0 48 81 C4 18 02 00 00 5B "
    "5F 5E 41 5C 41 5D 41 5E 41 5F 5D C3 8B 05 ?? ?? ?? ?? F7 D0 A8 81 75 DE 0F B6 D3 48 8D 0D ?? ?? "
    "?? ?? EB CA";

using PushFn = uint64_t (*)(uint32_t kind, uint32_t color, uint32_t unit_id, const char* text);

PushFn volatile g_push = nullptr;
const char* volatile g_why = "not looked for yet";
volatile LONG g_mode = kChat;

struct Line {
  char text[160];
  uint8_t color;
  bool chat;       // the chat is asked (the mode has it, and the loader's routine is there)
  bool screen;     // it goes on screen too, whatever the chat does with it
  ULONGLONG due;   // not said before this (GetTickCount64)
};
constexpr int kMaxLines = 8;
constexpr ULONGLONG kOverdueMs = 2500;  // the UI thread did not come for a line: it is said on screen

SRWLOCK g_lock = SRWLOCK_INIT;
Line g_lines[kMaxLines];
int g_count = 0;
volatile LONG g_waiting = 0;
volatile long g_said_logged = 0, g_refused_logged = 0, g_overdue_logged = 0;

// The bytes at `at` against a pattern (mem.h): every byte but the wildcards.
bool shaped(uintptr_t at, const char* shape) {
  const mem::Pattern p = mem::parse_pattern(shape);
  std::vector<uint8_t> have(p.bytes.size());
  if (have.empty() || !mem::copy_from(have.data(), at, have.size())) return false;
  for (size_t i = 0; i < have.size(); ++i)
    if (p.bytes[i] >= 0 && have[i] != static_cast<uint8_t>(p.bytes[i])) return false;
  return true;
}

void to_screen(const Line& l) {
  struct Rgb {
    uint8_t r, g, b;
  };
  const Rgb c = l.color == kRed      ? Rgb{255, 90, 90}
                : l.color == kGreen  ? Rgb{0, 230, 0}
                : l.color == kBlue   ? Rgb{120, 120, 255}
                : l.color == kGold   ? Rgb{199, 179, 119}
                : l.color == kGrey   ? Rgb{150, 150, 150}
                : l.color == kTan    ? Rgb{208, 194, 125}
                : l.color == kOrange ? Rgb{255, 168, 0}
                : l.color == kYellow ? Rgb{255, 255, 100}
                                     : Rgb{255, 255, 255};
  toast::push(l.text, c.r, c.g, c.b);
}

// The UI thread: one line - into the chat, onto the screen, or both, as it asks. It goes on screen once at the most:
// where that was asked for, and where the chat was asked and did not take it.
void say_now(const Line& l) {
  const PushFn push = g_push;
  const bool in_game = game::current_level() > 0;
  const bool said = l.chat && push && in_game && push(0, l.color, 0, l.text) != 0;
  if (said && log_first(&g_said_logged, 16))
    logf("chat: \"%s\" said in the game's chat%s", l.text, l.screen ? ", and on screen" : "");
  if (l.screen || !said) to_screen(l);
  if (l.chat && push && !said && log_first(&g_refused_logged, 4, LogLevel::kWarning))
    log_warn("chat: the game's chat did not take \"%s\" (%s) - said on screen instead", l.text,
             in_game ? "the loader's routine answered no" : "not in a game");
}

}  // namespace

#ifdef D2RCC_CHATLINE_TEST
// The test's stand-in for the loader's routine (tests/test_chatline.cpp); not in any build of the plugin.
void set_push_for_test(uint64_t (*fn)(uint32_t, uint32_t, uint32_t, const char*)) { g_push = fn; }
#endif

void set_mode(int mode) { InterlockedExchange(&g_mode, mode >= kOff && mode <= kBoth ? mode : kChat); }
int mode() { return static_cast<int>(g_mode); }

void bind() {
  const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
  const uintptr_t base = reinterpret_cast<uintptr_t>(core);
  const uintptr_t entry = core ? reinterpret_cast<uintptr_t>(GetProcAddress(core, kExport)) : 0;
  const char* why = nullptr;
  uintptr_t push = 0;
  if (!core) {
    why = "D2RCore.dll is not loaded";
  } else if (!entry) {
    why = "this loader's D2RCore.dll exports no PushClientChatEntry";
  } else if (!shaped(entry, kExportShape)) {
    why = "this loader's PushClientChatEntry is not the code the plugin was written against";
  } else {
    push = mem::rip_target(entry + kPushCallAt, 1, 5);
    if (!mem::module_range(base).contains(push) || !shaped(push, kPushShape)) {
      why = "this loader's chat routine is not the code the plugin was written against";
      push = 0;
    }
  }
  g_why = why;
  g_push = reinterpret_cast<PushFn>(push);
  const IMAGE_NT_HEADERS* nt = core ? mem::nt_headers(base) : nullptr;
  const unsigned long stamp = nt ? nt->FileHeader.TimeDateStamp : 0;
  if (why)
    log_warn("chat: %s (D2RCore.dll built 0x%08lX) - lines are said on screen instead", why, stamp);
  else
    logf("chat: lines go into the game's chat through the loader's own routine (D2RCore.dll+0x%llX, found through "
         "its PushClientChatEntry at +0x%llX; D2RCore.dll built 0x%08lX)",
         static_cast<unsigned long long>(push - base), static_cast<unsigned long long>(entry - base), stamp);
}

bool available() { return g_push != nullptr; }
const char* why_not() { return g_why; }

void announce(const char* text, uint8_t color, uint32_t delay_ms) {
  const LONG mode = g_mode;
  if (!text || !*text || mode == kOff) return;
  Line l{};
  std::snprintf(l.text, sizeof(l.text), "%s", text);
  l.color = color;
  l.chat = (mode & kChat) != 0 && g_push != nullptr;
  l.screen = (mode & kScreen) != 0;
  l.due = GetTickCount64() + delay_ms;
  if (!l.chat && !delay_ms) {
    // Nothing to wait for: it goes on screen now - asked for there, or the chat asked for and out of reach.
    to_screen(l);
    return;
  }
  Line pushed_out{};
  bool full = false;
  AcquireSRWLockExclusive(&g_lock);
  if (g_count == kMaxLines) {  // the oldest gives way, and is said on screen
    full = true;
    pushed_out = g_lines[0];
    std::memmove(&g_lines[0], &g_lines[1], sizeof(Line) * (kMaxLines - 1));
    --g_count;
  }
  g_lines[g_count++] = l;
  InterlockedExchange(&g_waiting, 1);
  ReleaseSRWLockExclusive(&g_lock);
  if (full) to_screen(pushed_out);
}

void on_ui() {
  if (!g_waiting) return;
  const ULONGLONG now = GetTickCount64();
  Line due[kMaxLines];
  int n = 0;
  AcquireSRWLockExclusive(&g_lock);
  int kept = 0;
  for (int i = 0; i < g_count; ++i) {
    if (g_lines[i].due <= now) due[n++] = g_lines[i];
    else g_lines[kept++] = g_lines[i];
  }
  g_count = kept;
  if (!kept) InterlockedExchange(&g_waiting, 0);
  ReleaseSRWLockExclusive(&g_lock);
  for (int i = 0; i < n; ++i) say_now(due[i]);
}

void overdue() {
  if (!g_waiting) return;
  const ULONGLONG now = GetTickCount64();
  Line late[kMaxLines];
  int n = 0;
  AcquireSRWLockExclusive(&g_lock);
  int kept = 0;
  for (int i = 0; i < g_count; ++i) {
    if (now > g_lines[i].due + kOverdueMs) late[n++] = g_lines[i];
    else g_lines[kept++] = g_lines[i];
  }
  g_count = kept;
  if (!kept) InterlockedExchange(&g_waiting, 0);
  ReleaseSRWLockExclusive(&g_lock);
  for (int i = 0; i < n; ++i) to_screen(late[i]);
  if (n && log_first(&g_overdue_logged, 2, LogLevel::kWarning))
    log_warn("chat: the UI thread did not come for a line within %.1f s - said on screen instead",
             static_cast<double>(kOverdueMs) / 1000.0);
}

void clear() {
  AcquireSRWLockExclusive(&g_lock);
  g_count = 0;
  InterlockedExchange(&g_waiting, 0);
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace d2rcc::chatline
