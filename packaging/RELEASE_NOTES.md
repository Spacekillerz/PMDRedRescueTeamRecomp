**This download contains no game data.** It is a builder that turns your own
legally dumped *Pokemon Mystery Dungeon: Red Rescue Team* (USA) ROM and GBA BIOS
into a native Linux game, on your machine, on first launch.

### What's new

- **Windows: no more console windows during the build.** The first build used
  to pop up a command prompt window for each step; it now runs in the
  background, with its progress in the builder's window.
- The Windows builder has been confirmed working on a real Windows machine.
- The README and these notes show how to pass launch options on Windows.
- Updating from beta.4 does not recompile the game.

Since beta.3: widescreen dungeon floors (terrain, Pokémon, status icons and
items drawn in the side margins with `--widescreen`).
Since beta.2: a fix for a crash when entering a dungeon (GCC-built copies).
Since beta.1: the Windows builder, and the launch options `--fullscreen` and
`--widescreen`.

### Install (Linux desktop or Steam Deck in Desktop Mode)

```sh
flatpak install --user ./PMDRedRescueTeamRecomp-linux-x86_64-*.flatpak
```

The first install also fetches the Freedesktop SDK (about 1 GB), which provides
the compiler.

### First launch

1. Put your ROM (`.gba`, not zipped) and `gba_bios.bin` in `~/Emulation/roms/gba/`
   and `~/Emulation/bios/` (EmuDeck's layout, SD card included), or anywhere in
   `~/Downloads` or your home folder. File names do not matter: they are found by
   their SHA-1.
2. Launch **PMD Red Rescue Team Recomp**. Both files show as *Verified*.
3. Press **Build and play**. The game compiles once: about 1 minute on a desktop,
   5 to 8 minutes on a Steam Deck. Every later launch starts the game directly.

On a Steam Deck, add it to Steam (Games > Add a Non-Steam Game) to play from
Gaming Mode with the controller.

From a terminal, with the optional launch options:

```sh
flatpak run io.github.spacekillerz.PMDRedRescueTeamRecomp --fullscreen --widescreen
```

In Steam, append them to the end of the game's *Launch Options*.

### Windows

Download `PMDRedRescueTeamRecomp-windows-x64-*.zip`, extract the whole folder
(it carries its own compiler in `toolchain\`), and run
`PMDRedRescueTeamBuilder.exe`. The program is not code-signed: if SmartScreen
warns, choose *More info* then *Run anyway*. The ROM and BIOS are searched in
`Emulation\roms\gba` and `Emulation\bios` (EmuDeck), Downloads, Documents and
Desktop, or picked with *Choose file*. The first build takes a few minutes.

Launch options go after the program name, from PowerShell in that folder
(`.\PMDRedRescueTeamBuilder.exe --fullscreen --widescreen`), or at the end of a
shortcut's *Target*, after the closing quote.

| File      | SHA-1                                      |
|-----------|--------------------------------------------|
| ROM (USA) | `9f4cfc5b5f4859d17169a485462e977c7aac2b89` |
| GBA BIOS  | `300c20df6731a33952ded8c436f7f186d25d3492` |

`SHA256SUMS.txt` lists the hash of the download.
