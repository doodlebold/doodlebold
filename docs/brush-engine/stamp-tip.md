# StampTip.cpp: stamps

The named values in `src/core/stroke/StampTip.cpp`: what they are, what they do, and how they were chosen. The values themselves
are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## Taps and reach

### `kLongestTap`

**What it does.** In px: a pen that travels less than this has tapped, and no stamp is laid before it. Only the Pencil's tip
reads it, as every other tip walks its taps.

**How it was chosen.** Fitted to the reference's taps and dots. Every value from 0.85 to 2 draws a 2 px touch as the reference's
dash and keeps the resting taps as they were; 1.5 is the middle.

| Size | 1 | 2 | 4 |
| --- | --- | --- | --- |
| RMS levels, 0.85 to 2 | 4.3 | 5.4 | 13.8 |
| RMS levels, at 3 | 16.5 | 27.2 | 34.4 |

### `kReachSpare`

**What it does.** In px: a cell passed as out of a stamp's reach is this much further than its reach, as the band test's spare.

**How it was chosen.** The least tried that paints the same:

- 0.5 moves the Pencil's, Eraser's and Clone Stamp's grid (from 500 px) by 1 in 65535 at any lot;
- 0 moves the 1500 px Pencil's last bits when samples go one or nine at a time;
- 2 paints the same.

## Skirts

### `kSkirtWidth`

**What it does.** A stamp's skirt (StampTip::skirtNeighbours): a Gaussian of kSkirtWidth of its step past the radius, less what
it has left at kSkirtReach steps, where it ends.

**How it was chosen.** Fitted to the reference's three stamps 30 px apart at 600 px, whose skirts make its rims: within 0.02 of a
stamp's density all along both rims, at most.

- **Why a Gaussian:** it's one number to fit, our own form rather than the reading's table, and flat where it starts, so the
  whole stamp and its skirt meet without a crease.
- **The reading's table itself,** read between its points, fits within 0.015.
- **A smoothstep** falling over 1.8 steps falls too slowly near the outline: 0.97 and 0.74 at 0.2 and 0.6 steps, the reading's
  0.92 and 0.58.

## Reading a cell between its points

Three tests decide whether a cell is read between its points or worked out per pixel. All three were measured on 2026-11-13 and
rechecked on 2026-11-15.

### `kStampSmoothRange`

**What it does.** A cell is read between its points where its middle pixel's coverage, before the stroke's opacity, differs from
the reading there by no more than this: an 8-bit level.

**How it was chosen.**

- Half a level cuts the Pencil's, Eraser's and Clone Stamp's flecks past a level by three quarters for 1 to 2.5 % more per-pixel
  work, still to be weighed.
- 2 to 8 levels let larger ones through.

### `kStampShapeSpread`

**What it does.** A cell the outline crosses is read between its points only where the outline's share differs between them by
no more than this: a blurred outline's, as the Clone Stamp's softest Shape has, not a hard one's, which turns within a cell.

**How it was chosen.**

- On the 5th Shape, from 1/64 to 1/4, the flecks move little at no measurable work, 1/64 trimming the Pencil's few past 2
  levels.
- On the soft Shapes it sets the work: 1/64 about doubles the cells worked out per pixel and 1/4 halves them, with the look within
  a level of the per-pixel model either way. Still to be weighed.

### `kStampSpreadCap`

**What it does.** Nor is a cell read between its points where its points' coverage differs by more than this, 16 8-bit levels: a
smooth ramp stays read between them, but a step near a corner, a quarter of itself at the middle, goes per pixel.

**How it was chosen.**

- 16 catches few cells the other two tests don't, but among them, on the 5th Shape from 1000 px, flecks of up to 14 levels that
  32 lets through.
- 8 holds the Eraser's and Pencil's worst flecks to 3.6 levels for 0.1 to 3 % more work over the reference set and up to 22 % on
  the 5th Shape at 1000 px, still to be weighed.

## Bristle hairs

### `kHairToneExponent`

