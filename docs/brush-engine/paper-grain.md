# PaperGrain.cpp: paper textures

The named values in `src/core/stroke/PaperGrain.cpp`: what they are, what they do, and how they were chosen. The values
themselves are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## `kTurnColumnSets`

**What it does.** When a picture is turned over for erasing, this sets its top and bottom:

- **The top** is the lowest of the lightest greys of the picture's columns taken kTurnColumnSets apart. Any set of every 8th, or
  every 4th, column held low lowers it; rows don't.
- **The bottom** is its darkest grey when its width is a whole number of kTurnColumnSets, else black (a floor of 60 counts at
  512 px, not at 510).

**How it was chosen.** Found from probe textures, the exception of 2026-11-21.
