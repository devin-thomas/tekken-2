#!/usr/bin/env bash
# release.sh: build, test, package, zip and audit the Mac release of Tekken 2
# Recompiled from a clean commit. The Windows counterpart is release.ps1.
#
# Usage (from recomp, on an Apple silicon Mac, after generate.sh):
#   bash scripts/release.sh --out <dir> --disc <Tekken 2 (USA) (Rev 1).cue> [--build build-player] [--jobs 6]
#
# With release/ICON "default", the app icon is built from that disc first
# (assets/icon/src/extract_icon.py, in a venv under <dir>), so the repo holds no
# game art; "fallback" ships the psxrecomp logo.
#
# Writes to <dir>:
#   Tekken-2-Recompiled-<version>-macOS-arm64.zip   the player download
#   Tekken-2-Recompiled-<version>-macOS-arm64.zip.sha256
#   release-macos.json        private build record (commit, pin, sizes): never published
#   audit-macos.json          tools/release_audit.py's report
# The zip holds one folder, "Tekken 2 Recompiled <version>", with the .app,
# README.txt, CHANGELOG.txt, CREDITS.txt and LICENSES/.
set -euo pipefail

die() { echo "release: ERROR: $*" >&2; exit 1; }
[ "$(uname -s)" = "Darwin" ] || die "run this on macOS"
[ "$(uname -m)" = "arm64" ] || die "build the Mac release on Apple silicon (arm64 only)"
cd "$(dirname "$0")/.."
PROJECT="$(pwd)"

OUT=""
DISC=""
BUILD="build-player"
JOBS=6
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2;;
    --disc) DISC="$2"; shift 2;;
    --build) BUILD="$2"; shift 2;;
    --jobs) JOBS="$2"; shift 2;;
    -h|--help) sed -n '2,15p' "$0"; exit 0;;
    *) die "unknown argument: $1";;
  esac
done
[ -n "$OUT" ] || die "--out <dir> is required"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
[ -z "$DISC" ] || DISC="$(cd "$(dirname "$DISC")" && pwd)/$(basename "$DISC")"

VERSION="$(tr -d '[:space:]' < VERSION)"
case "$VERSION" in [0-9]*.[0-9]*.[0-9]*) ;; *) die "VERSION is not major.minor.patch: $VERSION";; esac
[ -d generated ] || die "no generated/ (run scripts/generate.sh first)"

# A release comes from a commit: no tracked change outside the patched submodule.
dirty="$(git status --porcelain --untracked-files=no -- . ':(exclude)psxrecomp' || true)"
[ -z "$dirty" ] || die "uncommitted changes:\n$dirty"
COMMIT="$(git rev-parse HEAD)"
PIN="$(git ls-tree HEAD psxrecomp | awk '{print $3}')"

echo "=== Tekken 2 Recompiled $VERSION for macOS arm64 from ${COMMIT:0:12} ==="
rm -rf "$BUILD"   # a release is a clean build
# ...from the pinned framework: undo earlier patch applications (patches may have
# changed since), so build.sh applies today's stack to a pristine submodule.
git -C psxrecomp checkout -q -- . && git -C psxrecomp clean -fdq
T2_RELEASE=ON nice -n 10 bash scripts/build.sh game.toml Release "$BUILD" "$JOBS"
nice -n 10 cmake --build "$BUILD" --parallel "$JOBS"
ctest --test-dir "$BUILD" --output-on-failure
grep -q '^T2_RELEASE:BOOL=ON' "$BUILD/CMakeCache.txt" || die "$BUILD was not configured with T2_RELEASE=ON"
grep -q '^PSX_DEBUG_TOOLS:BOOL=OFF' "$BUILD/CMakeCache.txt" || die "$BUILD has PSX_DEBUG_TOOLS on"

# Runs on macOS 11 and later: every Mach-O in the bundle must say so.
for macho in "$BUILD/Tekken_2_Recompiled"; do
  minos="$(otool -l "$macho" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/&&!d{print $2; d=1}')"
  [ "$minos" = "11.0" ] || die "$(basename "$macho") needs macOS $minos, not 11.0"
