#!/usr/bin/env bash
# package_macos.sh: wrap a built Tekken 2 runtime in a signed, self-contained
# "Tekken 2 Recompiled.app" (no disc, no generated C).
#
# Usage (from recomp, on a Mac, after scripts/build.sh):
#   bash scripts/package_macos.sh [--build build-release] [--out scratch/mac-app] [--icon default|fallback]
# --icon fallback leaves assets/icon/default out (the psxrecomp logo instead), while
# an icon is held back from release (release/ICON).
#
# Data lives in Contents/Resources, as in psxrecomp/tools/package_release_macos.sh.
# That packager is not called directly because it expects the framework's own
# repo layout (runtime/licenses at --repo) and dylibbundler. This script copies
# every non-system dylib the binary links (with Homebrew: SDL3 and OpenSSL
# libcrypto) into Contents/Frameworks and rewrites the load commands
# to @executable_path/../Frameworks.
#
# Per-user state stays out of the signed bundle. The runtime anchors every
# file (game.toml, bios/, mods/, settings.toml, disc.cfg, saves/,
# overlay_captures.json) on the directory of the executable path it was
# started with, without resolving symlinks, while dyld resolves
# @executable_path through them. So the bundle executable is a shell shim
# that builds a home in ~/Library/Application Support/Tekken 2 Recompiled/
# (a symlink to the real binary, links to bios/ and assets/, a copy of mods/,
# and a per-user game.toml) and execs the binary through that symlink. All
# writes land there and the code seal stays intact.
#
# The disc is never bundled. On a launch with no arguments and no remembered
# disc, the shim shows the standard macOS Open panel (osascript "choose
# file") and remembers the choice in disc-path.txt in that folder; the
# per-user game.toml carries it as [game] disc. `--disc <cue>` on the command
# line still overrides. The runtime itself has no file picker on macOS.
#
# Gates (any failure exits non-zero): every Mach-O links only /usr/lib,
# /System or the bundle's Frameworks; no build-machine home path in the
# binary; OpenBIOS image present; no disc, BIOS dump, generated C or local
# state in the bundle; codesign --verify --deep --strict passes.
set -euo pipefail

die() { echo "package_macos: ERROR: $*" >&2; exit 1; }
note() { echo "  $*"; }

[ "$(uname -s)" = "Darwin" ] || die "run this on macOS"
cd "$(dirname "$0")/.."
PROJECT="$(pwd)"

BUILD="build-release"
OUT="scratch/mac-app"
ICON="default"
while [ $# -gt 0 ]; do
  case "$1" in
    --icon) ICON="$2"; shift 2;;
    --build) BUILD="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    -h|--help) sed -n '2,28p' "$0"; exit 0;;
    *) die "unknown argument: $1";;
  esac
done

