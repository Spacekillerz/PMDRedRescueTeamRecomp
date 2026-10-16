#!/usr/bin/env bash
# Regenerates the recompiled C++ for the BIOS and the cartridge.
# Run again after every reviewed change to game.toml.
source "$(dirname "$0")/common.sh"

"$ROOT/scripts/verify-inputs.sh"

RECOMPILE="$TOOLS_BUILD/gba_recompile"
[[ -x "$RECOMPILE" ]] || { echo "run scripts/bootstrap.sh first" >&2; exit 1; }

"$RECOMPILE" --bios "$BIOS" --config "$GBARECOMP/bios/gba_bios.toml" \
    --out "$ROOT/generated_bios"

"$RECOMPILE" --rom "$ROM" --config "$ROOT/game.toml" --out "$ROOT/generated"
