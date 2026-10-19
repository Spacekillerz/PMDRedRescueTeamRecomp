#!/usr/bin/env bash
# Builds the ROM-free Windows release: windows-build/PMDRedRescueTeamRecomp-windows-x64.zip
#
# Runs in Git Bash on Windows (the GitHub windows runner). The builder and the
# recompiler are compiled with the very toolchain the zip ships, so a green run
# proves the player's compiler can build this project.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/windows-build"
CACHE="$OUT/cache"
TC="$OUT/toolchain"
BUILD="$OUT/b"
STAGE="$OUT/PMDRedRescueTeamRecomp"
ZIP="$OUT/PMDRedRescueTeamRecomp-windows-x64.zip"

W64DEVKIT_URL="https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x64-2.10.0.7z.exe"
W64DEVKIT_SHA256="18d0a4c71a166f8401ab6305781bec5882b40b5e06ba9807c61cb5f3b3c6325e"
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.zip"
SDL2_SHA256="f15cff5fca62ec9381a016ef1d42a95c638cd72d2f226ba5781c76fe43dbd1ac"
SDL2_DIR="SDL2-2.32.10"
TOMLPP_URL="https://raw.githubusercontent.com/marzer/tomlplusplus/30172438cee64926dc41fdd9c11fb3ba5b2ba9de/toml.hpp"
TOMLPP_SHA256="6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e"

fetch() {
    local url="$1" sha="$2" dest="$CACHE/$(basename "$1")"
    mkdir -p "$CACHE"
    [[ -f "$dest" ]] || curl -fsSL -o "$dest" "$url"
    echo "$sha  $dest" | sha256sum -c - >/dev/null || { echo "checksum mismatch: $url" >&2; exit 1; }
    printf '%s' "$dest"
}

echo "==> toolchain"
rm -rf "$TC" "$BUILD" "$STAGE" "$ZIP"
mkdir -p "$TC/SDL2"
7z x -y -bd -o"$TC" "$(fetch "$W64DEVKIT_URL" "$W64DEVKIT_SHA256")" >/dev/null
SDL_TMP="$OUT/sdl2-extract"
rm -rf "$SDL_TMP" && mkdir -p "$SDL_TMP"
unzip -q "$(fetch "$SDL2_URL" "$SDL2_SHA256")" -d "$SDL_TMP"
mv "$SDL_TMP/$SDL2_DIR/x86_64-w64-mingw32" "$TC/SDL2/"
cp "$SDL_TMP/$SDL2_DIR/LICENSE.txt" "$TC/SDL2/LICENSE.txt"
rm -rf "$SDL_TMP"
[[ -x "$TC/w64devkit/bin/g++.exe" && -x "$TC/w64devkit/bin/cmake.exe" && -x "$TC/w64devkit/bin/ninja.exe" ]] ||
    { echo "w64devkit layout changed" >&2; exit 1; }

# gbarecomp fetches toml++ at configure time; the player's build has no reason
# to need the network, so the pinned header ships with the sources.
mkdir -p "$ROOT/gbarecomp/third_party/tomlpp"
cp "$(fetch "$TOMLPP_URL" "$TOMLPP_SHA256")" "$ROOT/gbarecomp/third_party/tomlpp/toml.hpp"

echo "==> build the builder and the recompiler with the shipped toolchain"
export PATH="$TC/w64devkit/bin:$TC/SDL2/x86_64-w64-mingw32/bin:$PATH"
export CC=gcc CXX=g++
SDL_PREFIX="$(cygpath -m "$TC/SDL2/x86_64-w64-mingw32")"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DGBAGAME_BUILDER_ONLY=ON -DGBAGAME_RECOMP_UI=ON \
    -DCMAKE_PREFIX_PATH="$SDL_PREFIX" -DCMAKE_EXE_LINKER_FLAGS=-static
cmake --build "$BUILD" --target PMDRedRescueTeamBuilder test_builder_core gba_recompile
"$BUILD/test_builder_core.exe"

echo "==> stage"
mkdir -p "$STAGE/sources/bin"
cp "$BUILD/PMDRedRescueTeamBuilder.exe" "$STAGE/"
cp "$TC/SDL2/x86_64-w64-mingw32/bin/SDL2.dll" "$STAGE/"
cp "$(find "$BUILD" -name gba_recompile.exe -print -quit)" "$STAGE/sources/bin/"
cp "$ROOT/LICENSE" "$STAGE/"

# Only tracked files ship, plus the pinned toml++ header; never the nested
# Android SDL checkout.
( cd "$ROOT" && git ls-files --recurse-submodules ) |
    grep -v '^gbarecomp/platform/android/' |
    while IFS= read -r f; do
        [[ -f "$ROOT/$f" ]] || continue
        mkdir -p "$STAGE/sources/$(dirname "$f")"
        cp "$ROOT/$f" "$STAGE/sources/$f"
    done
mkdir -p "$STAGE/sources/gbarecomp/third_party/tomlpp"
cp "$ROOT/gbarecomp/third_party/tomlpp/toml.hpp" "$STAGE/sources/gbarecomp/third_party/tomlpp/"
mv "$TC" "$STAGE/toolchain"

cat > "$STAGE/README.txt" <<'EOF'
Pokemon Mystery Dungeon: Red Rescue Team - native PC build (Windows)

This download contains no game data. It builds the game on your PC from your
own dumps:
  - ROM: Pokemon Mystery Dungeon - Red Rescue Team (USA, Australia).gba
         SHA-1 9f4cfc5b5f4859d17169a485462e977c7aac2b89
  - GBA BIOS: gba_bios.bin, SHA-1 300c20df6731a33952ded8c436f7f186d25d3492

1. Keep this whole folder together (the program needs toolchain\ and sources\).
2. Run PMDRedRescueTeamBuilder.exe. If Windows SmartScreen warns, choose
   "More info" then "Run anyway": the program is not code-signed.
3. Your ROM and BIOS are found automatically in Emulation\roms\gba and
   Emulation\bios (EmuDeck), Downloads, Documents or Desktop; otherwise use
   "Choose file...".
4. Press "Build and play". The game compiles once (a few minutes), then every
   later launch starts it directly.

Your builds, saves and save states live in %LOCALAPPDATA%\PMDRed.
EOF

echo "==> check the payload"
BAD="$(find "$STAGE" \( -iname '*.gba' -o -iname '*.agb' -o -iname '*.sav' -o -iname gba_bios.bin \
    -o -path "$STAGE/sources/generated" -o -path "$STAGE/sources/generated_bios" \
    -o -name 'recompiled_*' -o -name 'bios_recompiled.*' -o -name bios_symbol_map.cpp \
    -o -name CMakeCache.txt -o -name .git \) -print -quit)"
[[ -z "$BAD" ]] || { echo "protected data reached the payload: $BAD" >&2; exit 1; }
SOURCES_MB="$(du -sm "$STAGE/sources" | cut -f1)"
(( SOURCES_MB < 80 )) || { echo "sources are ${SOURCES_MB} MB: something untracked leaked in" >&2; exit 1; }

echo "==> zip"
( cd "$OUT" && 7z a -tzip -mx=7 -bd "$ZIP" "$(basename "$STAGE")" >/dev/null )
ls -la "$ZIP"
