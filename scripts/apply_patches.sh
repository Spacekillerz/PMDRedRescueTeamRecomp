#!/usr/bin/env bash
# Applies patches/gbarecomp-*.patch to the gbarecomp submodule; idempotent.
source "$(dirname "$0")/common.sh"

for patch in "$ROOT"/patches/gbarecomp-*.patch; do
    [[ -e "$patch" ]] || continue
    if git -C "$GBARECOMP" apply --reverse --check "$patch" 2>/dev/null; then
        echo "already applied: $(basename "$patch")"
    else
        git -C "$GBARECOMP" apply "$patch"
        echo "applied: $(basename "$patch")"
    fi
done
