#!/usr/bin/env bash
# Configure and build the native runtime from generated/ (run generate.sh first).
# Usage: scripts/build.sh [game.toml] [Release|RelWithDebInfo] [build-dir] [jobs]
# Set PSX_DEBUG_TOOLS=ON for a Release build with the loopback TCP debug server
# (needed by scripts/headless_capture.py); use its own build dir, because the
# option is cached per build dir. T2_RELEASE=ON builds the player release.
set -euo pipefail

CONFIG="${1:-game.toml}"
BUILD_TYPE="${2:-Release}"
BUILD_DIR="${3:-build-release}"
JOBS="${4:-4}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
case "$BUILD_TYPE" in
    Release|RelWithDebInfo) ;;
    *) echo "error: use Release or RelWithDebInfo for the generated game C." >&2; exit 1 ;;
esac
PYTHON_CMD=python3
command -v python3 >/dev/null 2>&1 || PYTHON_CMD=python
"$PYTHON_CMD" scripts/project_config.py --config "$CONFIG"

if [ ! -f "psxrecomp/runtime/runtime.cmake" ]; then
    echo "psxrecomp submodule missing; run: git submodule update --init --recursive recomp/psxrecomp" >&2
    exit 1
fi

# Tekken 2-owned framework fixes (patches/*.patch), applied to the pinned submodule.
"$PYTHON_CMD" scripts/apply_framework_patches.py
echo "=== Tekken 2 Recomp: building psx-runtime ($BUILD_TYPE, $JOBS jobs) ==="
EXTRA_ARGS=()
if [ -n "${PSX_DEBUG_TOOLS:-}" ]; then
    EXTRA_ARGS+=("-DPSX_DEBUG_TOOLS=${PSX_DEBUG_TOOLS}")
fi
# T2_RELEASE=ON: the player release's settings panel (scripts/release.sh).
if [ -n "${T2_RELEASE:-}" ]; then
    EXTRA_ARGS+=("-DT2_RELEASE=${T2_RELEASE}")
fi
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DGAME_CONFIG="$CONFIG" ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
cmake --build "$BUILD_DIR" --target psx-runtime --parallel "$JOBS"
echo "=== Build complete ==="
