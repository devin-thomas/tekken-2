# The settings panel's font

| File | Face | Licence | Source |
| --- | --- | --- | --- |
| `NotoSans-Regular.ttf` | Noto Sans 2.000, Copyright 2012 Google Inc. | SIL Open Font License 1.1 (`LICENSE-NotoSans-OFL.txt`) | the GGX+ port's subset |

The file is subset to Basic Latin, Latin-1, the General Punctuation dashes,
quotes and ellipsis, the euro and trade mark signs, with its kerning kept:

    pyftsubset <font> --unicodes="U+0020-007E,U+00A0-00FF,U+2010-2027,U+2030-2033,U+2039-203A,U+20AC,U+2122" --layout-features=kern --name-IDs='*' --notdef-outline

CMake embeds it in the runtime (`t2_font_data.c`, generated at configure
time), so no package script has to ship it. Its name table keeps the
copyright and licence notice, which the OFL accepts for embedded copies.
