#!/usr/bin/env python3
"""Reject a config that is not the USA Rev 1 (SLUS_002.13 v1.1) base this tree builds."""

import argparse
from pathlib import Path
import tomllib

PROJECT_ROOT = Path(__file__).resolve().parents[1]
BASE_ID = "SLUS-00213"
BASE_BOOT = "SLUS_002.13"
BASE_MD5 = "14c6b7308e1bf796fb36e523adde34d7"


def validate(config, project_root=PROJECT_ROOT):
    path = Path(config)
    if not path.is_absolute():
        path = project_root / path
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    game = data.get("game", {})
    prep = data.get("prepare_disc", {})
    if (game.get("id") != BASE_ID or Path(game.get("exe", "")).name != BASE_BOOT
            or prep.get("boot_exe") != BASE_BOOT or BASE_MD5 not in prep.get("known_md5", [])):
        raise ValueError(
            f"{path}: this tree builds Tekken 2 USA Rev 1 ({BASE_BOOT}, Track 1 MD5 "
            f"{BASE_MD5}). Other revisions and regions need their own generated sources.")
    return path.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="game.toml")
    args = parser.parse_args()
    try:
        path = validate(args.config)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        parser.exit(1, f"Configuration rejected: {error}\n")
    print(f"USA Rev 1 config verified: {path}")


if __name__ == "__main__":
    main()
