#!/usr/bin/env bash
# Emit the game C under generated/ (gitignored: it is derived from the disc).
# Usage: scripts/generate.sh [game.toml] [path/to/disc.cue] [SCPH1001.BIN]
set -euo pipefail

CONFIG="${1:-game.toml}"
DISC="${2:-}"
BIOS="${3:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "=== Tekken 2 Recomp: code generation ==="

if [ ! -f "psxrecomp/psxrecomp_cli.py" ]; then
    echo "psxrecomp submodule missing; run: git submodule update --init --recursive recomp/psxrecomp" >&2
    exit 1
fi

PYTHON_CMD=python3
command -v python3 >/dev/null 2>&1 || PYTHON_CMD=python

"$PYTHON_CMD" scripts/project_config.py --config "$CONFIG"
"$PYTHON_CMD" -B scripts/sync_symbols.py --check
# Tekken 2-owned framework fixes (patches/*.patch), applied to the pinned submodule.
"$PYTHON_CMD" scripts/apply_framework_patches.py

ARGS=("$PYTHON_CMD" "psxrecomp/psxrecomp_cli.py" "generate" "--config" "$CONFIG" "--project-root" ".")
[ -n "$DISC" ] && ARGS+=("--disc" "$DISC")
[ -n "$BIOS" ] && ARGS+=("--bios" "$BIOS")

"${ARGS[@]}"

echo "=== Code generation complete ==="
