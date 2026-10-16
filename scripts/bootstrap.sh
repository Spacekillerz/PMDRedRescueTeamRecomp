#!/usr/bin/env bash
# Builds the gbarecomp host tools (the recompiler itself).
source "$(dirname "$0")/common.sh"

git -C "$ROOT" submodule update --init --recursive
"$ROOT/scripts/apply_patches.sh"

cmake -S "$GBARECOMP" -B "$TOOLS_BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$TOOLS_BUILD" --target gba_recompile -j"$(nproc)"