done

STAGE="$OUT/stage-macos"
rm -rf "$STAGE"
# release/ICON: "default" builds assets/icon/default from the disc and ships it;
# "fallback" ships the psxrecomp logo.
ICON="$(tr -d '[:space:]' < release/ICON)"
case "$ICON" in default|fallback) ;; *) die "release/ICON must be default or fallback, not '$ICON'";; esac
if [ "$ICON" = "default" ]; then
  [ -f "$DISC" ] || die "release/ICON is default: pass --disc <the Tekken 2 (USA) (Rev 1) .cue>"
  rm -rf assets/icon/default
  [ -x "$OUT/icon-venv/bin/python" ] || python3 -m venv "$OUT/icon-venv"
  "$OUT/icon-venv/bin/pip" install -q -r assets/icon/src/requirements.txt
  "$OUT/icon-venv/bin/python" -I assets/icon/src/extract_icon.py --disc "$DISC"
  [ -s assets/icon/default/macos/AppIcon.icns ] || die "the icon build made no macos/AppIcon.icns"
fi
bash scripts/package_macos.sh --build "$BUILD" --out "$STAGE/app" --icon "$ICON"

NAME="Tekken 2 Recompiled $VERSION"
DIR="$STAGE/$NAME"
mkdir -p "$DIR"
/usr/bin/ditto "$STAGE/app/Tekken 2 Recompiled.app" "$DIR/Tekken 2 Recompiled.app"
cp release/README.txt release/CHANGELOG.txt release/CREDITS.txt release/CONTRIBUTORS.txt "$DIR/"
/usr/bin/ditto "$DIR/Tekken 2 Recompiled.app/Contents/Resources/licenses" "$DIR/LICENSES"
codesign --verify --deep --strict "$DIR/Tekken 2 Recompiled.app"
if [ "$ICON" = "default" ]; then
  cmp -s assets/icon/default/macos/AppIcon.icns "$DIR/Tekken 2 Recompiled.app/Contents/Resources/AppIcon.icns" \
    || die "the app's AppIcon.icns is not the one built from the disc"
fi
for macho in "$DIR/Tekken 2 Recompiled.app/Contents/MacOS/Tekken_2_Recompiled" "$DIR/Tekken 2 Recompiled.app/Contents/Frameworks/"*.dylib; do
  [ -e "$macho" ] || continue
  minos="$(otool -l "$macho" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/&&!d{print $2; d=1}')"
  case "$minos" in 1[01].*) ;; *) die "$(basename "$macho") needs macOS $minos (the README says 11)";; esac
done

ZIP="Tekken-2-Recompiled-$VERSION-macOS-arm64.zip"
rm -f "$OUT/$ZIP" "$OUT/$ZIP.sha256"
# ditto keeps the bundle's symlinks and signature intact; no resource forks or __MACOSX.
(cd "$STAGE" && /usr/bin/ditto -c -k --norsrc --keepParent "$NAME" "$OUT/$ZIP")
(cd "$OUT" && shasum -a 256 "$ZIP" > "$ZIP.sha256")

if [ -f tools/release_audit.py ]; then  # the maintainers' privacy audit; not in the public repo
  python3 -I tools/release_audit.py "$OUT/$ZIP" --deny "$COMMIT" --deny "${COMMIT:0:8}" \
    --deny "$PIN" --deny "${PIN:0:8}" --json "$OUT/audit-macos.json"
fi

SIZE="$(stat -f%z "$OUT/$ZIP")"
SHA="$(awk '{print $1}' "$OUT/$ZIP.sha256")"
cat > "$OUT/release-macos.json" <<EOF
{"version": "$VERSION", "platform": "macOS arm64", "commit": "$COMMIT", "psxrecomp": "$PIN",
 "zip": "$ZIP", "bytes": $SIZE, "sha256": "$SHA", "built": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"}
EOF
echo "RELEASE: $OUT/$ZIP ($SIZE bytes, sha256 $SHA)"
