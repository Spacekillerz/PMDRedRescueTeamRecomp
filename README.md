# PMD Red Recomp

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
