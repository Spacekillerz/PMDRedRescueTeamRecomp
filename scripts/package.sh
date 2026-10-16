#!/usr/bin/env bash
# Builds a portable Linux folder + tarball under release-stage/.
# It embeds code translated from the ROM: for personal use, never redistribute.
# The ROM and BIOS are never included.
source "$(dirname "$0")/common.sh"

NAME="PMDRedRescueTeamRecomp"
"$GBARECOMP/packaging/package_linux.sh" --root "$ROOT" --target PMDRedRescueTeamRecomp --name "$NAME" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

STAGE="$ROOT/release-stage"
OUT="$STAGE/$NAME-linux-$(uname -m)"

# Fedora's libSDL2 is sdl2-compat, which dlopens libSDL3.so.0 by bare name: the
# packaging script cannot see it, so bundle it and point play.sh at it.
SDL3="$(ldconfig -p | awk '$1=="libSDL3.so.0"{print $NF; exit}')"
if [[ -n "$SDL3" ]]; then
    cp -L "$SDL3" "$OUT/libSDL3.so.0"
    echo "    + libSDL3.so.0 (dlopen'd by sdl2-compat)"
fi

cp "$ROOT/game.toml" "$OUT/game.toml"
cp "$ROOT/config/config.ini" "$OUT/config.ini"
mkdir -p "$OUT/roms" "$OUT/bios" "$OUT/saves"
cp "$ROOT/roms/README.md" "$OUT/roms/README.md"
cp "$ROOT/bios/README.md" "$OUT/bios/README.md"

cat > "$OUT/play.sh" <<'SH'
#!/usr/bin/env bash
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./PMDRedRescueTeamRecomp --bios bios/gba_bios.bin --rom roms/pmd_red.gba "$@" game.toml
SH
chmod +x "$OUT/play.sh"

( cd "$STAGE" && tar -czf "$(basename "$OUT").tar.gz" -- "$(basename "$OUT")" )
echo "==> $OUT"
echo "==> $OUT.tar.gz"
