#!/usr/bin/env python3
"""Apply Tekken 2's own psxrecomp patches to the pinned submodule.

The submodule stays at its upstream pin; every patches/*.patch is applied to
its working tree before generate and build, so the emitter, the static overlay
compiler and the runtime all see the same sources. Idempotent: applied
patches are left alone. Fails if the tree is neither a prefix of the stack
plus the pin nor accepts the remaining patches (the pin moved, or the tree has
other edits). Runs the same on macOS, Linux and Windows (needs git on PATH).

The stack is checked as a whole: a later patch may edit lines an earlier one
added or uses as context, so "is patch N applied" is answered by undoing the
applied prefix, last patch first, on a scratch copy of the touched files.

Usage: python3 scripts/apply_framework_patches.py [--check]
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK = ROOT / "psxrecomp"


def git_apply(cwd, *args):
    return subprocess.run(["git", "apply", *args], cwd=str(cwd),
                          capture_output=True, text=True).returncode == 0


def touched_files(patches):
    files = set()
    for patch in patches:
        for line in patch.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"diff --git a/(\S+) b/(\S+)", line)
            if m:
                files.update(m.groups())
    return sorted(files)


def prefix_applied(patches, k, files):
    """True if patches[:k] are applied: undo them, last first, on a copy."""
    if k == 0:
        return True
    with tempfile.TemporaryDirectory(prefix="t2-patch-check-") as tmp:
        tmp = Path(tmp)
        for rel in files:
            src = FRAMEWORK / rel
            if src.is_file():
                dst = tmp / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
        for patch in reversed(patches[:k]):
            if not git_apply(tmp, "--reverse", str(patch)):
                return False
    return True


def main():
    check_only = sys.argv[1:] == ["--check"]
    patches = sorted((ROOT / "patches").glob("*.patch"))
    files = touched_files(patches)
    applied = next(k for k in range(len(patches), -1, -1)
                   if prefix_applied(patches, k, files))
    for patch in patches[:applied]:
        print(f"framework patch applied: {patch.name}")
    for patch in patches[applied:]:
        if check_only:
            sys.exit(f"framework patch NOT applied: {patch.name}")
        if not git_apply(FRAMEWORK, "--check", str(patch)):
            sys.exit(f"error: {patch.name} neither applies nor is already applied "
                     "to psxrecomp; the submodule moved or has other edits")
        if not git_apply(FRAMEWORK, str(patch)):
            sys.exit(f"error: git apply failed for {patch.name}")
        print(f"framework patch applied now: {patch.name}")


if __name__ == "__main__":
    main()
