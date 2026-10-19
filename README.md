# PMDRedRescueTeamRecomp

*Pokemon Mystery Dungeon: Red Rescue Team* (GBA, USA) running natively on Linux,
the Steam Deck and Windows: the game's machine code is statically recompiled
into a native program with [gbarecomp], not emulated.

> **Preview.** The game is playable, but not every code path is recompiled ahead
> of time yet. Code the recompiler has not reached runs through a built-in
> interpreter the first time, is recompiled on the fly and cached, so the game
> gets faster the more it is played. Steam Deck and Windows support are designed
> for but not yet validated on that hardware.

**No game data is included or distributed.** You supply your own dump of the
cartridge and of the GBA BIOS; the game is compiled from them on your machine.

## Play

### What you need

| File      | Expected dump                                                  | SHA-1                                      |
|-----------|----------------------------------------------------------------|--------------------------------------------|
| ROM       | Pokemon Mystery Dungeon - Red Rescue Team (USA, Australia), `.gba`, not zipped | `9f4cfc5b5f4859d17169a485462e977c7aac2b89` |
| GBA BIOS  | `gba_bios.bin`, 16 KB                                          | `300c20df6731a33952ded8c436f7f186d25d3492` |

Any other revision or region is refused: the recompiled code is tied to this
exact ROM.

### Install (Linux, Steam Deck)

Download `PMDRedRescueTeamRecomp-linux-x86_64-<version>.flatpak` from
[Releases](../../releases) and install it. On a Steam Deck, do this in Desktop
Mode, from Konsole:

```sh
flatpak install --user ./PMDRedRescueTeamRecomp-linux-x86_64-*.flatpak
```

The first install also fetches the Freedesktop SDK (about 1 GB): it provides the
compiler that builds the game on your machine.

### First launch

1. Put the ROM and the BIOS where they are found automatically. File names do not
   matter, they are recognized by content:
   - `~/Emulation/roms/gba/` and `~/Emulation/bios/` (EmuDeck's layout, also on an
     SD card);
   - or anywhere in `~/Downloads` or your home folder.
2. Launch **PMD Red Rescue Team Recomp**. Both files show as *Verified*.
3. Press **Build and play**. The game compiles once: about 1 minute on a desktop
   PC, 5 to 8 minutes on a Steam Deck.

Every later launch starts the game directly.

### Windows

