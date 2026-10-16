#!/usr/bin/env bash
# Builds the native game executable. Extra arguments go to CMake configure,
# e.g. scripts/build.sh -DPMD_RECOMP_UI=ON
source "$(dirname "$0")/common.sh"

cmake -S "$ROOT" -B "$GAME_BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$GAME_BUILD" --target PmdRedRecomp -j"$(nproc)"
