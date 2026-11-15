# Tekken 2 recomp app icon

The icon is built from the user's own Tekken 2 disc. `src/extract_icon.py`
reads the disc, decodes the game's TIM textures and CLUTs with the game's own
LZ scheme, and builds the platform icon sets. Every pixel is a game pixel:
nothing is redrawn, smoothed, outlined or retouched. Pieces are placed on a
plain background whose colour comes from the game's palette (or black) and
scaled by whole numbers with nearest-neighbour sampling.

The repo stores the script and this README only. The extracted pieces and the
built icons contain game art, so they are gitignored: build them at packaging
time from the user's disc, or into private scratch.

## Build

From `recomp`:

```sh
python3 -m venv ~/scratch/t2-icon-venv
~/scratch/t2-icon-venv/bin/pip install -r assets/icon/src/requirements.txt
~/scratch/t2-icon-venv/bin/python -I assets/icon/src/extract_icon.py \
    --disc "disc/Tekken 2 (USA) (Rev 1).cue"
```

It takes under a second and writes:

- `scratch/icon-extracted/`: for each candidate, `native.png` (the raw piece at
  native pixels), `icon-{1024,512,256,128,64,48,32,16}.png`, and `source.json`
  (where each piece is on the disc and how each size was made);
  `contact-sheet.png`; `pieces.sha256.json`.
- `assets/icon/default/`: the sets for `--default` (candidate 1 unless given):
  `macos/AppIcon.icns` (needs `iconutil`, so macOS only) and
  `macos/AppIcon-1024.png`; `windows/app.ico` (16 to 256 px) and
  `windows/app-256.png`; `android/res/mipmap-*` (adaptive background and
  foreground, legacy `ic_launcher` and `ic_launcher_round`) and
  `android/play-store-512.png`; `ios/AppIcon.appiconset/` with
  `Contents.json`; `icon-1024.png` and `SOURCE`.

The disc must be Tekken 2 (USA) (Rev 1), SLUS-00213, as a raw MODE2/2352
track 1 (`.cue` or `.bin`). The script checks the SHA-256 of every decoded
piece and stops on any other release. `--print-hashes` prints them.

Packaging (`scripts/package_*.sh`, `package_windows.ps1`) still uses the
psxrecomp logo. Wiring these sets in is release work: run the script with the
release's disc first, then copy from `assets/icon/default/`.

## Candidates

Each source was traced to what the game itself draws: GP0 command dumps and
VRAM reads from headless runs of the debug-tools build (the attract stage
card, the arcade stage card, the title menu and Option Mode), compared with
the data the script decodes from the disc.

| Key | What | Source on the disc | How the game draws it |
| --- | --- | --- | --- |
| **1 (default)** | "T2" in the stage-card font | Glyphs: `SLUS_002.13` LZ stream at 0xF6224, a 4 bpp TIM at VRAM (512, 256); cells 36 x 30, T at (36, 160), 2 at (72, 40). Colours: BNS record 17 LZ stream at 0x257A8, CLUT row (0, 507), palette at (32, 507). | The stage card's "VS" (red fill): 36 x 30 sprites, 30 px apart, drawn right to left, as the card draws "STAGE 1". |
| 2 | Title logo, 鉄拳 TEKKEN2 | BNS record 15 (an archive), entries 0 to 3: four 8 bpp 128 x 240 TIMs at VRAM (512..704, 0) with their own CLUTs (512, 252..255); crop x 50-459, y 24-175. | The title menu draws the four strips 1:1 as the 512 x 240 screen. |
| 3 | Option Mode logo tile | BNS record 17 LZ stream at 0x29798 (4 bpp TIM at VRAM (832, 0)), tile u 0 v 176, 128 x 64; CLUT row from 0x2BB90, palette at (528, 241). | The scrolling background of Option Mode. |

`--t2-colour` picks candidate 1's palette from the same CLUT row: `vs-red`
(default, (32, 507), the card's "VS"), `names-red` ((80, 507), the card's
fighter names: dark fill, red rim) or `blue` ((48, 507), "STAGE n").
`scratch/icon-extracted/contact-sheet-red.png` shows the three side by side.
The other red lettering on screen is a different glyph set: "GAME OVER" is a
24 x 12 font at VRAM (576, 256) with CLUT (32, 506), and the names under the
health bars are whole-word name plates at VRAM (960, 256).

Candidate 1's small sizes (48, 32 and 16 px), where the 59 x 30 native piece
cannot fit at 1x, use the game's small HUD font from the same texture: cells
16 x 10, T at (208, 10), 2 at (32, 0), 13 px apart, in the white palette the
title screen uses for its copyright line (CLUT (96, 505), from the
`SLUS_002.13` LZ stream at 0xF9C84). 48 and 32 px show "T2" at 1x; 16 px
shows the 2 alone, because "T2" is 26 px wide. Sizes from 64 px up use the
stage-card glyphs at 1x to 16x.

鉄拳 cannot be extracted on its own: the title picture paints TEKKEN2 over the
lower third of both kanji, and every other copy of the logo on the disc (the
Option Mode tile, the area-champion screens in records 6 to 14, the blue title
in record 16) is the same composite. Candidate 2 is therefore the whole logo
with the game's own 2. Candidates 2 and 3 are wider than their small sizes
allow at 1x, so below 512 px (candidate 2) and 256 px (candidate 3) they keep
every n-th pixel; they are not legible below 128 px.

Android's adaptive icon has no monochrome layer, since a themed silhouette
would recolour the art.