BIN_NAME="Tekken_2_Recompiled"
APP_NAME="Tekken 2 Recompiled"
BUNDLE_ID="com.lilgohan.tekken2recompiled"
VERSION="$(tr -d '[:space:]' < VERSION 2>/dev/null || echo 0.0.0)"
BUILD="$(cd "$BUILD" && pwd)"
BIN="$BUILD/$BIN_NAME"
[ -f "$BIN" ] || die "no built binary at $BIN (run scripts/build.sh first)"

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
APPDIR="$OUT/$APP_NAME.app"
case "$APPDIR" in "$OUT"/*.app) ;; *) die "unexpected bundle path $APPDIR";; esac
rm -rf "$APPDIR"
mkdir -p "$APPDIR/Contents/MacOS" "$APPDIR/Contents/Resources" "$APPDIR/Contents/Frameworks"
MACOS="$APPDIR/Contents/MacOS"
RES="$APPDIR/Contents/Resources"
FW="$APPDIR/Contents/Frameworks"

echo "packaging $APP_NAME $VERSION from $BIN ($(lipo -archs "$BIN"))"
cp "$BIN" "$MACOS/$BIN_NAME"
chmod u+w "$MACOS/$BIN_NAME"

stage_data() {  # stage_data <name in Contents/Resources> <source>
  [ -e "$2" ] || die "missing staged data: $2"
  cp -a "$2" "$RES/$1"
}
stage_data bios "$BUILD/bios"
stage_data assets "$BUILD/assets"
stage_data mods "$BUILD/mods"
find "$RES/mods" \( -name 'state.toml' -o -name 'state.toml.tmp' \) -delete
# game.toml without its comments (they point at private notes and the build).
grep -v '^[[:space:]]*#' "$PROJECT/game.toml" > "$RES/game.toml"
# The shim rewrites the one [game] disc line in its per-user copy.
[ "$(grep -c '^disc = ' "$RES/game.toml")" = "1" ] \
  || die "game.toml must have exactly one line starting 'disc = ' (the shim rewrites it)"
# Licences and notices: psxrecomp's runtime notices, its licence and the
# PERMISSIONS.md that must ride with it, OpenBIOS, the embedded font, stb,
# and release/licenses (Tekken 2 Recompiled's own, SDL3). Each bundled dylib
# adds its Homebrew formula's licence below.
LIC="$RES/licenses"
mkdir -p "$LIC"
cp psxrecomp/runtime/licenses/* "$LIC/"
cp psxrecomp/LICENSE "$LIC/psxrecomp-LICENSE.txt"
cp psxrecomp/PERMISSIONS.md "$LIC/psxrecomp-PERMISSIONS.md"
cp psxrecomp/bios/OpenBIOS.LICENSE "$LIC/OpenBIOS.LICENSE"
cp src/fonts/LICENSE-NotoSans-OFL.txt "$LIC/NotoSans-OFL.txt"
cp src/third_party/stb/LICENSE "$LIC/stb_truetype-LICENSE.txt"
cp release/licenses/* "$LIC/"

# ---- bundle non-system dylibs ---------------------------------------------
is_system() { case "$1" in /usr/lib/*|/System/Library/*) return 0;; *) return 1;; esac; }
deps_of() { otool -L "$1" | tail -n +2 | awk '{print $1}'; }

bundle_license() {  # bundle_license <real dylib path>: its formula's licence, as <Name>-LICENSE.txt
  local prefix name f
  prefix="$(dirname "$(dirname "$1")")"
  case "$(basename "$(dirname "$prefix")")" in
    sdl3) name=SDL3;;
    openssl*) name=OpenSSL;;
    *) name="$(basename "$(dirname "$prefix")")";;
  esac
  for f in LICENSE.txt LICENSE COPYING.txt COPYING; do
    if [ -f "$prefix/$f" ]; then
      cp "$prefix/$f" "$LIC/$name-LICENSE.txt"
      note "licence for $(basename "$1"): $name-LICENSE.txt"
      return 0
    fi
  done
  die "no licence file beside $1 (looked in $prefix)"
}

bundle_deps() {  # bundle_deps <mach-o inside the bundle>
  local target="$1" dep base real
  for dep in $(deps_of "$target"); do
    is_system "$dep" && continue
    case "$dep" in @executable_path/../Frameworks/*) continue;; esac
    base="$(basename "$dep")"
    # The dylib's own install name (its first otool -L line) is not a dependency.
    [ "$target" != "$FW/$base" ] || continue
    if [ ! -f "$FW/$base" ]; then
      real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$dep")"
      [ -f "$real" ] || die "cannot find $dep (linked by $(basename "$target"))"
      cp "$real" "$FW/$base"
      chmod u+w "$FW/$base"
      install_name_tool -id "@executable_path/../Frameworks/$base" "$FW/$base" 2>/dev/null
      note "bundled $base (from $real)"
      bundle_license "$real"
      bundle_deps "$FW/$base"
    fi
    install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$target" 2>/dev/null
  done
}
bundle_deps "$MACOS/$BIN_NAME"
# Build-machine search paths (Homebrew's lib) out of the load commands.
for macho in "$MACOS/$BIN_NAME" "$FW"/*.dylib; do
  [ -e "$macho" ] || continue
  for rp in $(otool -l "$macho" | awk '/cmd LC_RPATH/{getline; getline; print $2}'); do
    case "$rp" in @*) ;; *) install_name_tool -delete_rpath "$rp" "$macho"; note "dropped rpath $rp from $(basename "$macho")";; esac
  done
done

# ---- icon: the project's own icon (assets/icon/default, built from the disc by
# assets/icon/src/extract_icon.py; scripts/release.sh runs it), else the framework's
# logo as a fallback until that icon lands. No game assets either way.
ICON_KEY=""
OWN_ICNS="$PROJECT/assets/icon/default/macos/AppIcon.icns"
if [ "$ICON" = "default" ] && [ -f "$OWN_ICNS" ]; then
  cp "$OWN_ICNS" "$RES/AppIcon.icns"
  ICON_KEY="<key>CFBundleIconFile</key><string>AppIcon</string>"
  note "icon from ${OWN_ICNS#$PROJECT/}"
elif [ -f "$BUILD/assets/psxrecomp.png" ] && command -v iconutil >/dev/null; then
  ICONSET="$(mktemp -d)/AppIcon.iconset"
  mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    sips -z $s $s "$BUILD/assets/psxrecomp.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) "$BUILD/assets/psxrecomp.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$RES/AppIcon.icns"
  rm -rf "$(dirname "$ICONSET")"
  ICON_KEY="<key>CFBundleIconFile</key><string>AppIcon</string>"
  note "icon from the psxrecomp logo (fallback: --icon $ICON or no assets/icon/default)"
fi

# ---- shim and Info.plist ---------------------------------------------------
cat > "$MACOS/$APP_NAME" <<'SHIM'
#!/bin/sh
# Tekken 2 Recompiled launcher (bundle executable). See scripts/package_macos.sh.
# Builds the per-user home, asks for the disc once, then runs the real binary
# through a symlink so the runtime treats that home as its folder.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$DIR/../Resources" && pwd)"
BIN=Tekken_2_Recompiled
SUPPORT="${T2_RECOMP_HOME:-$HOME/Library/Application Support/Tekken 2 Recompiled}"
mkdir -p "$SUPPORT" || exit 1

# Read-only data: refreshed from this bundle on every launch, so a moved or
# updated .app is picked up. mods/ is copied because the runtime writes
# mods/state.toml; ditto merges and keeps that file.
ln -sfn "$DIR/$BIN" "$SUPPORT/$BIN"
for name in bios assets; do
    rm -f "$SUPPORT/$name"
    ln -s "$RES/$name" "$SUPPORT/$name"
done
/usr/bin/ditto "$RES/mods" "$SUPPORT/mods"

# The disc: remembered in disc-path.txt; asked for with the standard Open
# panel when missing and no command-line arguments were given.
DISC=""
[ -s "$SUPPORT/disc-path.txt" ] && DISC="$(head -n 1 "$SUPPORT/disc-path.txt")"
if [ "$#" -eq 0 ] && { [ -z "$DISC" ] || [ ! -f "$DISC" ]; }; then
    DISC="$(/usr/bin/osascript \
        -e 'set f to choose file with prompt "Choose your Tekken 2 (USA) (Rev 1) disc image (.cue)" of type {"cue", "chd", "bin", "iso"}' \
        -e 'POSIX path of f' 2>/dev/null)" || exit 0
    [ -n "$DISC" ] || exit 0
    printf '%s\n' "$DISC" > "$SUPPORT/disc-path.txt"
fi

# Per-user game.toml: the bundled one with [game] disc set to the chosen
# image (TOML basic string: backslashes and quotes escaped).
ESC="$(printf '%s' "$DISC" | sed 's/\\/\\\\/g; s/"/\\"/g')"
T2_DISC="$ESC" /usr/bin/awk '
    /^disc = / && ENVIRON["T2_DISC"] != "" { print "disc = \"" ENVIRON["T2_DISC"] "\""; next }
    { print }' "$RES/game.toml" > "$SUPPORT/game.toml.tmp" \
    && mv "$SUPPORT/game.toml.tmp" "$SUPPORT/game.toml"

export SDL_JOYSTICK_HIDAPI_STEAM=1
cd "$SUPPORT" || exit 1
exec "$SUPPORT/$BIN" --game "$SUPPORT/game.toml" "$@"
SHIM
chmod +x "$MACOS/$APP_NAME"

cat > "$APPDIR/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  $ICON_KEY
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
EOF

# ---- sign (ad hoc), inside out ---------------------------------------------
for lib in "$FW"/*.dylib; do
  [ -e "$lib" ] && codesign --force --sign - "$lib"
done
codesign --force --sign - "$MACOS/$BIN_NAME"
codesign --force --sign - "$APPDIR"

# ---- gates -----------------------------------------------------------------
for macho in "$MACOS/$BIN_NAME" "$FW"/*.dylib; do
  [ -e "$macho" ] || continue
  bad="$(deps_of "$macho" | grep -vE '^(/usr/lib/|/System/Library/|@executable_path/../Frameworks/)' || true)"
  [ -z "$bad" ] || die "$(basename "$macho") links outside the OS and bundle: $bad"
done
note "self-contained: links only /usr/lib, /System and Contents/Frameworks"
leak="$(strings -a "$MACOS/$BIN_NAME" | grep -oE "(/opt/homebrew|$HOME)/[^ \"']*" | sort -u | head -5 || true)"
[ -z "$leak" ] || die "binary holds build-machine paths: $leak"
note "no build-machine paths in the binary"
[ "$(stat -f%z "$RES/bios/openbios.bin")" = "524288" ] || die "OpenBIOS image missing or wrong size"
stray="$(find "$APPDIR" \( -iname '*.cue' -o -iname '*.iso' -o -iname '*.chd' -o -iname 'SLUS_*' \
  -o -iname 'SCPH*.BIN' -o -iname '*.mcd' -o -iname '*.mcr' -o -iname 'state.toml' \
  -o -iname 'settings.toml' -o -iname 'overlay_captures.json' -o -iname 'disc.cfg' \
  -o -path '*/generated/*' \) -print)"
stray_bin="$(find "$APPDIR" -iname '*.bin' ! -path "$RES/bios/openbios.bin" -print)"
[ -z "$stray$stray_bin" ] || die "files that must not ship are in the bundle: $stray $stray_bin"
note "no disc, BIOS dump, generated C or local state"
for f in psxrecomp-LICENSE.txt psxrecomp-PERMISSIONS.md OpenBIOS.LICENSE NotoSans-OFL.txt \
         stb_truetype-LICENSE.txt Tekken2Recompiled-LICENSE.txt SDL3-LICENSE.txt; do
  [ -s "$LIC/$f" ] || die "licence missing from the bundle: $f"
done
note "licences: $(ls "$LIC" | wc -l | tr -d ' ') files"
codesign --verify --deep --strict --verbose=1 "$APPDIR"
note "codesign verified (ad hoc)"
echo "BUILT: $APPDIR"
