#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GBARECOMP="$ROOT/gbarecomp"
TOOLS_BUILD="$GBARECOMP/build"
GAME_BUILD="$ROOT/build"

ROM="$ROOT/roms/pmd_red.gba"
BIOS="$ROOT/bios/gba_bios.bin"
ROM_SHA1="9f4cfc5b5f4859d17169a485462e977c7aac2b89"
BIOS_SHA1="300c20df6731a33952ded8c436f7f186d25d3492"

GENERATOR="Ninja"
command -v ninja >/dev/null || GENERATOR="Unix Makefiles"

export CC="${CC:-clang}" CXX="${CXX:-clang++}"
command -v "$CC" >/dev/null || export CC=gcc CXX=g++

check_file() {
    local path="$1" want="$2"
    if [[ ! -f "$path" ]]; then
        echo "missing: $path" >&2
        return 1
    fi
    local got
    got="$(sha1sum "$path" | cut -d' ' -f1)"
    if [[ "$got" != "$want" ]]; then
        echo "bad hash: $path is $got, expected $want" >&2
        return 1
    fi
    echo "ok: $path"
}
