# Bristles.h: bristle maps

The named values in `src/core/stroke/Bristles.h`: what they are, what they do, and how they were chosen. The values themselves
are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## `kHairSpreadShare`

**What it does.** How far the map reaches, its longest side against the tool's size, its share at that pressure as the stamp's
is.

**How it was chosen.** Fitted on the reference's Flat Bristle at 400 px, where its one light line is a gap between two hairs
whose edges stand clear:

| Tool | Band across | Gap in from its outer side | Gap width |
| --- | --- | --- | --- |
| Pencil (pressure 0.76 there) | 328 px | 64 px | 10 px |
| Brush (its Min Size keeps it wider there) | 374 | 69 | 16.5 |

Each gives a map column 5.69 / 6.58 px apart and a hair 29.8 / 29.5 px wide at the pen's pressure there (kHairWidthShare is now
the full width).

The result: 1.195. The two bands give 1.18; 1.195 lines the C's 100 px Pencil band up to the pixel, and 1.15 to 1.25 were tried
on the fitted cases.

## `kHairWidthShare`

**What it does.** A hair's full width against the tool's Size, at Hair 100 % and full pressure. Hair scales it straight, and
with Size on pressure, Hair Min's rule thins it with the pressure.

**How it was chosen.** Fitted on the pack's bristle cases (Bristle and Flat Bristle, the C at 35, 100 and 400 px, Shape 1),
painted at the reference's starting Hair, 50, as a width at Hair 50. Levels over the Brush, Pencil, Water Color and Eraser (the
Eraser, its Size off pressure, closest at 0.090 too):

| Width at Hair 50 | 0.074 | 0.085 | 0.090 | 0.095 | 0.100 | 0.105 | 0.115 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Levels | 14.67 | 9.76 | 9.04 | 9.19 | 9.79 | 10.66 | 13.05 |

The width at 100 is twice that, as the reference's Hair sweep has it (a line at pressure 0.75):

| Hair | 10 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| One-hair map's coverage, the reference's (px) | 1.0 | 3.5 | 6.5 | 10.4 | 14.2 |
| One-hair map's coverage, ours (px) | 1.4 | 3.4 | 6.7 | 10.1 | 13.5 |
| Flat Bristle's on the Pencil, the reference's | 24.7 | 62.0 | 76.8 | 83.6 | 87.3 |
| Flat Bristle's on the Pencil, ours | 28.0 | 59.2 | 77.5 | 84.1 | 87.4 |

The Brush's are alike.
