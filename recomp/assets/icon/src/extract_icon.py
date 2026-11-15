#!/usr/bin/env python3
"""Build the Tekken 2 recomp app icons from the user's own Tekken 2 disc.

Every pixel comes from the disc: the game's TIM textures and CLUTs, decoded
with the game's own LZ scheme (0x8006C46C). Nothing is redrawn, smoothed or
retouched. Pieces are only placed on a plain background whose colour is taken
from the game's own palette (or black), and scaled by whole numbers with
nearest-neighbour sampling. The repo stores this script, not the art: run it
at packaging time, or into private scratch.

Disc: Tekken 2 (USA) (Rev 1), SLUS-00213, the raw MODE2/2352 track 1 (.bin or
its .cue). The script checks a SHA-256 of every piece it decodes and stops if
the disc is a different release.

Candidates (see ../README.md for how each was traced to the game's own draw):
  1  "T2" in the stage-card font, drawn as the game draws "STAGE 2"
  2  the title-screen logo (鉄拳 TEKKEN2), BNS record 15
  3  the small 鉄拳 TEKKEN2 logo tile from the Option Mode background

Usage (from recomp):
  python3 -m venv ~/scratch/t2-icon-venv
  ~/scratch/t2-icon-venv/bin/pip install -r assets/icon/src/requirements.txt
  ~/scratch/t2-icon-venv/bin/python assets/icon/src/extract_icon.py \\
      --disc "disc/Tekken 2 (USA) (Rev 1).cue"

Writes scratch/icon-extracted/ (raw pieces, per-candidate renders, contact
sheet) and assets/icon/default/ (macOS, Windows, Android and iOS sets for
--default). Both are gitignored. The .icns needs macOS `iconutil`; elsewhere
the .icns is skipped and every other set is still written.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[2]  # recomp
SECTOR = 2352

# ---------------------------------------------------------------- disc access


def track1_path(disc: Path) -> Path:
    """The track 1 .bin of a .cue (or the .bin itself)."""
    if disc.suffix.lower() != ".cue":
        return disc
    text = disc.read_text(errors="replace")
    if "MODE2/2352" not in text:
        sys.exit(f"{disc}: track 1 is not MODE2/2352")
    for line in text.splitlines():
        line = line.strip()
        if line.upper().startswith("FILE"):
            name = line.split('"')[1] if '"' in line else line.split()[1]
            return disc.parent / name
    sys.exit(f"{disc}: no FILE line")


class Disc:
    def __init__(self, path: Path):
        self.f = open(path, "rb")
        self.files = dict(self._walk(*self._root(), ""))

    def _sector(self, lba: int) -> bytes:
        self.f.seek(lba * SECTOR)
        return self.f.read(SECTOR)[24:24 + 2048]  # MODE2 form 1 user data

    def read(self, lba: int, size: int) -> bytes:
        out = bytearray()
        for i in range((size + 2047) // 2048):
            out += self._sector(lba + i)
        return bytes(out[:size])

    def _root(self):
        pvd = self._sector(16)
        if pvd[1:6] != b"CD001":
            sys.exit("not an ISO 9660 track (expected a raw MODE2/2352 image)")
        return struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]

    def _walk(self, lba, size, prefix):
        data, i = self.read(lba, size), 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            elba, esz = struct.unpack_from("<I", data, i + 2)[0], struct.unpack_from("<I", data, i + 10)[0]
            flags, nl = data[i + 25], data[i + 32]
            name = data[i + 33:i + 33 + nl].decode("latin1")
            i += n
            if name in ("\x00", "\x01"):
                continue
            path = f"{prefix}/{name.split(';')[0]}"
            if flags & 2:
                yield from self._walk(elba, esz, path)
            else:
                yield path, (elba, esz)

    def file(self, path: str) -> bytes:
        return self.read(*self.files[path])


def bns_record(disc: Disc, exe: bytes, n: int) -> bytes:
    """BNS record n. The table is in the EXE at file offset 0x960 (RAM
    0x80010160): 400 entries of {0, sector, size}, sectors from the BNS start."""
    _, sec, size = struct.unpack_from("<III", exe, 0x960 + n * 12)
    lba, _ = disc.files["/TEKKEN2/TEKKEN2.BNS"]
    return disc.read(lba + sec, size)


# ---------------------------------------------------------------- the game's formats


def lz(src: bytes, pos: int) -> bytes:
    """The game's LZ streams (decoder at 0x8006C46C): a flag byte, LSB first;
    1 = literal, 0 = big-endian u16 (distance 11 bits, 0 = 0x800; length
    5 bits, 0 = 0x20); a flag byte of 0 ends the stream."""
    out = bytearray()
    flags = src[pos]
    pos += 1
    while flags:
        while flags >= 2:
            if flags & 1:
                out.append(src[pos])
                pos += 1
            else:
                w = src[pos] << 8 | src[pos + 1]
                pos += 2
                dist, n = (w & 0x7FF) or 0x800, ((w >> 11) & 0x1F) or 0x20
                s = len(out) - dist
                if s < 0:
                    raise ValueError("bad LZ stream")
                for k in range(n):
                    out.append(out[s + k])
            flags >>= 1
        flags = src[pos]
        pos += 1
    return bytes(out)


def tim(blob: bytes):
    """A TIM: {'bpp', 'clut': (x, y, [colours]) or None, 'img': (x, y, w_words, h, [words]) or None}."""
    if blob[:4] != b"\x10\0\0\0":
        raise ValueError("not a TIM")
    flags, q, out = blob[4], 8, {"bpp": (4, 8, 16, 24)[blob[4] & 3], "clut": None, "img": None}
    for key in (["clut", "img"] if flags & 8 else ["img"]):
        if q + 12 > len(blob):
            break
        n, x, y, w, h = struct.unpack_from("<IHHHH", blob, q)
        # CLUT-only TIMs end in a stub image header; stop at any block that does not fit.
        if w == 0 or h == 0 or q + 12 + w * h * 2 > len(blob):
            break
        words = list(struct.unpack_from(f"<{w * h}H", blob, q + 12))
        out[key] = (x, y, words) if key == "clut" else (x, y, w, h, words)
        q += 12 + w * h * 2
    return out


def c15(c: int):
    """PS1 15-bit colour to RGBA; 0x0000 is the PS1's transparent colour."""
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3, 0 if c == 0 else 255)


