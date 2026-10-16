#!/usr/bin/env bash
# Builds the ROM-free builder as a Flatpak bundle: flatpak-build/<name>.flatpak.
# Players install it, and it compiles the game from their own ROM and BIOS.
#
#   scripts/package_flatpak.sh                  generate, build, bundle
#   scripts/package_flatpak.sh --generate-only  manifest only (no flatpak-builder)
#
# Adapted from BoktaiRecomp's tools/package_builder_flatpak.sh, Copyright (c)
# 2026 Shy (github.com/Shy), licensed under the PolyForm Noncommercial License 1.0.0.
source "$(dirname "$0")/common.sh"

GENERATE_ONLY=0
[[ "${1:-}" == "--generate-only" ]] && GENERATE_ONLY=1

APP_ID="io.github.roblittiere.PMDRedRescueTeamRecomp"
NAME="PMD Red Rescue Team Recomp"
SUMMARY="Builds Pokemon Mystery Dungeon: Red Rescue Team natively from your own ROM"
BUNDLE="PMDRedRescueTeamRecomp.flatpak"
OUT="$ROOT/flatpak-build"
MANIFEST="$OUT/$APP_ID.yml"
mkdir -p "$OUT"

cat > "$OUT/$APP_ID.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$NAME
Comment=$SUMMARY
Exec=PMDRedRescueTeamBuilder
Icon=$APP_ID
Categories=Game;
Terminal=false
EOF

cat > "$OUT/$APP_ID.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>$APP_ID</id>
  <name>$NAME</name>
  <summary>$SUMMARY</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>LicenseRef-PolyForm-Noncommercial-1.0.0</project_license>
  <description>
    <p>Recompiles Pokemon Mystery Dungeon: Red Rescue Team (GBA, USA) into a native
    Linux game on first launch, from the player's own ROM and GBA BIOS. No game data
    is included.</p>
  </description>
  <launchable type="desktop-id">$APP_ID.desktop</launchable>
  <content_rating type="oars-1.1"/>
</component>
EOF

cp "$ROOT/packaging/icon.png" "$OUT/$APP_ID.png"

SKIP_FILE="$(mktemp)"
SKIP_YAML="$(mktemp)"
trap 'rm -f "$SKIP_FILE" "$SKIP_YAML"' EXIT

# flatpak-builder's `skip` takes exact paths, not globs, so every excluded path
# is listed as found. Root-only directories are matched by path because
# gbarecomp has its own src/runtime/generated_bios/ holding a needed source.
cd "$ROOT"
find . -mindepth 1 \
    \( -name .git -o -path ./.github -o -path ./.idea \
       -o -path ./flatpak-build -o -path ./.flatpak-builder -o -path ./release-stage \
       -o \( -type d \( -name build -o -name 'build-*' -o -name 'cmake-build-*' -o -name recomp_cache \) \) \
       -o -path ./roms -o -path ./saves -o -path ./generated -o -path ./generated_bios \
       -o -path ./gbarecomp/platform/android \) -prune -print \
    -o \( -iname '*.gba' -o -iname '*.agb' -o -name '*.sav' -o -name gba_bios.bin \
          -o -name 'recompiled_*.cpp' -o -name 'bios_recompiled.*' -o -name bios_symbol_map.cpp \
          -o -name 'recomp_coverage_*.json' -o -name 'recomp_master_misses_*.toml.frag' \) -print \
    | sed 's|^\./||' | LC_ALL=C sort -u > "$SKIP_FILE"

while IFS= read -r path; do
    printf "        - '%s'\n" "${path//\'/\'\'}" >> "$SKIP_YAML"
done < "$SKIP_FILE"

sed -e "s|@APP_ID@|$APP_ID|g" -e "s|@SOURCE_DIR@|$ROOT|g" -e "s|@SUPPORT_DIR@|$OUT|g" \
    "$ROOT/packaging/flatpak/manifest.yml.in" |
awk -v skipfile="$SKIP_YAML" '
    /^@SKIP@$/ {
        print "        skip:"
        while ((getline line < skipfile) > 0) print line
        close(skipfile)
        next
    }
    { print }
' > "$MANIFEST"

echo "==> $MANIFEST (paths excluded: $(wc -l < "$SKIP_FILE"))"
[[ "$GENERATE_ONLY" == 1 ]] && exit 0

if command -v flatpak-builder >/dev/null 2>&1; then
    BUILDER=(flatpak-builder)
elif flatpak info org.flatpak.Builder >/dev/null 2>&1; then
    BUILDER=(flatpak run --filesystem=host --share=network org.flatpak.Builder)
else
    echo "flatpak-builder is required: sudo dnf install flatpak-builder" >&2
    exit 1
fi

BUILD_DIR="$OUT/build"
REPO="$OUT/repo"
rm -rf "$BUILD_DIR" "$REPO"
# CI runs as root in a container whose SDK is installed system-wide.
FLATPAK_SCOPE="${FLATPAK_SCOPE:---user}"
"${BUILDER[@]}" "$FLATPAK_SCOPE" --install-deps-from=flathub --force-clean --state-dir="$ROOT/.flatpak-builder" \
    --repo="$REPO" "$BUILD_DIR" "$MANIFEST"

PAYLOAD="$BUILD_DIR/files/bin/sources"
# Object files and archives can only come from a stray build tree, which may
# hold a compiled game: none belong in the payload.
BAD="$(find "$BUILD_DIR/files" \( -iname '*.gba' -o -iname '*.agb' -o -name '*.sav' -o -name gba_bios.bin \
    -o -path "$PAYLOAD/generated" -o -path "$PAYLOAD/generated_bios" -o -path "$PAYLOAD/roms" \
    -o -name 'recompiled_*' -o -name 'bios_recompiled.*' -o -name bios_symbol_map.cpp \
    -o -name '*.o' -o -name '*.a' -o -name .git -o -name CMakeCache.txt \) -print -quit)"
[[ -z "$BAD" ]] || { echo "protected data reached the Flatpak payload: $BAD" >&2; exit 1; }
PAYLOAD_MB="$(du -sm "$PAYLOAD" | cut -f1)"
(( PAYLOAD_MB < 80 )) || { echo "source payload is ${PAYLOAD_MB} MB: a build tree or history leaked in" >&2; exit 1; }
[[ -x "$BUILD_DIR/files/bin/PMDRedRescueTeamBuilder" ]] || { echo "builder not installed" >&2; exit 1; }
[[ -x "$BUILD_DIR/files/bin/sources/bin/gba_recompile" ]] || { echo "gba_recompile not installed" >&2; exit 1; }
[[ -f "$BUILD_DIR/files/bin/sources/gbarecomp/third_party/tomlpp/toml.hpp" ]] ||
    { echo "toml++ header missing: the player's offline build would fail" >&2; exit 1; }

flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
    "$REPO" "$OUT/$BUNDLE" "$APP_ID"
echo "==> $OUT/$BUNDLE"
