#!/usr/bin/env bash
# Runs the game from the repo root, where game.toml and saves/ live.
# The coverage report and miss proposals are written next to it.
source "$(dirname "$0")/common.sh"

cd "$ROOT"
mkdir -p saves
exec "$GAME_BUILD/PMDRedRescueTeamRecomp" --bios "$BIOS" --rom "$ROM" "$@" game.toml