Download `PMDRedRescueTeamRecomp-windows-x64-<version>.zip` from
[Releases](../../releases) and extract the whole folder anywhere writable: it
carries its own compiler (w64devkit) in `toolchain\`, so nothing needs to be
installed. Run `PMDRedRescueTeamBuilder.exe`; the program is not code-signed, so
SmartScreen may ask you to confirm (*More info* > *Run anyway*).

The ROM and BIOS are found in `Emulation\roms\gba` and `Emulation\bios` on any
drive (EmuDeck), in Downloads, Documents or Desktop, or picked with *Choose
file*. Builds, saves and states live in `%LOCALAPPDATA%\PMDRed`.

### Steam Deck

After installing in Desktop Mode, open Steam > Games > *Add a Non-Steam Game to
My Library*, tick **PMD Red Rescue Team Recomp**, and add it. In Gaming Mode the
first-launch screen works with the controller alone: press A on *Build and play*.

### Controls

GBA buttons follow QWERTY key positions (B is W on an AZERTY keyboard); system
shortcuts follow the printed key.

| Action                  | Keyboard                  | Controller |
|-------------------------|---------------------------|------------|
| A / B                   | X / Z                     | A / B      |
| L / R                   | C / V                     | LB / RB    |
| Start / Select          | Enter / Right Shift       | Start / Back |
| D-pad                   | Arrow keys                | D-pad      |
| Settings menu           | Escape                    | Guide      |
| Fullscreen              | F11                       | R3         |
| Pause                   | P                         | L3         |
| Rewind / fast-forward   | 1 / 2 (held)              | LT / RT    |
| Load / save state       | F1–F9 / Shift + F1–F9     |            |
| Volume up / down        | Page Up / Page Down       |            |
| Window bigger / smaller | Ctrl + Page Up / Down     |            |

### Where things live

Everything the app writes stays in
`~/.var/app/io.github.roblittiere.PMDRedRescueTeamRecomp/data/PMDRedRescueTeamRecomp/`:
the compiled game (`src/`), your saves and save states (`saves/`), and the build
log (`build.log`). Your ROM and BIOS are only read, never copied or modified.

- **Update:** install the new release; the next launch recompiles once, usually
  in seconds.
- **Uninstall:** `flatpak uninstall io.github.roblittiere.PMDRedRescueTeamRecomp`,
  then delete the folder above to remove saves too.

### Troubleshooting

- **ROM or BIOS not found:** check it is the unzipped USA `.gba`, and run
  `flatpak run io.github.roblittiere.PMDRedRescueTeamRecomp --scan` to see what
  the search finds.
- **Choose file does nothing:** the file dialog is not available inside the
  Flatpak sandbox yet; move the files into one of the folders above.
- **Build fails:** the last lines of `build.log` (path above) say why; attach it
  to an issue.

## Legal

This repository contains no ROM, no BIOS, no decompiled game source and no
recompiled game code: only original recompiler, runtime and build code. You must
use your own legally obtained dumps. Pokemon and Pokemon Mystery Dungeon are
trademarks of Nintendo, Creatures, GAME FREAK, The Pokemon Company and Spike
Chunsoft; this is an unaffiliated, non-commercial project.

## Development

### Layout

| Path                  | Role                                                 |
|-----------------------|------------------------------------------------------|
| `gbarecomp/`          | Recompiler + runtime (submodule, upstream mstan)     |
| `recomp-ui/`          | Launcher and in-game menu (submodule)                |
| `game.toml`           | Per-game recompiler config: the file we iterate on   |
| `src/main.cpp`        | Host entry point, runtime options                    |
| `tools/builder/`      | First-run builder app (the released artifact)        |
| `packaging/`          | Flatpak manifest template, icon, release notes       |
| `scripts/`            | bootstrap, regen, build, run, package, verify-inputs |
| `patches/`            | Local gbarecomp patches, applied by bootstrap        |
| `config/config.ini`   | Default hotkeys, seeded next to the executable       |
| `roms/`, `bios/`      | Your own dumps (git-ignored)                         |
| `generated/`          | Recompiled C++ (git-ignored, always regenerated)     |

### Build and run locally

Fedora: `sudo dnf install clang cmake ninja-build SDL2-devel ccache`

```sh
git clone --recurse-submodules https://github.com/robLittiere/PMDRedRescueTeamRecomp
scripts/bootstrap.sh       # once: apply patches, build the gba_recompile tool
scripts/verify-inputs.sh   # check roms/pmd_red.gba and bios/gba_bios.bin
scripts/regen.sh           # recompile BIOS + cartridge into C++
scripts/build.sh           # compile the native executable
scripts/run.sh             # play; writes the coverage report
```

`scripts/package.sh` makes a portable folder of your own build. It embeds code
translated from your ROM: it is for your own use and must never be shared.

### The coverage loop

Code the static analyzer could not find (indirect calls, jump tables, code copied
to RAM) is run by the interpreter the first time it is reached, then logged:

- `recomp_coverage_B24E.json`: `"coverage"` must reach fully static;
- `recomp_master_misses_B24E.toml.frag`: proposed `[[extra_func]]`,
  `[[code_copy]]` and `[[jump_table]]` entries.

Review each proposal against the disassembly and the
[pret/pmd-red](https://github.com/pret/pmd-red) decomp, copy the genuine ones into
`game.toml`, then regen, build and run again. Bump `app_version` in
`tools/builder/builder_core.cpp` so players recompile.

### The builder and releases

A recompiled binary is translated ROM code, so only the builder is released: it
configures with `-DGBAGAME_BUILDER_ONLY=ON`, which needs no ROM.

```sh
cmake -S . -B build-builder -G Ninja -DGBAGAME_BUILDER_ONLY=ON
cmake --build build-builder && build-builder/test_builder_core
scripts/package_flatpak.sh   # flatpak-build/PMDRedRescueTeamRecomp.flatpak
```

CI builds and tests the builder on every push, on Linux and on Windows with the
toolchain the zip ships. Pushing a `v*` tag builds the Flatpak (Flathub SDK
container) and the Windows zip (`scripts/package_windows.sh`: w64devkit 2.10.0
trimmed to 64-bit C/C++, SDL2 2.32.10, both pinned by SHA-256) and publishes them
with one `SHA256SUMS.txt`; a tag with a `-` (`v0.1.0-beta.1`) is a pre-release.

## License and credits

[PolyForm Noncommercial 1.0.0](LICENSE), like gbarecomp. Built on
[gbarecomp] and [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) by
mstan; the builder is adapted from [BoktaiRecomp](https://github.com/Shy/BoktaiRecomp)
by Shy.

[gbarecomp]: https://github.com/mstan/gbarecomp
