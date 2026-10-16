**This download contains no game data.** It is a builder that turns your own
legally dumped *Pokemon Mystery Dungeon: Red Rescue Team* (USA) ROM and GBA BIOS
into a native Linux game, on your machine, on first launch.

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

| File      | SHA-1                                      |
|-----------|--------------------------------------------|
| ROM (USA) | `9f4cfc5b5f4859d17169a485462e977c7aac2b89` |
| GBA BIOS  | `300c20df6731a33952ded8c436f7f186d25d3492` |

`SHA256SUMS.txt` lists the hash of the download.