**What it does.** A hair is crossed by fewer of the tool's stamps than a line's middle is, so each stamp lays more: what the
round stamp's solved opacity lays as density, times (stamp radius / hair radius) to this power at kHairToneSize. The Pencil and
Eraser lay all but solid.

**How it was chosen.** With each hair a run from its last place, it's crossed by more of them than as discs. Refitted on the
Brush's and Water Color's bristle cases (54):

| Exponent | 0.85 | 0.7 | 0.6 |
| --- | --- | --- | --- |
| Levels | 14.5 | 13.0 | 13.9 |

As discs, 0.85 was fitted on Water Color at 100 px.

### `kHairFollowShare`

**What it does.** Direction Auto: over how much path, as a share of the tool's size, the map's turn closes all but 1/e of its gap
to the stroke's way. So it follows the way smoothly, a little behind at a sharp corner, not snapping to each piece's.

**How it was chosen.** Fitted on the reference's bristle-settings strokes (Direction Auto and the Detect Turn plates, at Hair
100, mean |difference| of alpha over pixels either paints):

| Share | 0 | 0.02 | 0.05 | 0.1 | 0.2 |
| --- | --- | --- | --- | --- | --- |
| Levels | 13.31 | 12.90 | 12.23 | 13.18 | 16.00 |

On the pack's bristle cases (Shape 1), 0.05 is the best too:

| Share | 0 | 0.05 | 0.1 | 0.2 | 0.4 |
| --- | --- | --- | --- | --- | --- |
| Brush, levels | 7.67 | 7.54 | 7.62 | 7.88 | 9.10 |

### `kHairCornerTurn`, `kHairCornerWindow`, `kHairSettleTurn`, `kHairSettleWindow`

**What they do.** Detect Turn:

- Where the stroke's way turns by kHairCornerTurn or more, radians (45 degrees), within kHairCornerWindow of the tool's size of
  path, the hairs end, cut off, the map not sweeping round the corner.
- They stay cut until the way turns less than kHairSettleTurn, radians (15 degrees), over kHairSettleWindow of the size of path
  since the corner and on to the next pen sample.
- Then they start afresh along it, already turned.

They're against the size, as the reference's are: its 14 px hairpin is cut at 100 px and not at 35 px, its 120 and 150 degree
zig-zag corners at both.

**How they were chosen.** Fitted where it's on, at Stabilizer 0 on the Detect Turn plates and at Stabilizer 11 on the Stabilizer
11 strokes (mean |difference| of alpha, levels), the settle at 15 degrees:

| Corner | Window | Stabilizer 0 | Stabilizer 11 |
| --- | --- | --- | --- |
| 45 degrees | 0.3 | 13.26 | 27.11 |
| 60 degrees | 0.3 | 13.12 | 29.00 |
| 90 degrees | 0.3 | 13.70 | 29.27 |
| 45 degrees | 0.15 | 12.77 | 26.18 |
| 45 degrees | 0.2 | 12.77 | 27.11 |
| 45 degrees | 0.25 | 12.77 | 27.11 |

At 45 degrees with windows of 0.15, 0.2 and 0.25, the 35 px hairpins are as with it off at all three, where at 0.3 they were
cut. A settle of 7.5 or 20 to 30 degrees was worse at both levels.

### `kHairWanderMost`, `kHairLagMost`

**What they do.** Randomize: a hair's wander takes a new random aim every this many of the tool's sizes of path, eased between.
At Randomize 20 a hair in the map's middle wanders up to kHairWanderMost of half the map's span. Each hair also lags the map's
turn by up to kHairLagMost more of the follow length.

**How they were chosen.** Fitted on the reference's five-hair map's straight lines at 100 px (the Pencil's and the Brush's, three
strokes each at Randomize 10 and 20):

- its middle hair moves across the line by 0.5 px (std; at most 2.2) at 20 and 0.2 px at 10, and decorrelates over 112 px of
  path;
- at 1.0 and 0.05 ours moved 0.97 px and decorrelated over 45 px.

The lag, which straight lines can't show, is untuned.
