# Drop Marker

A [D2RLoader](https://d2rloader.net) plugin for Diablo II: Resurrected that tells you when something you care
about drops, and where it is.

You choose what counts: any of the 33 runes, and keys, organs, essences, Worldstone Shards and the Ancients'
statues. When one of those drops it gets

- an **icon on the minimap** for as long as it lies on the ground,
- a **line in the game's chat** ("Ber Rune dropped"), and/or
- a **pop-up on screen** for a few seconds.

Each of the three is a switch. Anything you do not track is never shown. That is all the plugin does: no
cheats, no console command, nothing about the game's rules is changed.

![The menu](docs/menu.png)

Drop Marker is a cut-down build of [CabbyCodes](https://github.com/SirCabby/D2RCabbyCodes) by SirCabby, with a
drop message and a menu added. See [Where it comes from](#where-it-comes-from).

## Install

1. Close the game.
2. Put `d2rl-rune-marker.dll` into `<game folder>\d2rloader\plugins\`.
3. Start the game through D2RLoader.

A few seconds into your first game a line says "Drop Marker is on - F7 opens its menu (what to track)".

Do not install it next to CabbyCodes itself: both want the same game routines, and the loader gives a routine
to one plugin only. CabbyCodes already has the map marks; this plugin is for people who want only that.

If you play with others over TCP/IP, everyone needs the plugin: the loader counts it among the plugins all
players of a game must share.

## Using it

Press **F7** in a game. The menu has

- the three ways a tracked drop is shown, as tick boxes;
- every rune, with **All**, **None** and "from a rune up" (pick Lem and everything from Lem to Zod is ticked);
- the other drops as groups: keys, organs, essences, Worldstone Shards, statues, and the fragments
  (Defender's, Protector's and Guardian's; off by default).

Choices are saved at once to `<game folder>\d2rloader\config\rune-marker.toml`. The file can also be edited by
hand with the game closed; its comments explain every setting, including how to move the menu to another key
and how to switch the plugin's log off. (The file and the log are named after the plugin's first name, Rune
Marker. They keep that name so existing settings carry over.)

Runes are marked with an orange rune stone and an orange line; the other drops with a gold star and a gold
line.

## What to expect

- **Tested with** D2RLoader 1.3.1-beta on the Steam release of the game, version 3.4.95041. Nothing else has
  been tried: not other loader versions, not the Battle.net release.
- The plugin finds the game routines it needs by their byte patterns and checks them before it touches
  anything. On a build where one is not found it leaves that part off and says so in its log
  (`<game folder>\d2rloader\logs\rune-marker.log`).
- The chat line goes through a routine inside D2RLoader's own `D2RCore.dll`. If a loader update changes that
  routine, the plugin notices and shows the line as a pop-up instead. The log says why.
- Overlays that take over the game's frame presentation (Steam's, Discord's, RTSS) do not stop the menu: it is
  then drawn from the game's own present routine. The log notes it once.
- A drop is announced once per game. Picking it up removes its icon.
- It hooks five routines of the game, all through the loader: the present routine (for the menu), the
  per-frame player tick, the drop routine, the item notice and the automap's marker decision.

## Uninstall

Close the game and delete `d2rl-rune-marker.dll`. The settings file can stay or go. No game file was changed.

## Building it

The plugin is built with GCC (mingw-w64), on Linux as CabbyCodes is, or on Windows with a kit such as
w64devkit:

```
make RUNES_ONLY=1
```

gives `build/runes/d2rl-rune-marker.dll`. Plain `make` still builds CabbyCodes itself from the same tree,
unchanged. The version of Drop Marker is in `VERSION.drop-marker`; `VERSION` is CabbyCodes' own.

Three tests run without the game; how to build each is at the top of its file:

- `tests/test_track.cpp` - what is tracked, and the settings file read and written back
- `tests/test_chatline.cpp` - finding and checking the loader's chat routine (needs a real `D2RCore.dll`)
- `tests/test_trackmenu.cpp` - the menu, drawn and clicked without a screen

## Where it comes from

This repository is a fork of [SirCabby/D2RCabbyCodes](https://github.com/SirCabby/D2RCabbyCodes), taken at
CabbyCodes 1.4.2 (commit `0bf3385`, 2026-11-05). CabbyCodes is an in-game cheat panel with a great deal more
in it; its own read-me is kept here as [README-CabbyCodes.md](README-CabbyCodes.md). All of that is SirCabby's
work, and so is the code Drop Marker runs on: the way routines are found and hooked, the map icons, the
overlay. (The original project's file of working notes is not carried in this fork; it is in SirCabby's
repository.)

What was changed here, in November 2026:

- a second build of the tree, `make RUNES_ONLY=1`, that compiles 20 of the 43 source files and leaves every
  cheat, the panel and the console command out;
- tracking by choice (`src/track.*`) and the menu for it (`src/trackmenu.*`);
- the line said as a tracked item drops, in the game's chat or as a pop-up (`src/chatline.*`, `src/toast.*`);
- keys, organs, essences, Worldstone Shards, statues and fragments beside the runes;
- its own plugin id, settings file, log, version and name.

Drop Marker is not SirCabby's release and he is not responsible for it. Problems with it belong in this
repository's issues, not his.

## License

GPL-3.0, as CabbyCodes is: see [LICENSE](LICENSE). The bundled libraries keep their own licenses, in
`contrib/`: Dear ImGui (MIT), MinHook (BSD 2-Clause) and the D2RLoader PluginSDK (MIT).

Not affiliated with Blizzard Entertainment.
