#!/usr/bin/env python3
"""Derive emitter seeds and the symbol header from symbols.toml.

Seeds = every [[func]] in symbols.toml plus the first-pass JAL targets in
seeds/probe_jal_targets.txt (written by psxrecomp's probe_disc.py), minus any
probe target inside game.toml [recompiler].data_ranges: probe_disc.py scans
data words too, and some decode as JAL. [[ref]] entries (data, mid-function
code sites, overlay bases) are never seeded.
"""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[1]
TEXT_START = 0x80010000
TEXT_END = 0x80010000 + 0x000F9800  # header t_size
STATUSES = {"verified", "unverified-external", "guessed"}


def _addr(value) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def load_symbols(root: Path) -> tuple[list[dict], list[dict]]:
    data = tomllib.loads((root / "symbols.toml").read_text(encoding="utf-8"))
    funcs, refs = data.get("func", []), data.get("ref", [])
    if not funcs:
        raise ValueError("symbols.toml has no [[func]] entries")
    names: set[str] = set()
    pcs: set[int] = set()
    for entry in funcs:
        pc = _addr(entry["pc"])
        if pc % 4 or not TEXT_START <= pc < TEXT_END:
            raise ValueError(f"func outside SLUS_002.13 text or misaligned: {entry!r}")
        if pc in pcs:
            raise ValueError(f"duplicate func address: {entry!r}")
        pcs.add(pc)
    for entry in funcs + refs:
        name = entry["name"]
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or name in names:
            raise ValueError(f"bad or duplicate symbol name: {entry!r}")
        names.add(name)
        if entry.get("status") not in STATUSES:
            raise ValueError(f"unknown status (use {sorted(STATUSES)}): {entry!r}")
        if entry.get("status") == "unverified-external" and not entry.get("source"):
            raise ValueError(f"unverified-external entries need a source: {entry!r}")
    for entry in refs:
        if _addr(entry["addr"]) in pcs:
            raise ValueError(f"[[ref]] address is also a seeded [[func]]: {entry!r}")
    return funcs, refs


def probe_seeds(root: Path) -> set[int]:
    path = root / "seeds/probe_jal_targets.txt"
    out = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.add(int(line, 16))
    return out


def data_ranges(root: Path) -> list[tuple[int, int]]:
    config = tomllib.loads((root / "game.toml").read_text(encoding="utf-8"))
    ranges = config.get("recompiler", {}).get("data_ranges", [])
    return [(_addr(lo), _addr(hi)) for lo, hi in ranges]


def in_data(addr: int, ranges: list[tuple[int, int]]) -> bool:
    return any(lo <= addr < hi for lo, hi in ranges)


def expected_outputs(root: Path) -> dict[Path, str]:
    funcs, refs = load_symbols(root)
    ranges = data_ranges(root)
    named = {_addr(f["pc"]) for f in funcs}
    inside = sorted(pc for pc in named if in_data(pc, ranges))
    if inside:
        raise ValueError("[[func]] entries inside data_ranges: "
                         + ", ".join(f"0x{a:08X}" for a in inside))
    ref_addrs = {_addr(r["addr"]) for r in refs}
    seeds = {pc for pc in probe_seeds(root) if not in_data(pc, ranges)} | named
    clash = sorted(seeds & ref_addrs)
    if clash:
        raise ValueError("seeds include [[ref]] (non-function) addresses: "
                         + ", ".join(f"0x{a:08X}" for a in clash))

    helper = root / "psxrecomp/tools/new_project_layout/sync_symbols.py"
    spec = importlib.util.spec_from_file_location("psxrecomp_symbol_exports", helper)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load framework symbol exporter: {helper}")
    exporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exporter)
    header_funcs = exporter.load_funcs(root / "symbols.toml")

    lines = [
        "# Derived from symbols.toml + seeds/probe_jal_targets.txt (minus",
        "# game.toml data_ranges) by scripts/sync_symbols.py; do not edit.",
        f"# Function count: {len(seeds)}",
        "",
        *(f"0x{pc:08X}" for pc in sorted(seeds)),
    ]
    return {
        root / "psx_symbols.h": exporter.render_header(header_funcs, game="tekken2"),
        root / "seeds/ghidra_funcs.txt": "\n".join(lines) + "\n",
    }


def sync_outputs(root: Path = ROOT, *, check: bool = False) -> bool:
    current = True
    for path, expected in expected_outputs(root).items():
        if path.is_file() and path.read_text(encoding="utf-8") == expected:
            continue
        current = False
        if check:
            print(f"out of date: {path}; run scripts/sync_symbols.py")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(expected, encoding="utf-8", newline="\n")
            print(f"wrote {path}")
    return current if check else True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail on stale exports")
    args = parser.parse_args()
    return 0 if sync_outputs(check=args.check) else 1


if __name__ == "__main__":
    raise SystemExit(main())
