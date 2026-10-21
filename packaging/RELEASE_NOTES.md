**This download contains no game data.** It is a builder that turns your own
legally dumped *Pokemon Mystery Dungeon: Red Rescue Team* (USA) ROM and GBA BIOS
into a native Linux game, on your machine, on first launch.

### What's new

- **Widescreen dungeons.** With `--widescreen`, dungeon floors now fill the
  extra width up to 16:9: terrain, Pokémon, their status icons and floor items
  are drawn in the side margins, and slide out at the screen edges instead of
  vanishing. Towns, menus and cutscenes keep the original 240×160 picture
  between black bars. Dark dungeon floors are not tested yet.
- Installing this version recompiles the game once, as long as a first launch
  takes.

Since beta.2: a fix for a crash when entering a dungeon (GCC-built copies).
Since beta.1: an experimental Windows builder (not yet tested on a real Windows
machine), and the launch options `--fullscreen` and `--widescreen`.

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
flatpak run io.github.roblittiere.PMDRedRescueTeamRecomp --fullscreen --widescreen
```

In Steam, append them to the end of the game's *Launch Options*.

### Windows

Download `PMDRedRescueTeamRecomp-windows-x64-*.zip`, extract the whole folder
(it carries its own compiler in `toolchain\`), and run
`PMDRedRescueTeamBuilder.exe`. The program is not code-signed: if SmartScreen
warns, choose *More info* then *Run anyway*. The ROM and BIOS are searched in
`Emulation\roms\gba` and `Emulation\bios` (EmuDeck), Downloads, Documents and
Desktop, or picked with *Choose file*. The first build takes a few minutes.

| File      | SHA-1                                      |
|-----------|--------------------------------------------|
| ROM (USA) | `9f4cfc5b5f4859d17169a485462e977c7aac2b89` |
| GBA BIOS  | `300c20df6731a33952ded8c436f7f186d25d3492` |

`SHA256SUMS.txt` lists the hash of the download.
