# PMDRedRescueTeamRecomp

Native static recompilation of *Pokemon Mystery Dungeon: Red Rescue Team*
(GBA, USA/Australia, `B24E`) built on [gbarecomp].

The game's ARM/Thumb machine code is translated ahead of time into C++,
compiled with a normal compiler, and linked against gbarecomp's GBA hardware
runtime (PPU, audio, DMA, timers, IRQs, flash save). No ROM, BIOS or
ROM-derived code is stored in this repository.

## Layout

| Path                  | Role                                                 |
|-----------------------|------------------------------------------------------|
| `gbarecomp/`          | Recompiler + runtime (submodule, upstream mstan)     |
| `recomp-ui/`          | Optional launcher and in-game menu (submodule)       |
| `game.toml`           | Per-game recompiler config: the file we iterate on   |
| `src/main.cpp`        | Host entry point, runtime options                    |
| `scripts/`            | bootstrap, regen, build, run, package, verify-inputs |
| `patches/`            | Local gbarecomp patches, applied by bootstrap        |
| `tools/builder/`      | First-run builder app (the released artifact)        |
| `packaging/`          | Flatpak manifest template and app icon               |
| `config/config.ini`   | Default hotkeys, seeded next to the executable       |
| `roms/`, `bios/`      | Your own dumps (git-ignored)                         |
| `generated/`          | Recompiled C++ (git-ignored, always regenerated)     |
| `../pmd-red/`         | `pret/pmd-red` decomp, reference for names/layout    |

## Requirements

Fedora: `sudo dnf install clang cmake ninja-build SDL2-devel ccache mgba`

## Workflow

```sh
scripts/bootstrap.sh       # once: build the gba_recompile tool
scripts/verify-inputs.sh   # check roms/pmd_red.gba and bios/gba_bios.bin
scripts/regen.sh           # recompile BIOS + cartridge into C++
scripts/build.sh           # compile the native executable
scripts/run.sh             # play; writes the coverage report
scripts/package.sh         # portable folder + tarball in release-stage/
```

The package embeds code translated from your ROM: it is for your own use and
must never be shared or published.

## Builder and Flatpak (what gets released)

A recompiled binary is translated ROM code, so it is never distributed. The
release is `PMDRedRescueTeamBuilder` instead: it contains no game data, finds the
player's ROM and BIOS, verifies their SHA-1, recompiles and compiles the game
once (a few minutes), then launches it directly on every later start.

- ROM and BIOS are found automatically in EmuDeck's layout (`~/Emulation/roms/gba`,
  `~/Emulation/bios`, also on an SD card), `~/Downloads` and the home folder, so a
  Steam Deck in Gaming Mode only needs the A button.
- The Flatpak's runtime is the Freedesktop SDK, which brings gcc, CMake and Ninja
  to an immutable Steam Deck root.
- Everything the app writes (build, saves, states) stays in
  `~/.var/app/io.github.roblittiere.PMDRedRescueTeamRecomp/`; the player's files are
  mounted read-only.

```sh
cmake -S . -B build-builder -G Ninja -DGBAGAME_BUILDER_ONLY=ON   # proves it needs no ROM
cmake --build build-builder && build-builder/test_builder_core
build-builder/PMDRedRescueTeamBuilder --help
scripts/package_flatpak.sh   # flatpak-build/PMDRedRescueTeamRecomp.flatpak
```

Install on a Steam Deck (Desktop Mode), then add it to Steam as a non-Steam game:

```sh
flatpak install --user ./PMDRedRescueTeamRecomp.flatpak
```

The builder is adapted from [BoktaiRecomp](https://github.com/Shy/BoktaiRecomp)'s,
Copyright (c) 2026 Shy, under the PolyForm Noncommercial License 1.0.0.

### Controls

Keyboard (GBA buttons follow QWERTY positions; hotkeys follow printed keys):

| Action                  | Key                       | Controller |
|-------------------------|---------------------------|------------|
| A / B                   | X / Z (W on AZERTY)       | A / B      |
| L / R                   | C / V                     | LB / RB    |
| Start / Select          | Enter / Right Shift       | Start/Back |
| Fullscreen              | F11                       | R3         |
| Pause                   | P                         | L3         |
| Volume up / down        | Page Up / Page Down       |            |
| Window bigger / smaller | Ctrl + Page Up / Down     |            |
| Rewind / fast-forward   | 1 / 2 (held)              | LT / RT    |
| Load / save state       | F1–F9 / Shift + F1–F9     |            |
| Settings menu           | Escape                    | Guide      |

### The coverage loop

Code the static analyzer could not find (indirect calls, jump tables, code
copied to RAM) is run by the interpreter the first time it is reached, then
logged:

- `recomp_coverage_B24E.json`: `"coverage"` must reach fully static;
- `recomp_master_misses_B24E.toml.frag`: proposed `[[extra_func]]`,
  `[[code_copy]]` and `[[jump_table]]` entries.

Review each proposal (disassembly, the decomp in `../pmd-red`), copy the
genuine ones into `game.toml`, then regen, build and run again. Play
different parts of the game to reach new code. Repeat until no miss is left.

[gbarecomp]: https://github.com/mstan/gbarecomp
