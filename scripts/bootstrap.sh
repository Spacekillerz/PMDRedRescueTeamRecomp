#!/usr/bin/env bash
# Builds the gbarecomp host tools (the recompiler itself).
source "$(dirname "$0")/common.sh"

git -C "$ROOT" submodule update --init --recursive

for patch in "$ROOT"/patches/gbarecomp-*.patch; do
    [[ -e "$patch" ]] || continue
    if git -C "$GBARECOMP" apply --reverse --check "$patch" 2>/dev/null; then
        echo "already applied: $(basename "$patch")"
    else
        git -C "$GBARECOMP" apply "$patch"
        echo "applied: $(basename "$patch")"
    fi
done
cmake -S "$GBARECOMP" -B "$TOOLS_BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$TOOLS_BUILD" --target gba_recompile -j"$(nproc)"