def indices(t) -> list:
    """Pixel rows of CLUT indices (4 or 8 bpp)."""
    _, _, w, h, words = t["img"]
    if t["bpp"] == 4:
        return [[(words[j * w + i // 4] >> ((i % 4) * 4)) & 15 for i in range(w * 4)] for j in range(h)]
    return [[(words[j * w + i // 2] >> ((i % 2) * 8)) & 255 for i in range(w * 2)] for j in range(h)]


def paint(rows, pal, u=0, v=0, w=None, h=None) -> Image.Image:
    w = w or len(rows[0]) - u
    h = h or len(rows) - v
    im = Image.new("RGBA", (w, h))
    im.putdata([c15(pal[rows[v + j][u + i]]) for j in range(h) for i in range(w)])
    return im


def sha(data) -> str:
    if isinstance(data, Image.Image):
        data = data.tobytes()
    return hashlib.sha256(data).hexdigest()


# SHA-256 of each decoded piece for Tekken 2 (USA) (Rev 1). A mismatch means a
# different release or a damaged image.
EXPECTED = {
    "clut241": "f85ce213f8b697756e40f21c4570ff31e8bd9411f88710ae2ba42c26912fef3d",
    "clut505": "f9b0b638aed25c3bc60445e46a42a1de144e4c7dc0e63c44e5f63b5e22412f7c",
    "clut507": "95ac2cd3d316f3a85a0bacc533acb61bd8b3590db0196b2a095054956e64aa12",
    "font": "e30b848fa2637aa1f6d781721a6f858ac99cb45b7bbe62c75ffc86984bba42aa",
    "optpage": "f19c6bf2dff44ace7d0b0712503474579030fdea5d97487958c65289031ce2ab",
    "title0": "86e1ffeff9ec8eee06347ec6d7b9782e0782eca235afff5869413e7d910def13",
    "title1": "ea7499f2b26ab1dc6c33d69e0e00a34e9c0a6ef2614dbbaa166ef87a00deacad",
    "title2": "0b68e1ad6528f3e0a688337f58c72d9067316df78880549444ef9b125051f492",
    "title3": "abed1192c891f4828290d7b9f263ff2587cd4c56c9d8fc4b7b419c3cc0876bd5",
}


class Pieces:
    """Every source the candidates use, decoded straight from the disc."""

    def __init__(self, disc_path: Path):
        d = Disc(track1_path(disc_path))
        exe = d.file("/SLUS_002.13")
        r15, r17 = bns_record(d, exe, 15), bns_record(d, exe, 17)
        self.blobs = {
            # The HUD and stage-card font: 4 bpp, 256 x 256 px, loaded to VRAM (512, 256).
            "font": lz(exe, 0xF6224),
            # CLUT row (0, 505), 16 palettes of 16: the HUD's small-font colours.
            "clut505": lz(exe, 0xF9C84),
            # CLUT row (0, 507): the stage card's colours ("STAGE", names, "VS").
            "clut507": lz(r17, 0x257A8),
            # The Option Mode texture page: 4 bpp at VRAM (832, 0) and its CLUT row (512, 241).
            "optpage": lz(r17, 0x29798),
            "clut241": lz(r17, 0x2BB90),
        }
        # Record 15 is an archive: count, then {offset, size} pairs; entries 0-3
        # are the title picture's four 128 x 240 strips (8 bpp, own CLUTs).
        count = struct.unpack_from("<I", r15, 0)[0]
        offs = [struct.unpack_from("<II", r15, 4 + i * 8)[0] for i in range(count)]
        for i in range(4):
            self.blobs[f"title{i}"] = lz(r15, offs[i])
        self.t = {k: tim(v) for k, v in self.blobs.items()}

    def pal(self, key: str, slot: int):
        return self.t[key]["clut"][2][slot * 16:slot * 16 + 16]

    def hashes(self) -> dict:
        return {k: sha(v) for k, v in sorted(self.blobs.items())}


# ---------------------------------------------------------------- candidates


def text_run(font_rows, pal, glyphs, cell, advance):
    """Lay glyph cells out the way the game's text routine does: left to right
    by `advance`, drawn right to left so each glyph sits on top of the next."""
    w, h = cell
    run = Image.new("RGBA", (advance * (len(glyphs) - 1) + w, h), (0, 0, 0, 0))
    for k in reversed(range(len(glyphs))):
        u, v = glyphs[k]
        run.alpha_composite(paint(font_rows, pal, u, v, w, h), (k * advance, 0))
    return run.crop(run.getbbox())


# The stage card's palettes for the big font, all in CLUT row (0, 507) from
# BNS record 17 (LZ 0x257A8). Each was seen in GP0 dumps of the stage card
# (attract frame 6395) and matches VRAM at the arcade card (frame 2700).
T2_COLOURS = {
    "vs-red": (2, "VRAM (32, 507), the stage card's 'VS' (red fill)"),
    "names-red": (5, "VRAM (80, 507), the stage card's fighter names (dark fill, red rim)"),
    "blue": (3, "VRAM (48, 507), the stage card's 'STAGE n'"),
}


def t2_big(p: Pieces, colour: str) -> Image.Image:
    """T2 from the stage-card font: cells 36 x 30, T at (36, 160), 2 at (72, 40), advance 30 px."""
    return text_run(indices(p.t["font"]), p.pal("clut507", T2_COLOURS[colour][0]),
                    [(36, 160), (72, 40)], (36, 30), 30)


def candidates(p: Pieces, colour: str = "vs-red") -> dict:
    font = indices(p.t["font"])
    # Stage card: 36 x 30 sprites, advance 30 px, drawn right to left.
    big = t2_big(p, colour)
    # HUD small font: 16 x 10 cells, advance 13 px; CLUT (96, 505) is the title
    # screen's copyright line. "T" at (208, 10), "2" at (32, 0).
    hud = p.pal("clut505", 6)
    small = text_run(font, hud, [(208, 10), (32, 0)], (16, 10), 13)
    small2 = text_run(font, hud, [(32, 0)], (16, 10), 13)

    title = Image.new("RGBA", (512, 240))
    for i in range(4):
        t = p.t[f"title{i}"]
        title.paste(paint(indices(t), t["clut"][2]), (i * 128, 0))
    logo = title.crop((50, 24, 460, 176))  # the logo and its glow; the rest is the plain ground
    title_bg = c15(p.t["title0"]["clut"][2][indices(p.t["title0"])[0][0]])[:3]

    # Option Mode background tile: page (832, 0), u 0 v 176, 128 x 64, CLUT (528, 241).
    opt = paint(indices(p.t["optpage"]), p.pal("clut241", 1), 0, 176, 128, 64)
    opt_bg = max(opt.getcolors(1 << 16))[1][:3]

    return {
        "1-t2-stage-font": {
            "title": f"T2 in the stage-card font ({colour})",
            "native": big, "small": [small, small2], "bg": (0, 0, 0),
            "source": {
                "glyphs": "SLUS_002.13 LZ stream at file offset 0xF6224 (also BNS record 4 at 0x1588): "
                          "4 bpp TIM, VRAM (512, 256), 256 x 256 px; cells 36 x 30, T at (36, 160), 2 at (72, 40)",
                "clut": f"BNS record 17 LZ stream at 0x257A8: CLUT row (0, 507), palette "
                        f"{T2_COLOURS[colour][0]} = {T2_COLOURS[colour][1]}",
                "layout": "advance 30 px, drawn right to left, as the stage card draws 'STAGE 1' "
                          "(GP0 attract frame 6395; matches VRAM at arcade frame 2700)",
                "small sizes": "HUD small font: cells 16 x 10, T at (208, 10), 2 at (32, 0), advance 13 px, "
                               "CLUT (96, 505) from SLUS_002.13 LZ 0xF9C84 (the title screen's copyright line)",
                "background": "black",
            },
        },
        "2-title-logo": {
            "title": "Title logo (鉄拳 TEKKEN2)",
            "native": logo, "small": [], "bg": title_bg,
            "source": {
                "picture": "BNS record 15, archive entries 0-3 (LZ at 0x2C, 0x19E4, 0x3EDC, 0x66A4): "
                           "8 bpp TIMs 128 x 240 at VRAM (512..704, 0) with CLUTs (512, 252..255); "
                           "identical to VRAM at the title menu (arcade frame 1100)",
                "crop": "x 50-459, y 24-175 of the 512 x 240 title picture (the logo and its glow)",
                "background": f"the title picture's own ground colour {title_bg}",
                "note": "鉄拳 cannot be cut out alone: the picture paints TEKKEN2 over the lower third "
                        "of both kanji, so the logo is used whole with the game's own 2",
            },
        },
        "3-option-logo": {
            "title": "Option Mode logo tile",
            "native": opt, "small": [], "bg": opt_bg,
            "source": {
                "texture": "BNS record 17 LZ stream at 0x29798: 4 bpp TIM at VRAM (832, 0); "
                           "tile u 0 v 176, 128 x 64",
                "clut": "BNS record 17 LZ stream at 0x2BB90: CLUT row (512, 241), palette 1 = VRAM (528, 241)",
                "use": "the scrolling background of Option Mode (GP0 option frame 1499, 18 tiles)",
                "background": f"the tile's own ground colour {opt_bg}",
            },
        },
    }


# ---------------------------------------------------------------- rendering


def scaled(im: Image.Image, k: int) -> Image.Image:
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def decimated(im: Image.Image, n: int) -> Image.Image:
    """Every n-th game pixel (nearest neighbour at 1/n), sampled at cell centres."""
    w, h = (im.width + n - 1) // n, (im.height + n - 1) // n
    src, out = im.load(), Image.new("RGBA", (w, h))
    dst = out.load()
    for j in range(h):
        for i in range(w):
            dst[i, j] = src[min(im.width - 1, i * n + n // 2), min(im.height - 1, j * n + n // 2)]
    return out


def art_for(c: dict, avail: int):
    """The art for a box of `avail` px: the native piece at the largest whole
    scale that fits; else the candidate's own small-size piece at 1x; else the
    native piece at 1/n. Returns (image, how)."""
    nat = c["native"]
    k = min(avail // nat.width, avail // nat.height)
    if k >= 1:
        return scaled(nat, k), f"native x{k}"
    for i, piece in enumerate(c["small"]):
        k = min(avail // piece.width, avail // piece.height)
        if k >= 1:
            return scaled(piece, k), f"small piece {i} x{k}"
    n = max(-(-nat.width // avail), -(-nat.height // avail))
    return decimated(nat, n), f"native 1/{n}"


def render(c: dict, size: int, pad=None, transparent=False):
    pad = max(1, size // 32) if pad is None else pad
    art, how = art_for(c, size - 2 * pad)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0) if transparent else (*c["bg"], 255))
    canvas.alpha_composite(art, ((size - art.width) // 2, (size - art.height) // 2))
    return canvas, how


SIZES = (1024, 512, 256, 128, 64, 48, 32, 16)


def write_candidate(key: str, c: dict, root: Path) -> dict:
    d = root / key
    d.mkdir(parents=True)
    c["native"].save(d / "native.png")
    for i, piece in enumerate(c["small"]):
        piece.save(d / f"native-small-{i}.png")
    hows = {}
    for s in SIZES:
        im, how = render(c, s)
        im.convert("RGB").save(d / f"icon-{s}.png")
        hows[s] = how
    meta = {"title": c["title"], "native_size": list(c["native"].size), "source": c["source"],
            "sizes": {str(s): h for s, h in hows.items()}}
    (d / "source.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n")
    return hows


# ---------------------------------------------------------------- platform sets


def build_platforms(c: dict, root: Path, key: str):
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    rgb = lambda s: render(c, s)[0].convert("RGB")  # noqa: E731
    rgb(1024).save(root / "icon-1024.png")
    (root / "SOURCE").write_text(f"candidate {key}: {c['title']}\n"
                                 + json.dumps(c["source"], indent=2, ensure_ascii=False) + "\n")

    mac = root / "macos"
    mac.mkdir()
    rgb(1024).save(mac / "AppIcon-1024.png")
    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as td:
            iconset = Path(td) / "AppIcon.iconset"
            iconset.mkdir()
            for base in (16, 32, 128, 256, 512):
                rgb(base).save(iconset / f"icon_{base}x{base}.png")
                rgb(base * 2).save(iconset / f"icon_{base}x{base}@2x.png")
            subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(mac / "AppIcon.icns")],
                           check=True)
    else:
        print("iconutil not found: skipped macos/AppIcon.icns")

    win = root / "windows"
    win.mkdir()
    wsizes = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
    imgs = [render(c, s)[0] for s in wsizes]
    imgs[-1].save(win / "app.ico", format="ICO", sizes=[(s, s) for s in wsizes], append_images=imgs[:-1])
    imgs[-1].convert("RGB").save(win / "app-256.png")

    # Android: adaptive background (plain) + foreground kept in the 66 dp safe
    # circle of the 108 dp layer; legacy square and round launchers.
    res = root / "android" / "res"
    for name, dens in {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}.items():
        dp = res / f"mipmap-{name}"
        dp.mkdir(parents=True)
        s = round(108 * dens)
        Image.new("RGB", (s, s), c["bg"]).save(dp / "ic_launcher_background.png")
        safe = int(s * 66 / 108 / 1.4143)  # the square that fits inside the safe circle
        fg = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        art, _ = art_for(c, safe)
        fg.alpha_composite(art, ((s - art.width) // 2, (s - art.height) // 2))
        fg.save(dp / "ic_launcher_foreground.png")
        legacy = round(48 * dens)
        sq = render(c, legacy)[0]
        sq.save(dp / "ic_launcher.png")
        mask = Image.new("L", (legacy, legacy), 0)
        ImageDraw.Draw(mask).ellipse((0, 0, legacy - 1, legacy - 1), fill=255)  # hard edge, no antialiasing
        rnd = Image.new("RGBA", (legacy, legacy), (0, 0, 0, 0))
        rnd.paste(sq, (0, 0), mask)
        rnd.save(dp / "ic_launcher_round.png")
    anydpi = res / "mipmap-anydpi-v26"
    anydpi.mkdir()
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
           '    <background android:drawable="@mipmap/ic_launcher_background"/>\n'
           '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
           '</adaptive-icon>\n')
    (anydpi / "ic_launcher.xml").write_text(xml)
    (anydpi / "ic_launcher_round.xml").write_text(xml)
    rgb(512).save(root / "android" / "play-store-512.png")

    ios = root / "ios" / "AppIcon.appiconset"
    ios.mkdir(parents=True)
    entries = [("iphone", 20, 2), ("iphone", 20, 3), ("iphone", 29, 2), ("iphone", 29, 3),
               ("iphone", 40, 2), ("iphone", 40, 3), ("iphone", 60, 2), ("iphone", 60, 3),
               ("ipad", 20, 1), ("ipad", 20, 2), ("ipad", 29, 1), ("ipad", 29, 2),
               ("ipad", 40, 1), ("ipad", 40, 2), ("ipad", 76, 1), ("ipad", 76, 2),
               ("ipad", 83.5, 2), ("ios-marketing", 1024, 1)]
    images, done = [], set()
    for idiom, pt, sc in entries:
        ptn = f"{pt:g}"
        fn = f"AppIcon{ptn}x{ptn}" + (f"@{sc}x" if sc > 1 else "") + ".png"
        if fn not in done:
            rgb(round(pt * sc)).save(ios / fn)
            done.add(fn)
        images.append({"filename": fn, "idiom": idiom, "scale": f"{sc}x", "size": f"{ptn}x{ptn}"})
    (ios / "Contents.json").write_text(json.dumps(
        {"images": images, "info": {"author": "xcode", "version": 1}}, indent=2) + "\n")


# ---------------------------------------------------------------- contact sheet


def contact_sheet(cands: dict, hows: dict, default: str, dest: Path):
    try:
        f_big, f_small = ImageFont.load_default(size=28), ImageFont.load_default(size=18)
    except TypeError:  # Pillow without FreeType
        f_big = f_small = ImageFont.load_default()
    W, row_h, gap = 3000, 1024 + 140, 40
    sheet = Image.new("RGB", (W, len(cands) * (row_h + gap) + 60), (236, 236, 236))
    d = ImageDraw.Draw(sheet)
    ink = (20, 20, 20)
    for r, (key, c) in enumerate(cands.items()):
        y0 = 30 + r * (row_h + gap)
        name = f"{key}: {c['title']}" + ("  (default)" if key == default else "")
        d.text((30, y0), name, fill=ink, font=f_big)
        y = y0 + 50
        # native at 1x, then 4x for reading the pixels
        x = 30
        d.text((x, y), f"native {c['native'].width} x {c['native'].height}, 1x", fill=ink, font=f_small)
        sheet.paste(c["native"].convert("RGB") if c["native"].mode != "RGBA" else
                    Image.alpha_composite(Image.new("RGBA", c["native"].size, (*c["bg"], 255)), c["native"]).convert("RGB"),
                    (x, y + 26))
        z = 4 if c["native"].width * 4 <= 900 else 2
        d.text((x, y + 60 + c["native"].height), f"native, {z}x", fill=ink, font=f_small)
        zoomed = scaled(Image.alpha_composite(Image.new("RGBA", c["native"].size, (*c["bg"], 255)), c["native"]), z)
        sheet.paste(zoomed.convert("RGB"), (x, y + 86 + c["native"].height))
        # 1024 render
        x = 960
        d.text((x, y), f"1024 ({hows[key][1024]})", fill=ink, font=f_small)
        sheet.paste(render(c, 1024)[0].convert("RGB"), (x, y + 26))
        # small sizes: 1x on the first line, 4x (nearest) under the ones up to 64
        x = 2020
        for s in (128, 64, 48, 32, 16):
            im = render(c, s)[0].convert("RGB")
            d.text((x, y), f"{s}", fill=ink, font=f_small)
            sheet.paste(im, (x, y + 26))
            if s <= 64:
                sheet.paste(scaled(im, 4), (x, y + 26 + 128 + 16))
            d.text((x, y + 26 + 128 + 16 + (s * 4 if s <= 64 else 0) + 4), hows[key][s], fill=ink, font=f_small)
            x += max(s, s * 4 if s <= 64 else 0) + 24
        # Dock and taskbar mocks (sheet only)
        mx, my = 2020, y + 560
        d.text((mx, my - 26), "Dock and taskbar mock", fill=ink, font=f_small)
        d.rounded_rectangle((mx, my, mx + 940, my + 150), 28, fill=(198, 200, 206))
        for i in range(6):
            bx = mx + 24 + i * 152
            if i == 2:
                sheet.paste(render(c, 128)[0].convert("RGB"), (bx, my + 11))
            else:
                d.rounded_rectangle((bx + 4, my + 15, bx + 124, my + 135), 24, fill=(150, 152, 160))
        ty = my + 170
        d.rectangle((mx, ty, mx + 940, ty + 48), fill=(32, 32, 36))
        for i in range(8):
            bx = mx + 16 + i * 52
            if i == 3:
                sheet.paste(render(c, 32)[0].convert("RGB"), (bx, ty + 8))
            else:
                d.rectangle((bx + 4, ty + 12, bx + 28, ty + 36), fill=(90, 90, 98))
        d.rectangle((mx + 480, ty - 2, mx + 940, ty + 50), fill=(32, 32, 36))
        sheet.paste(render(c, 48)[0].convert("RGB"), (mx + 500, ty))
        d.text((mx + 560, ty + 14), "48 px", fill=(230, 230, 230), font=f_small)
    dest.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(dest)


def red_sheet(p: Pieces, dest: Path):
    """The stage card's real palettes for T2, side by side: native, 8x and 128 px."""
    try:
        f_big, f_small = ImageFont.load_default(size=26), ImageFont.load_default(size=17)
    except TypeError:
        f_big = f_small = ImageFont.load_default()
    keys = ["vs-red", "names-red", "blue"]
    col = 700
    sheet = Image.new("RGB", (col * len(keys) + 40, 640), (236, 236, 236))
    d = ImageDraw.Draw(sheet)
    ink = (20, 20, 20)
    for i, key in enumerate(keys):
        x = 20 + i * col
        art = t2_big(p, key)
        label = key + (" (default)" if key == "vs-red" else "")
        d.text((x, 20), label, fill=ink, font=f_big)
        slot, what = T2_COLOURS[key]
        d.multiline_text((x, 56), f"BNS record 17, LZ 0x257A8, CLUT row (0, 507) palette {slot}\n{what}",
                         fill=ink, font=f_small)
        y = 110
        bg = Image.new("RGBA", art.size, (0, 0, 0, 255))
        sheet.paste(Image.alpha_composite(bg, art).convert("RGB"), (x, y))
        d.text((x + art.width + 10, y + 6), "native", fill=ink, font=f_small)
        big = scaled(Image.alpha_composite(bg, art), 8).convert("RGB")
        sheet.paste(big, (x, y + 44))
        sheet.paste(render({"native": art, "small": [], "bg": (0, 0, 0)}, 128)[0].convert("RGB"),
                    (x, y + 44 + big.height + 20))
        d.text((x + 140, y + 44 + big.height + 20), "128 px", fill=ink, font=f_small)
    sheet.save(dest)


# ---------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--disc", required=True, type=Path, help="Tekken 2 (USA) (Rev 1) .cue or track 1 .bin")
    ap.add_argument("--out", type=Path, default=GAME / "scratch" / "icon-extracted",
                    help="raw pieces, renders and contact sheet (gitignored)")
    ap.add_argument("--platforms", type=Path, default=GAME / "assets" / "icon" / "default",
                    help="platform icon sets (gitignored)")
    ap.add_argument("--default", default="1", help="candidate number for the platform sets")
    ap.add_argument("--t2-colour", choices=sorted(T2_COLOURS), default="vs-red",
                    help="stage-card palette for candidate 1 (default: vs-red, the stage card's VS)")
    ap.add_argument("--no-platforms", action="store_true")
    ap.add_argument("--print-hashes", action="store_true", help="print the decoded pieces' SHA-256 and exit")
    args = ap.parse_args()

    p = Pieces(args.disc)
    got = p.hashes()
    if args.print_hashes:
        print(json.dumps(got, indent=2))
        return
    bad = [k for k, h in EXPECTED.items() if got.get(k) != h]
    if bad:
        sys.exit("this disc's data does not match Tekken 2 (USA) (Rev 1): " + ", ".join(bad))

    cands = candidates(p, args.t2_colour)
    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)
    hows = {k: write_candidate(k, c, args.out) for k, c in cands.items()}
    default = next(k for k in cands if k.startswith(args.default + "-"))
    contact_sheet(cands, hows, default, args.out / "contact-sheet.png")
    red_sheet(p, args.out / "contact-sheet-red.png")
    (args.out / "pieces.sha256.json").write_text(json.dumps(got, indent=2) + "\n")
    print(f"pieces and renders: {args.out}")
    if not args.no_platforms:
        build_platforms(cands[default], args.platforms, default)
        print(f"platform sets ({default}): {args.platforms}")


if __name__ == "__main__":
    main()
