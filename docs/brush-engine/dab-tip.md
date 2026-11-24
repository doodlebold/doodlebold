# DabTip.cpp: the mixing tools' dabs

The named values in `src/core/stroke/DabTipEvents.inc` and `src/core/stroke/DabTipValues.inc`: what they are, what they do, and
how they were chosen. The values themselves are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## Contents

- [Marker](#marker)
- [Dabs](#dabs)
- [Blot maps](#blot-maps)
- [The Brush's grid and edges](#the-brushs-grid-and-edges)
- [The Brush laid in groups](#the-brush-laid-in-groups)
- [Water Color laid in groups](#water-color-laid-in-groups)

## Marker

### `kMarkerEdgeShiftSmall`, `kMarkerEdgeSmallRadius`, `kMarkerEdgeLargeRadius`, `kMarkerEdgeFullRadius`, `kMarkerEdgeBlur`

**How the reference's Marker lays a group.** It lays a group through one mask:

- Each pixel its stamps reach takes their shares together, each its opacity times its cover, taking its share of what's left, in
  whole numbers (kMaskOne, shared with the direct lay).
- A pixel's top is the highest of the stamps' that ever reached it and never falls.
- The pixel moves toward the colour the group carries, at an alpha of the higher of its top and its own, by the mask: over paint
  a faint group tints it, over nothing a click reaches Density times Density.

**What they do.** They set the Marker's soft Shapes' edge. Each stamp's profile is read kMarkerEdgeShiftSmall / Large px further
in (the earlier kMarkerEdgeShift in RebuiltStroke.cpp sets the 5th Shape's outline, which these Shapes don't take), and blurred
by kMarkerEdgeBlur px: the profile's edge around its half point is read as wide as its own width and the blur together (the root
of the sum of their squares), as the Brush's dab is.

- **kMarkerEdgeShiftSmall / Large:** the shift at a nominal radius of kMarkerEdgeSmallRadius and at kMarkerEdgeLargeRadius and
  above, straight between.
- **Below kMarkerEdgeFullRadius** the shift and the blur shrink with the radius.

**Why.** On the reference's C (Size 35 / 50 / 100 / 200, starting settings, pen pressure as recorded) ours lay the 2nd to 4th
Shapes' edges out by a fixed amount at every size, 0.85 / 0.5 / 0.3 px at half the line's top on the 2nd / 3rd / 4th, and steeper
at their foot than the reference's; the 1st Shape's 0.3 px out at 35 px and 0.9 px in at 200 px.

**How they were chosen.**

- **The smallest radii:** on the C the Marker's size follows pressure (0.48 to 0.78), so at 35 px its stamps run from about that
  radius up and the fit covers them; the reference wasn't measured on smaller ones. Held whole there, a 4 px line on the 2nd
  Shape lost its top (its half band 10,421 -> 895 px) and grew a faint halo (8,213 -> 25,568), the blur far wider than the
  stamp's own edge; shrinking from 17.5 px instead moved the 35 px C (2nd Shape 1.18 -> 1.56).
- **The shifts,** fitted with the blur at Sharpness 50 on the four sizes: the 2nd Shape's best 0.9 / 0.85 / 0.6 / 0.3 px at 35 /
  50 / 100 / 200 px with a 1 px blur, the 3rd's 0.8 / 0.95 / 0.75 / 0.3, the 4th's 0.45 / 0.5 / 0.6 / 0.45 with 0.5 px (blur 0.5
  / 1.0 / 1.5 tried with each: on the 4th 1.0 laid the small sizes 1 px too wide, the stamps' faint parts adding up along the
  line).
- **The 1st Shape:** 0.2 / 0.12 at the small end (35 px 0.46 / 0.87, 50 px 0.58 / 0.54) and -0.8 / -1.1 at the large (200 px
  1.81 / 1.63).
- **Rejected:** widening the edge in proportion to the size in place of the blur (1.1 / 1.2 times) laid the large sizes far too
  wide (+2.5 px at 200 px), the faint parts of the many stamps across the line adding up.

The four Shapes on the C at Sharpness 50, before -> after:

| Shape | 35 px | 50 px | 100 px | 200 px |
| --- | --- | --- | --- | --- |
| 1st | 1.83 -> 0.46 | 0.73 -> 0.58 | 1.44 -> 1.04 | 2.52 -> 1.63 |
| 2nd | 6.06 -> 1.18 | 4.28 -> 1.11 | 2.29 -> 1.11 | 1.57 -> 1.40 |
| 3rd | 3.70 -> 1.06 | 3.95 -> 0.98 | 1.95 -> 0.58 | 0.98 -> 0.73 |
| 4th | 2.73 -> 0.66 | 2.32 -> 0.50 | 1.85 -> 0.74 | 1.10 -> 0.65 |

The Marker's 35 readings went 5.970 -> 5.938: two better (its blot maps' ramps on the 1st and 3rd Shapes), none worse.

### `kMarkerBlendingCurveAbove50`

**What it does.** Above Blending 50 % the Marker's brush colour falls away faster, and what it carries follows what's under it
sooner: the reference's Marker at the Pastel settings carries far more of the field than the shared curve gives.

**How it was chosen.** Fitted on the reference's plain C stroke over the field at Blending 60, 70, 85, 95 and 100 %, all else at
the Pastel settings: the curve's power at each of these Blendings, straight between them and held above the last, and the
carry's keep per stamp, its power rising evenly to 1.3 times at 100 %.

| Blending | 60 % | 70 % | 85 % | 95 % | 100 % |
| --- | --- | --- | --- | --- | --- |
| Levels off | 3.83 | 3.53 | 2.69 | 2.45 | 4.38 |

That's against 28.18 at 85 % with the shared curve. Every Marker reading at 50 % or less is as it was.

### `kMarkerSpacingDiameter`

**What it does.** How many of its stamps stand for one of the reference's, by size. The reference spaces its Marker stamps
further apart, as a share of their size, the smaller they are, so a pixel of a narrow Marker takes fewer whole steps. The count is
kMarkerStampsPerOldStamp times (diameter / kMarkerSpacingDiameter) to the power -kMarkerSpacingPower, levelled off at
kMarkerSpacingLeast of it at large sizes.

**How it was chosen.**

- **The 1st Shape's diameter and the power** were fitted to the reference's 40 px case and the 20 lines at 200 px together:

  | Diameter and power | 40 px case, within | Lines, within (levels) |
  | --- | --- | --- |
  | 85 px and 0.9 | 0.009 of the reference's 0.715, 0.409 and 0.132 | 0.47 |
  | 100 px and 0.7 | 0.010 | 0.97 |
  | 85 px and 1.0 | 0.019 | 0.36 |
  | 100 px and 0.5 | 0.026 | 1.29 |

- **The floor:** the ring test (426 px) holds from 0 to 0.35 and fails at 0.5 (0.027 a step). The Marker's readings are the same
  from 0 to 0.35, as none reaches the sizes it acts at (from about 500 px).

### `kMarkerKeepPerStamp`

**What it does.** How much of Persistence a Marker stamp's mix counts for in what the Marker carries, now that its brush colour
goes into the carry.

**How it was chosen.** Fitted on the Marker's readings (its readings, the flat red line, the C stroke and the field):

- their mean 14.15 levels off at 0.08, against 15.94 before the reorder;
- 0.06 13.43 and 0.07 13.57, but the flat red line at 400 px 12.7 and 8.6 there, 4.7 here;
- 0.03 16.09, 0.5 26.37, and a whole stamp's worth (1) leaves it 48.8 and 71.6 off.

### `kMarkerEventSpacing`

**What it does.** Laid in groups, the Marker's stamps lie as far apart as the reference's, as a share of their diameter by Shape
(1st to 5th). On the 1st to 4th Shapes the diameter is the soft profile's reach.

**How it was chosen.** Matched to the reference's stamp counts on the C at Stabilizer 3:

- the 1st at 50 px: 646 stamps against 646;
- the 2nd to 4th at 200 px: 350 / 583 / 957 against 350 / 583 / 958 (without the profile, 0.205 / 0.0939 / 0.0563 / 0.0343 gave
  the same counts);
- the 5th keeps kMarkerSpacing's 2.35 %, which the reference's counts at 689 / 1500 / 4000 px are exactly (406 / 186 / 70
  against 406 / 187 / 70).

### `kMarkerRecentFollow`

**What it does.** How far the stamps recent events brought follow each new event's count. Below the Shape's most, a group holds
that many, rounded down, so a run of events with few stamps keeps the groups small a while, as the reference's.

**How it was chosen.** Fitted to the reference's group counts (above):

| Follow | 0.2 | 0.3 | 0.5 | 1.0 | Notes |
| --- | --- | --- | --- | --- | --- |
| 4000 px | 70 | 70 | 70 | 56 | |
| 1500 px | 82 | 79 | 77 | 57 | (70, 73) |
| 50 px | 280 | 270 | 266 | 262 | (264) |

Rounded to the nearest instead, 4000 px never comes to one stamp a group.

### `kMarkerKeepCountPower`

**What it does.** The carry's count of a group's stamps, to this power: a group of n stamps keeps Persistence to
kMarkerKeepPerStamp n^1.1 (with the spacing above), so a fast stroke's full groups let go of a little more each stamp than a slow
stroke's groups of one or two. The slow strokes don't move.

**How it was chosen.** Fitted on the 36 Marker readings and the kept Pastel fits over the field:

| Power | 1 | 1.1 | 1.15 | 1.2 | The build before |
| --- | --- | --- | --- | --- | --- |
| The 36 readings | 8.57 | 8.20 | 8.15 | 8.14 | |
| The Pastel at Blending 85 % | 6.14 | 3.03 | 4.82 | 7.04 | 2.69 |
| The Marker's plain C at 50 px | 8.98 | 5.73 | 4.28 | 3.35 | 4.47 |

- 1.1 keeps every kept Pastel fit within 0.35 of the build before or better.
- A rate raised for every group instead (1.5 times) mends the flat red at 400 px but loses the slow strokes' colour (marker/cross
  10.6 -> 24.4).

### `kMostMarkerCover`

**What it does.** How far a whole Marker stamp moves a pixel's colour toward the dab's paint over paint: its paint laid counts as
if its cover were at most this, -ln(1 - 0.999) = 6.9, not kSolidDensity's 12.

**How it was chosen.** Against the reference's Marker over flat paint the value is nearly flat from 0.5 to 1.0, a lower cap
bringing the colour over paint closer and the colour past it further off: no test settles 0.999.

## Dabs

### `kStampsPerDab`

**What it does.** By edge, from the softest: a fast event's stamps mix in dabs of at most this many, but the Brush's (below). It
binds only on an event of more stamps: the recorded C stroke on a 3000 px canvas splits at 50 and 200 px, from 600 px not (but
Water Color's 5th Shape at 600).

**How it was chosen.**

- **Water Color:** the 5th Shape's 4 is fitted to the reference's quick line (4 to 6 fit alike) and its 3rd's 8 is in the best
  band (8 and up). Its readings, on the 5th Shape, score 5.02 to 5.10 levels from 2 to 8.
- **Blur and Smudge** paint the same at every value from 1 stamp a dab to a whole event (Blur within 0.1 of a level, Smudge
  exactly), so the values aren't theirs.
- **The Marker's readings** (5th Shape, where it splits 1 of 33 events of the quick line) score 17.96 from 2 to 8.
- **Untuned:** no reading reaches Water Color's 1st, 2nd and 4th Shapes or the Marker's 1st to 4th. There, halving or doubling a
  Shape's value moves the C stroke at 50 and 200 px by 1.2 to 2.4 levels on average for Water Color and up to 24 for the Marker;
  halving costs Water Color 13 to 30 % more time at 200 px and doubling saves up to 12 %.

### `kAlongRatioSteps`, `kAlongKneeShape`, `kWholeAlongShape`

**What they do.** The outline along the path (alongTable): kept at this many places out from a stamp's centre, worked out from as
many steps each along the line through the place, and kept for ramps that differ by 1 / kAlongRatioSteps of the radius.

- A straight pass gives each pixel the stamp's side profile up to kAlongKneeShape, and further in its -ln(1 - shape) goes on
  rising as steeply as it does there, so no stamp's outline along the path has a sharp ring that a start or an end would show.
- From kWholeAlongShape the pixel counts as whole, as one a stamp's core covers did before (kept).

**How they were chosen.**

- **The knee:** lower fits the reference's donut better (its three Shapes' levels off, added up):

  | Knee | 0.93 | 0.95 | 0.999 |
  | --- | --- | --- | --- |
  | Levels off, added up | 10.79 | 11.13 | 11.61 |

  But from 0.92 down a straight line's end ripples (0.75 of a level at 0.92, 1.1 at 0.9) and its side dips (252 at 0.9); 0.95 is
  inside the band with neither, 0.93 to 0.999. An earlier cut at 0.999 showed rings in its frames; the finished line shows
  none at 0.999 now.
- **The whole:** from 0.99 to 0.9999 the soft Shapes at 1500 px move by 3 levels at most and the donut by 0.04, so 0.995 is a
  middle choice of speed: 0.99 paints 1 to 7 % faster, 0.999 and up 0 to 6 % slower. At 0.95 the side turns solid out to 120 px
  from the path (255 for 247).
- **The table's three sizes:** against the reference's donut and the Brush's readings, 128 places score worse and 512 within
  0.005 of a level; twice the steps moves the donut by 1 in 65535 at most and no score; ratios kept to 1/256 score within 0.006
  of a level of exact ones, and 128 worse.

### `kEmptyAlpha`

**What it does.** A pixel fainter than this counts as empty when picked up: alpha 2 of 255 is empty, 3 is paint, as the
reference's Brush has it. Below Blur Pressure the Brush tests the average of its pickup against it, so a wash at alpha 3 counts as
empty there when any of the pickup lies off it.

**How it was chosen.** Fitted to the reference's drag-out readings: dragged out of red at alpha 1 and 2 the Brush lays within 0.2
of a level of the reference, and any value from 515/65535 to 770/65535 fits.

### `kPictureRowsPerJob`

**What it does.** A brush shape's dab (StampTip::pictureStamps) goes to the workers from this many pixels, in bands of
kPictureRowsPerJob rows. Each of its pixels samples the picture once for every stamp over it, a hundred or so at Flat Nib's
Spacing 1, so even a 35 px dab (about 1600 px, under kParallelPixels) is worth sharing. The bands read and write only their own
pixels, so the pixels are the same however they're shared.

**How it was chosen.** Timed on the recorded C with Flat Nib on the Brush at 35 px, 16 cores, replayed whole (0.44 s of it
start-up):

| Pixels and rows | 4096 and 16 (a round dab's) | 1024 and 8 | 512 and 4 | 256 and 2 |
| --- | --- | --- | --- | --- |
| Time | 1.35 s | 0.86 s | 0.76 s | 0.69 s |

On 2 and 4 cores, 512 and 4 and 256 and 2 are the same. In the stroke view bench, done after the lift: 491 to 567 ms -> 40 to
46 ms.

### `kBehindDabWidthShare`

**What it does.** When behind, a dab takes as many stamps as its spacing fits into this share of their diameter along the way, or
its usual count where that is more: 10 stamps at the Brush's and Water Color's 5 % spacing, which raises the Brush's 1st, 3rd,
4th and 5th Shapes and Water Color's 3rd, 4th and 5th.

**How it was chosen.** Measured on 2026-11-13 on the 5th Shape, its look rechecked on the others on 2026-11-15. A trade-off: on 2
cores 0.25 keeps a behind stroke closer to one that kept up and shows the paint in shorter steps, for about 0.1 s more to finish
at 3000 px on the 5th Shape (the others not timed); 1.0 drags the colour further.

## Blot maps

### `kBrushBlotBodyFrom`, `kBrushBlotBodyTo`

**What they do.** The Brush's 1st to 4th Shapes with a blot map: from Strength kBrushBlotBodyFrom the picture reaches into the
body, wholly from kBrushBlotBodyTo, a smoothstep between.

**How they were chosen.**

- The reference's blot-mix readings at Mess 26 % (Pastel, the slow ramp at 200 px) keep the outline-only rule (11.2 to 14.7
  levels; the body rule took them to 16 to 21).
- Its C at Strength 50 keeps the body rule (Blots and Fine Blots at 35 / 100 / 400 px, Shape 1 / 3: 40.9 / 35.4 -> 31.7 / 28.1
  levels, painted pixels -16 / -11 % -> -2 / -2 %).

### `kBrushBlotCutShape`

**What it does.** The Shape whose blot cut the Brush's softer Shapes take, counted from 0: the 5th.

**How it was chosen.** A held-out check showed the reference's cut as sharp at Shape 1 as at Shape 3 on a light blot map, only
the whole stroke's edge softer. On the pack's Brush over Blots and Blots&Noise at Shapes 1 and 3 (20 cases):

| Cut | Each Shape's own | The 3rd's | The 4th's | The 5th's |
| --- | --- | --- | --- | --- |
| Levels | 17.4 | 18.5 | 17.0 | 16.5 |

### `kWaterBlotTone`

**What it does.** Water Color's 5th Shape with a blot map takes the Brush's outline (StampForm::BlotWindow::BrushFifth), each
stamp laying this much paint inside it.

**How it was chosen.**

- **First fit,** on the reference's C, Blots at 35 / 100 px and Strength 50 / 100:

  | Tone | 0.3 | 0.2 | 0.15 |
  | --- | --- | --- | --- |
  | Levels off | 15.0 | 5.4 | 10.4 |

  Fine Blots at 100 px: 5.6 at 0.2. The stamp tools' rule with its skirt (before): 37.0; without the skirt, 40.4 (it laid too
  few pixels, 33 to 44 % under).
- **Refitted on the reference's own maps,** with its skirt kept: the pack's Water Color C over Blots and Blots&Noise at Shape 5
  (35 / 100 / 400 px, Strength 35 / 50 / 100) and its straight lines (samples 25 / 100 / 200 px apart):

  | Tone | The C (levels) | The lines |
  | --- | --- | --- |
  | 0.2 | 19.8 | 35.7, 15.7, 13.3 |
  | 0.35 | 13.1 | 18.3, 9.8, 10.6 |
  | 0.5 | 18.3 | 5.8, 18.6, 20.9 |

### `kWaterBlotBySamples`, `kWaterBlotSamplesAt`

**What they do.** Over a blot map Water Color's tone follows how close its pen samples fall, as its plain line's does. With n the
pen samples a stamp's diameter spans, the 5th Shape's blot tone is kWaterBlotTone times 1 + kWaterBlotBySamples (n -
kWaterBlotSamplesAt).

**How they were chosen.** The reference's tone falls with the samples' spacing (100 px, pressure 0.7):

| Samples apart | 25 px | 50 px | 100 px | 200 px |
| --- | --- | --- | --- | --- |
| 5th Shape | 0.48 | 0.41 | 0.36 | 0.34 |
| 3rd Shape | 0.49 | 0.43 | 0.34 | 0.31 |

| Slope, at | 0, 0.9 | 0.13, 0.9 | 0.2, 1.3 | 0.25, 1.2 |
| --- | --- | --- | --- | --- |
| The lines' centres, levels off | 11.4 | 9.1 | 8.3 | 8.3 |
| The pack's C at Shape 5 | 13.1 | 11.1 | 10.5 | 10.3 |

0.3 at 1.3 gave 8.7 / 10.8.

### `kWaterSoftBlotFloor`, `kWaterSoftBlotBySamples`

**What they do.** Water Color's 1st to 4th Shapes, laid in groups, take each stamp's dab times min(1, kWaterSoftBlotFloor +
kWaterSoftBlotBySamples n).

**How they were chosen.**

| Setting | 1 | (0.7, 0.2) | (0.4, 0.45) | (0.2, 0.6) | (0, 0.8) |
| --- | --- | --- | --- | --- | --- |
| The lines at 100 and 200 px (levels) | 14.7 + 23.2 | 13.1 + 17.5 | 12.2 + 11.1 | 11.6 + 9.8 | 11.5 + 13.6 |
| The pack's C at Shape 3 | 15.7 | 13.2 | 11.8 | 12.9 | 17.0 |

The 1st Shape keeps the dab whole: with it the pack's C at Shape 1 went 12.7 -> 24.7 levels (no line case reaches that Shape).

## The Brush's grid and edges

### `kGridTwoFrom`, `kGridFourFrom`, `kGridEightFrom`, `kGridTwo`, `kGridFour`, `kGridEight`

**What they do.** How far apart the Brush's grid points are, by the tip's diameter:

- a pixel apart below kGridTwoFrom, so a small tip paints exactly as the per-pixel model does;
- then kGridTwo;
- kGridFour from kGridFourFrom;
- and kGridEight from kGridEightFrom.

**How they were chosen.** Measured on the fast strokes:

- below 500 px a pixel apart keeps up;
- at 500 px two apart is back within a few ms of the lift, and at 1000 px four apart, with the look as close as at 4000 px;
- from 3000 px eight apart looks as four apart did at 4000 px, and saves a fifth of the time there.

### `kSharpCells`

**What it does.** A stamp's edge that rises over fewer cells than this is worked out per pixel.

**How it was chosen.** Measured on 2026-11-13 and rechecked on 2026-11-15:

- at 4 the Brush's flecks at its sharp edges stay under 3 levels from its per-pixel model on the reference set;
- 3 lets them to 3.8;
- 5 takes them to 1.3 for 12 to 15 % more exact marks (up to 29 % on the soft Shapes).

The 5th Shape's flecks over paint (3.6 levels at 4000 px, more on other strokes) come from elsewhere: no value from 3 to 5 moves
them.

### `kSkirtWholeSpare`

**What it does.** In px: a band counts as inside a skirt, and inside a stamp's whole outline, only this far within them. The
skirt's share is above none however its table's last entries round (it falls to exactly none only at its reach), and no pixel's
distance, rounded, crosses an edge its band's corners are inside; so every pixel there has its outline raised whole, as markOver
finds pixel by pixel.

**How it was chosen.** 0 to 2 px paint the reference set the same: it costs only the bands it sends to the per-pixel raise.

### `kMostSkirtedBand`

**What it does.** The most stamps over a band whose distances dabMark keeps for skirtAt to take again, on the stack: past it
skirtAt takes them itself. With 2 · DabTip's kMostSkirtNeighbours it also sizes markSkirtedEdges' deep spans, which need 15 or
more (at 12 pixels move).

**How it was chosen.** A dab holds at most stampsPerDabWhenBehind stamps, 10 for the Brush's 5th Shape; 64 leaves room for any
refit of the spacing. The reference set's dabs hold at most 10 band stamps and 16 around; past this room the marks are slower,
not different.

### `kSkirtCutRampCells`

**What it does.** In cells: where a skirt is cut inside the stroke, deep inside a stamp around the dab that isn't one of its
neighbours, the cut is a ramp this wide, centred on the neighbours' outlines, so the grid reads it and no pixel there is worked
out per pixel.

**How it was chosen.** It's the narrowest ramp the grid carries within its margin, kSharpCells, and must stay equal to it:
narrower, the grid misreads it; wider moves more paint for no gain. Tried on stroke 2 at 4000 px (a throwaway trial), all at the
same speed:

| Ramp | 2 cells | 4 cells | 8 cells |
| --- | --- | --- | --- |
| Pixels moved over white | 676 k, by up to 6 levels | 1.11 M, by up to 6 | 1.75 M, by up to 8 |

### `kSharpOutsideCells`

**What it does.** In cells: the band where a stamp's sharp edge is worked out per pixel reaches this far outside it.

**How it was chosen.** Proved on 2026-11-13 at both ends and rechecked on 2026-11-15:

- widening it moves no pixel on the reference set or at full pressure, and none by more than 5 in 65535 at light pressure (the
  Brush's soft Shapes at 500 px);
- at 1 a few move by under half a level;
- at 0, flecks of up to 32 levels.

### `kSmoothRimCells`

**What it does.** A stamp whose outline ramps over at least this many cells, its blur or its eased ramp, changes it smoothly
enough at that scale to read it between grid points: the soft Shapes'. Narrower outlines are worked out per pixel.

**How it was chosen.** Measured on 2026-11-13: from 1 to 16 cells no pixel moves past half a level; above 8 the per-pixel
outlines' work climbs (2.4 times at 16).

### `kSeriesUpTo`, `kSeriesTerms`, `kExpFloor`

**What they do.** expm1 for 0 or less, in plain float steps the same in one lane as in four, so the soft Shapes' read-back can
work out four pixels at once and give each what one at a time would.

- Near 0 it's the series to x^kSeriesTerms / kSeriesTerms!, each term the last times x / n.
- Further out, exp(x) - 1: exp by its power of 2 and Cephes's polynomial on what's left.
- From kExpFloor on, -1.

**How they were chosen.** The soft Shapes' pixels come out with it within 0.01 of an 8-bit level of std::expm1's. With the series
below 0.15 to 0.35, laneExpm1 stays within 3 float steps and 0.006 of a 16-bit level of expm1; 7 to 12 terms give the same bits
for every float below kSeriesUpTo, 6 don't, and 8 is one past the fewest.

### `kCoverStep`

**What it does.** 1 - e^-density from a table, as stage 1's CoverageTable is: steps of kCoverStep to kCoverTop, read between
them. The Brush's plate takes it at each pixel its mark is worked out at, where expm1 was a fifth of the stroke's time; a run's or
a whole tile's one value keeps expm1.

**How it was chosen.** It's within 1.9e-6 of the exact, a sixteenth of a mask's step; past kCoverTop it changes by under 1.1e-7,
so the last value stands.

## The Brush laid in groups

### `kBrushGroupSpacing`

**What it does.** The Brush laid in groups: its stamps go as far apart as the reference's, as a share of their diameter, by
Shape.

**How it was chosen.** On the C at 200 px and Stabilizer 3:

| Shape | 1st | 2nd | 3rd | 4th | 5th, with Anti-Ripple |
| --- | --- | --- | --- | --- | --- |
| Our stamps | 176 | 360 | 622 | 1008 | 659 |
| The reference's | 175 | 360 | 622 | 1008 | 657 |

With Anti-Ripple off the reference's 5th lays 1398 there, so its spacing is the 5th's times 657 / 1398.

### `kBrushPlateEdgeEnd`, `kBrushPlateEdgeWidth`

**What they do.** In px. On the 5th Shape with Anti-Ripple each group's mask is the dab Brush's mark with its neighbours' skirts,
which lays the reference's straight lines across to their last rows (0.80 levels at 50 and 200 px, pressure 0.34 and 0.6; the
reference's own dab, stamp by stamp, 5.4, its edge falling short of the reference's). It's cut at an edge kBrushPlateEdgeWidth
wide that ends kBrushPlateEdgeEnd past each stamp's radius, over the group's stamps, those kept before it and those it waits for.

**How they were chosen.**

- The reference's click keeps 0.127 of its centre at exactly the radius at 50 and 200 px (this gives 0.141), and its lines lose
  about half the row half a pixel inside the radius, at both sizes.
- Without the cut the skirt reaches about a pixel further, the full-pressure ramp's last row 0.888 against 0.677 (brush/ramp
  3.74 against 1.72).
- Ending 0 and 0.5 px past it: the lines 0.87 and 0.57 levels against 0.65.

### `kBrushPlateEdgeWidthSoftest`

**What it does.** Below Sharpness 50 the plate's edge widens inward, its end kept, to this many px at Sharpness 0, straight with
the rim's share: the reference's lower Sharpness pulls the plate's solid core in and widens its soft edge by as much. Only Water
Color reads it.

**How it was chosen.** Fitted on the reference's lines at 8 / 20 / 165 px and Sharpness 0 / 50 / 100:

- **The edges:** 4.53 -> 2.85 levels with the Marker's shift below. 1.6 / 2.0 / 2.5 / 3.0, with the narrowing above 50 tried
  too, gave 3.77 / 4.52 / 6.84 / 8.89 on the Brush's alone.
- **From 50 up** it stays kBrushPlateEdgeWidth, as fitted. Narrowing it toward Sharpness 100 (0.4 px) made the small sizes too
  hard there (8 px 0.86 against the reference's 0.36), and pushing the end out with size (to bring the reference's faint outer
  row at 165 px) cost the Brush's readings (2.56 -> 2.60).
- **The readings:** the Brush's 47, most at its starting Sharpness 50, don't move; Water Color's, at 0, gain.
- **Water Color's C:** on the reference's Water C its 5th Shape at Sharpness 0 and 35 / 50 / 100 / 200 px fits 1.3 better: 4.60 /
  2.84 / 2.67 / 2.39 levels at 1.6, 4.09 / 2.61 / 2.58 / 2.34 at 1.3. But Water Color starts at Sharpness 0, and its readings
  there went 15 worse, by up to 0.27 (water/stripes-cross, watercolor-two, the ramps), so it stays.
- **Above 50** the reference's edge is a little harder at 100 than at 50 and ours the same, but ours at 50 is already as hard as
  the reference's at 100 (its faint fringe at 35 px 25,580 against 28,497 / 27,179). A harder edge at 100 cost 2.90 -> 3.03 to
  3.36 over the four sizes, and a tail or an edge ending further out at 50 (as the Brush's) brought the fringe count to the
  reference's and the differences up, 3.00 -> 3.01 to 3.90.

### `kBrushPlateEndBySharpness`, `kBrushPlateWidthBySharpness`, `kBrushPlateTailKnee`, `kBrushPlateTailBySharpness`, `kBrushPlatePullIn`, `kBrushPlateSmallFrom`, `kBrushPlateSmallTo`

**What they do.** The Brush's plate edge by Sharpness. The reference lays a wider line at Sharpness 100 than at 50 and a softer,
longer edge toward 0:

- its edge ends kBrushPlateEndBySharpness px past the radius;
- its straight part is kBrushPlateWidthBySharpness px wide;
- below kBrushPlateTailKnee of whole it falls to none over kBrushPlateTailBySharpness px in place of its straight line, a faint
  tail (the earlier edge had none: the C's faint fringe was half the reference's).

They're straight between the three Sharpnesses. Stamps under kBrushPlateSmallTo px in radius have it kBrushPlatePullIn px further
in, straight from none there to all of it at kBrushPlateSmallFrom: on the C at 20 px the reference's line is no wider than the
earlier edge's, while from 35 px up it's 0.25 to 0.3 px wider a side at Sharpness 0 and 100; its lines at 8 and 20 px agree.

**How they were chosen.** Fitted on the reference's C (20 to 400 px, Sharpness 0 / 50 / 100, pen pressure as recorded) and its
straight lines (8 / 20 / 165 px at pressure 0.8) together. Levels on average from the reference's, before -> after:

| Measure | Before -> after |
| --- | --- |
| The C's 18 cases | 2.89 -> 1.38 |
| The C at 20 px | 5.24 -> 3.29 |
| The C at 35 px | 4.65 -> 1.48 |
| The C at 400 px | 0.97 -> 0.81 |
| The lines' 13 rows | 2.85 -> 2.16 |

- 0.28 / 0.20 / 0.35 px with tails of 1.0 / 0.6 / 0.4 and no pull-in fitted the C from 35 px up best (0.99) but took its 20 px
  to 9.78 and the lines to 6.06.
- At Sharpness 50, the Brush's start, the C chose. 0.17 px with a tail of 0.5 gave the C at 35 / 50 / 100 px 1.78 / 1.27 / 0.87
  against 1.22 / 0.92 / 0.80, its faint fringe at 35 px 3,645 against 4,251 (the reference's 6,071), but the 165 px line's last
  rows 1.26 against 3.62, that line being 0.17 px a side wider than the reference's (its integral across). Pulling the end in
  0.12 px from 50 to 75 px radius won the line back (0.58) but cost the C at 200 px (0.75 -> 0.90).
- Water Color keeps the earlier edge: with this one its 34 readings went 4.29 -> 4.52 and its lines' last rows, already past the
  reference's, further out.

### `kBrushDabBlurBySharpness`, `kBrushDabHalfAt`, `kBrushDabEdgeWidth`, `kBrushDabBlurShift`

**What they do.** The Brush's 1st to 4th Shapes at small sizes. The reference's dab edge is about as wide as ours, a share of the
radius, with a fixed blur added as a blur adds, sqrt(w^2 + b^2), b kBrushDabBlurBySharpness px (read off the C's edge widths at 35
and 50 px, the same for every Shape within 0.2 px). So a dab is:

- read about its half point (kBrushDabHalfAt of the radius), stretched to that width;
- its edge kBrushDabEdgeWidth of the radius wide (the C's 0.8 to 0.2 span at 35 px over the radius);
- and moved out by kBrushDabBlurShift times what the blur widened it, which puts its line's half-covered row back where the
  reference's is.

**How they were chosen.** On the C at 35 and 50 px, Sharpness 0 / 50 / 100, levels from the reference's:

| Shape | 1st | 2nd | 3rd | 4th (with 100 and 400 px) |
| --- | --- | --- | --- | --- |
| Before -> after | 1.64 -> 0.94 | 1.52 -> 0.96 | 1.80 -> 1.16 | 1.68 -> 1.38 |

A straight added width (w + b) fitted the 4th at 35 px but softened the 1st and 2nd past the reference's.

### `kBrushGroupShareScale`, `kBrushGroupBlendingCurve`

**What they do.** The Brush laid in groups mixes in the reference's order:

1. its pickup;
2. the brush colour mixed into it by its share;
3. what it carries moving toward that mix by the carry step;
4. and it lays what it carries.

The share is kBrushGroupShareScale times the dab Brush's fade in above Blur Pressure (colourFadeIn), with Blending's curve to
kBrushGroupBlendingCurve in place of kBlendingCurve, so Blending 50 % lays 0.20 of Blending 0's brush colour where the dab Brush
lays 0.33.

**How they were chosen.** Fitted where only the share and the pickup act: the reference's quick line over the field on the 5th
Shape at Persistence 0 and at Blending 0, as it is and slowed six times, within 2.07 levels on average.

| Scale / curve | 1.6 / 2.2 | 1.7 / 2.2 | 1.8 / 2.3 | 1.7 / 2.5 | The dab Brush's 1 / 1.6 |
| --- | --- | --- | --- | --- | --- |
| Levels | 2.24 | 2.39 | 3.24 | 3.30 | 13.19 |

### `kBrushCarryStepPower`

**What it does.** The carry step: one stamp keeps 1 - (1 - Persistence) to this power of what it carried (0.82 at Persistence
50 %), a group of n stamps that to the n, as if each had stepped; the first group takes its mix as it is.

The dab Brush's carry eases in over the first stamps (kSettleStamps) and lets go sooner the firmer the pressure
(kBrushFirmLetGo); the reference's step depends on Persistence and the group's count alone, so the groups take neither.

**How it was chosen.** Fitted with the share above on the reference's pair of strokes at the starting settings: 2.74 levels on
average, all six of those quick lines 2.00.

| Power | 1 | 2 | 2.75 | 3 | 5 |
| --- | --- | --- | --- | --- | --- |
| Levels | 4.67 | 3.25 | 2.97 | 3.33 | 16.84 |

### `kBrushGroupFollow`

**What it does.** Laid in groups, the Brush and Water Color group their stamps as the reference's, as its workings were read:

- An event that lays no stamp doesn't count.
- The groups' size follows the average of the stamps recent events brought, by kBrushGroupFollow an event, but moves only when
  that average is more than a whole stamp from it, then to the nearest, starting at one.
- On the 5th Shape with Anti-Ripple the last stamps wait as the plate's look-ahead (skirtNeighbours), so stamps pool across
  events, and what's beyond goes out in groups of the size, the last smaller, the size growing by one a group while more wait.
- The end lays the rest as one group.

**How it was chosen.** Fitted on the reference's group counts: the C at 50 / 200 / 689 / 1500 / 4000 px on the 5th Shape and at
200 px on the 1st to 4th, for both tools, and the over-paint reading's quick line as it is and slowed.

- The groups are 2.0 % from the reference's on average (rounded down as before, 7.6 %).
- 0.15 / 0.35 / 0.5 gave 9.1 / 4.0 / 5.3 % before the remainder, 4.5 / 3.3 / 5.2 % with it.
- Water's quick line: 59 groups (21 single, 23 of 7) against 63 (23, 24); the C at 1500 px 56 against 53 (the Brush 57 against
  57).
- The look-ahead is the plate's own skirtNeighbours (kBrushSkirtNeighbours, 3), not fitted for the groups: on the same counts 2 /
  4 stamps gave 5.1 / 4.7 % against 3's 4.8 % before the size was updated ahead of the event's stamps, and 4 gave 3.1 % against
  3's 3.0 % after.

### `kBrushGroupFullFrom`, `kBrushGroupLeastShare`

**What they do.** No group, the end's flush included, holds more than the Shape's most, and its smallest stamp shrinks that toward
kBrushGroupLeastShare of it below kBrushGroupFullFrom px across, straight: the workings' "below a few tens of pixels it falls
toward about half". The half is the workings'.

**How they were chosen.** The size was fitted on the same group counts; the largest that keeps 2.0 % is taken:

| Size | 20 px | 25 px | 30 px | 40 px | 60 px |
| --- | --- | --- | --- | --- | --- |
| From the reference's | 2.0 % | 2.0 % | 2.0 % | 2.8 % | 6.6 % |

Without the limit, 1.9 %, but the quick lines' end went out as one group of 12 or 13 stamps; with it none holds more than 8, as
the reference's.

## Water Color laid in groups

### `kWaterPickUpReachSmall`, `kWaterPickUpSmallRadius`, `kWaterPickUpLargeRadius`

**What they do.** Water Color's pickup also counts the bare paper beside and ahead of its mask, out to this many of its stamps'
radii, as nothing, as the reference's workings were read: so a firm line settles short of full, more so the larger the share of
fresh bare paper a group sees. Only bare paper past the mask counts, so over a field nothing changes (the over-paint reading 1.62
either way). Below a nominal radius of kWaterPickUpLargeRadius it takes kWaterPickUpReachSmall, from kWaterPickUpSmallRadius,
straight between.

**How they were chosen.**

- **The reach:** lines at pressure 0.6 / 0.9, 50 / 200 px, and the 36 Water readings:

  | Reach | Without | 1.15 | 1.2 | 1.25 |
  | --- | --- | --- | --- | --- |
  | The lines (levels) | 2.65 | 1.93 | 1.96 | 2.50 |
  | The 36 Water readings | 5.07 | 4.63 | 4.58 | 4.79 |

  At 1.2 the 5th Shape gives 0.337 / 0.772 / 0.392 / 0.819 against 0.337 / 0.771 / 0.392 / 0.832. Counting what's past the mask
  whatever it holds, the over-paint reading went to 3.21.
- **By Shape, 1st to 5th:** on the 3rd and 4th, 1.5. The reference's C (35 to 200 px, starting settings, pen pressure as
  recorded) lays those Shapes 4 % and 2 to 3 % lighter than ours did all across the line, the 2nd Shape 4 %, the 1st and 5th
  3 %. Its quick line at 200 px (the quick-line readings) lays the 3rd 6 % and the 4th 13 % lighter, the 2nd as ours and the 1st
  13 % darker.
- **The 3rd and 4th Shapes:** counting more bare paper lightens only where the line meets bare paper, so the readings over paint
  don't move. No other reading moves:

  | Reach | 1.4 | 1.5 | 1.6 | 2.0 | Before |
  | --- | --- | --- | --- | --- | --- |
  | The C, 3rd Shape (levels) | 2.52 | 2.07 | 1.80 | 1.75 | 3.81 |
  | The C, 4th Shape | 2.20 | 2.38 | 2.68 | 3.36 | 3.20 |
  | The quick-line reading, 3rd Shape | 1.16 | 1.18 | 2.79 | 5.23 | 6.61 |
  | The quick-line reading, 4th Shape | 9.51 | 7.14 | 5.83 | 3.69 | 16.11 |

- **The other Shapes stay 1.2.** The 2nd Shape gains on the C only from 1.6 (3.35 -> 2.57), where its quick-line reading goes
  1.42 -> 4.29; the 1st's dab reaches past 1.4 radii, so it doesn't move; on the 5th, 1.4 cost the readings on its lines and
  ramps (4.32 -> 5.61).
- **Small radii:** at 35 px the reference's 4th Shape lays as ours did (one scale fitting it 0.998, against 0.965 to 0.981 from
  50 px), and 1.5 took it from 3.67 to 4.66 levels.

### `kWaterDilutionPower`, `kWaterNearOpaque`, `kWaterShareScale`, `kWaterBlendingCurve`, `kWaterFadePower`, `kWaterThickShare`

**What they do.** Water Color laid in groups mixes in the reference's order, as its workings were read:

1. its pickup over the mask, bare pixels as nothing (as the brush colour at Dilution 0);
2. Dilution moves that toward a stand-in, its own colour made opaque where it holds any paint (kWaterNearOpaque), else the brush
   colour, by (1 - Dilution) to kWaterDilutionPower;
3. the brush colour goes in at that opacity by its share, kWaterShareScale times (1 - Blending) to kWaterBlendingCurve times the
   pressure past Blur Pressure to kWaterFadePower, less over thick paint, by up to kWaterThickShare times what it picked up's
   opacity times Blending;
4. what it carries steps toward that by the Brush's carry step;
5. and each pixel moves toward the blur of what's there (kWaterMeltRadii) moved toward what it carries by the pressure past Blur
   Pressure, by the group's mask.

**How they were chosen.**

- **kWaterDilutionPower:** on bare paper a click carries (1 - Dilution)^1.43 = 0.60 at the starting 30 %, and its stamp makes up
  the rest of the reference's click (kWaterStampShare). The 1st Shape's click (0.571 of the Brush's, whose centre is 0.955) needs
  at least 0.55 of it. Lines at pressure 0.6 / 0.9, 50 / 200 px: 3.70 levels; at power 1 (0.70) 6.15, its line at 0.9 building
  too far (0.847 against 0.771 at 50 px). (Fitted with the carry counted as the group's count to 0.5 and its size rounded to the
  nearest; with the Brush's, below, 3.49.)
- **kWaterNearOpaque:** what it picks up counts as its own colour made opaque from this opacity, so its colour carried off the
  field onto bare paper stays that colour, as the reference's C and drag-out do:

  | Opacity | 0.99 | 0.5 | 0.2 | 0.05 |
  | --- | --- | --- | --- | --- |
  | The 36 Water readings | 11.02 | 9.56 | 9.12 | 9.08 |

  c5-watercolor-200-plain 33.7 -> 11.8, drag-out 19.0 -> 9.9.
- **kWaterShareScale, kWaterBlendingCurve, kWaterFadePower:** fitted on the reference's quick line over the field on the 5th
  Shape at its starting settings, Blending 0, Persistence 0 and Dilution 0, as it is and slowed six times (the over-paint
  reading): 1.92 levels on average (the build before, 4.21).
  - The fade-in makes the share rise with pressure, as the reference's ramps over the field do:

    | Fade power (scale) | 0 (0.64) | 1 (1.067) | 1.3 | 1.5 (1.377) | 2 (1.778) |
    | --- | --- | --- | --- | --- | --- |
    | The over-paint reading | 2.00 | 1.84 | 1.92 | 1.97 | 2.08 |
    | The readings | 6.72 | 5.24 | 5.04 | 5.14 | 5.72 |

  - The scale and curve at power 0:

    | Scale / curve | 0.6 / 2.4 | 0.6 / 2.8 | 0.68 / 3.2 | 0.76 / 3.2 | 0.64 / 3.0 |
    | --- | --- | --- | --- | --- | --- |
    | Levels | 2.76 | 2.19 | 2.40 | 4.65 | 2.00 |

  - Blending without pressure, or falling with it, was worse (the over-paint reading 4.56 and 14.5 at their best).
  - These were fitted with the carry counted as the group's count to 0.5 and the group's size rounded to the nearest; with the
    Brush's rules for both, the reference's own, the over-paint reading is 1.57 and the readings 5.09.
- **kWaterThickShare:** the reference's "by up to half"; not fitted.
