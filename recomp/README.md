# Tekken 2 Recompiled

Tekken 2's PlayStation code translated to C with
[psxrecomp](https://github.com/mstan/psxrecomp) and compiled into a native
app for Mac and Windows. To play, download a build from the
[releases page](../../../releases). This page is for building it yourself.

The repo holds no game data. The build reads the game's code from your own
disc image, so you need one:

- Tekken 2 (USA) (Rev 1), SLUS-00213, as a BIN/CUE image with all three
  tracks. Track 1 SHA-1: `699bd5831fbe3b662c59906ebf0acd3d1c3393f3`.

No BIOS is needed: psxrecomp's OpenBIOS is built in.

## Get the source

```
git clone --recurse-submodules https://github.com/devin-thomas/tekken-2.git
cd tekken-2/recomp
```

`psxrecomp/` is a submodule pinned to the upstream commit this release was
built and tested on. Tekken 2's own changes to it are in `patches/`, and the
build applies them for you.

Put your disc image in `recomp/disc/` (gitignored) as
`Tekken 2 (USA) (Rev 1).cue` with its BIN files, or pass its path to the
generate step.

## Mac (Apple silicon)

Needs the Xcode command line tools, Python 3.11 or later, and CMake, Ninja
and SDL3 (with Homebrew: `brew install cmake ninja sdl3`).

```
bash scripts/generate.sh game.toml "disc/Tekken 2 (USA) (Rev 1).cue"
bash scripts/build.sh game.toml Release build-release
```

The program is `build-release/Tekken_2_Recompiled`. To build the app and zip
the way the releases are made:

```
bash scripts/release.sh --out ../out --disc "disc/Tekken 2 (USA) (Rev 1).cue"
```

## Windows (x64)

Needs Python 3.11 or later. The scripts fetch psxrecomp's toolchain pack
(clang, CMake, Ninja, SDL3) the first time; add `-Msvc` to use Visual Studio
2022 instead. In PowerShell:

```
pwsh -File scripts\generate.ps1 -Disc "disc\Tekken 2 (USA) (Rev 1).cue"
pwsh -File scripts\build.ps1 -BuildType Release -BuildDir build-release
```

For the release zip:

```
pwsh -File scripts\release.ps1 -Out ..\out -Disc "disc\Tekken 2 (USA) (Rev 1).cue"
```

## Tests

Build every target first, then run them:

```
cmake --build build-release
ctest --test-dir build-release
```

## Licence

Tekken 2 Recompiled's own code is under the MIT licence (`../LICENSE`).
psxrecomp is under the PolyForm Noncommercial License 1.0.0 with its
additional permissions; see `../NOTICE.md`.
