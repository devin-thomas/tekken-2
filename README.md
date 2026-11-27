# Tekken 2

Native versions of Tekken 2, Namco's 1996 fighting game for the PlayStation,
for Mac and Windows. Each one runs from your own disc image of the game. This
repo holds no game data: no disc image, no BIOS and no files from the disc.

## What is here

| Folder | What it is | State |
| --- | --- | --- |
| [`recomp/`](recomp/) | Tekken 2 Recompiled: the game's code translated to C with [psxrecomp](https://github.com/mstan/psxrecomp) and compiled for Mac and Windows | 1.0.0 |
| `decomp/` | A decompilation of Tekken 2, when it is released | Not yet published |
| `ports/` | Ports built from the decompilation | Not yet published |

## Download

The [releases page](../../releases) has the current builds:

- `Tekken-2-Recompiled-1.0.0-macOS-arm64.zip`: Apple silicon Macs, macOS 11 or later
- `Tekken-2-Recompiled-1.0.0-Windows-x64.zip`: 64-bit Windows 10 and 11

Each release lists the SHA-256 of its zips. The README inside each zip says
how to open the app the first time, which disc it needs and how to check it,
and the controls.

## The disc

Tekken 2 (USA) (Rev 1), SLUS-00213, the Greatest Hits pressing, as a BIN/CUE
image with all three tracks. Track 1 SHA-1:
`699bd5831fbe3b662c59906ebf0acd3d1c3393f3`.

## Building from source

[`recomp/README.md`](recomp/README.md) has the steps. You need your own disc
image: the build reads the game's code from it.

## Credits

Tekken 2 Recompiled is by Lil Gohan ([Twitter](https://twitter.com/gohan__FGC),
[fgc.lilgohan.com](https://fgc.lilgohan.com)). It is built on psxrecomp by
Matthew Stanley.

## Licences

Our own code is under the MIT licence ([LICENSE](LICENSE)). psxrecomp is
under the PolyForm Noncommercial License 1.0.0 with its additional
permissions for free projects, so the builds here are free and nobody may
charge for them. [NOTICE.md](NOTICE.md) lists every licence.

Tekken is a registered trademark of Bandai Namco Entertainment Inc. This
project is not made, approved or endorsed by Bandai Namco. PlayStation is a
registered trademark of Sony Interactive Entertainment Inc.
