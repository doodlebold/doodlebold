# RebuiltStroke.cpp: each tool's tip

The named values in `src/core/stroke/RebuiltStroke.cpp`: what they are, what they do, and how they were chosen. The values
themselves are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## Edges through the paper

### `kSoftEdgeBlend`

**What it does.** The 1st to 4th Shapes' blend through the paper: how far each goes from the hard edge's thinning to the soft
edge's tops, and the bar for its hard part, times the soft bar. The 3rd, with no reading, lies between the 2nd and 4th. The 1st
is the soft edge's alone; the 5th has none.

**How it was chosen.** Fitted to the 2nd and 4th Shapes' soft-ramp readings' measure, the untextured stroke's pixels:

- **2nd:** 10.62 levels of 8 bits off (limit 11.65). Weights 0.35 and 0.45: 10.70 and 10.78. Depth 1.75: 10.63. The 2nd's
  softness against the 1st's, 0.54, with 1.5: 12.08.
- **4th:** 12.82 (limit 10.57). 0.17, its softness's: 13.21. 0.05: 13.19.

## Brush

### `kBrushFullStamp`, `kBrushSkirtNeighbours`

**What they do.** The Brush's 5th Shape lays one rule's stamps, as the reference's readings show them:

- A full stamp lays kBrushFullStamp, whole to its outline.
- Past its outline it lays a skirt (StampTip.cpp), shown only inside the outlines of the kBrushSkirtNeighbours stamps before and
  after it on the path.
- Below full pressure and Density its line has the tone it had.
- A pen's stroke lays its first stamp twice, and a click is one stamp.

**How they were chosen.** The count was fitted to the long line's side: the long line is within half a level from 3 px in; in a
model of the rule, 2 and 4 are 7 and 2.5 off.

## Pencil

### `kPencilDabStrength`

**What it does.** The Pencil's dab on the 1st to 4th Shapes, the reference's: each stamp is its dab, the profile in its paint
(profilePaint). A click is that one stamp as it is.

**How it was chosen.**

- **Strength:** at full pressure and Density, the reference's click there, the same at 50 and 200 px (243.4, 144.1, 88.6 and
  56.2 at the centre).
- **Spacing,** in nominal diameters: fitted to the reference's straight lines at full pressure, 200 px. The firmer the Shape, the
  closer.

## Water Color

### `kWaterMeltFirmPower`

**What it does.** Below full pressure Water Color's melt softens the stroke's own paint with what's under, so its passes settle
toward a ceiling short of full and a circle falls toward its side. The melt is times (1 - firmness) to this power.

**How it was chosen.** Fitted to:

- the reference's pen circle at 0.8: 226 at its centre falling to 158 near its edge (220 to 166);
- with the line gone over 1 to 6 times at 0.6 and 0.8 following: 81, 120, 141, 152, 164 and 162, 204, 220, 229, 235 (83, 120,
  139, 150, 161 and 166, 204, 219, 226, 232).

### `kWaterPenReach`

**What it does.** Each stamp picks up on its own, under its pen sample's last stamp, where the pen is, but no further ahead than
this many of its diameters. So a stroke whose samples fall far apart for its size lays a lighter line, and up to two stamps a
sample changes nothing, as the reference's.

**How it was chosen.** Fitted to the reference's 200 px line with samples 1 to 200 px apart, 0.1 to 20 stamps a sample:

- settling at 221.7 up to two, then 218.2, 211.3, 197.8 and 188.6 (221.7 to 221.6, then 217.8, 214.2, 207.3, 195.5, 189.8;
  192.5 and 182.9 at 0.4, 170.1 with no limit);
- with the full-pressure line at 1000 px following: 221 212 200 195 196 (223 217 209 199 195).

## Marker

### `kMarkerShareByCeiling`

**What it does.** The Marker, fitted to the reference's. The entry covers:

- the power of its dabs' ceiling its brush colour's share is multiplied by;
- the stamps over which that share follows the ceiling (1 diameter, counted as Persistence counts them);
- the stamps over which it keeps Persistence of what it carries;
- and the density share below which a light touch lays nothing, and the scale it fades in over.

### `kMarkerEdgeShift`

**How the Marker lays paint.**

- A Marker pixel's opacity is the most of what's there and each stamp's ceiling over it, so neither its crossings nor passes of
  its colour at the same pressure build up, and it never lightens what it laid.
- Where pressure falls, its coverage follows it smoothly, stamp by stamp, so its ends fade out softly.
- Its brush colour's share is times its dabs' ceiling to kMarkerShareByCeiling, following that ceiling over kMarkerShareLag
  stamps, so a light touch lays a little colour and a firm one more, and as pressure rises or falls the share comes behind it.
- It keeps what it carries over kMarkerPersistenceStamps stamps, and picks up from kMarkerPickUpAcross places across, so fine
  detail doesn't alias.
- Its edge is the Brush's, set in by the Pencil's inset. It melts what it crosses, in colour only, so empty canvas nearby doesn't
  thin paint. A tap lays nothing, a click that never moves one stamp.
- Where its strength changes, stamps go in between so it changes in steps of kMarkerFadeStep. A stamp lighter than the stroke's
  firmest mixes its paint in, and changes the colour it lays, only by its share of the firmest, so a fading end keeps the body's
  colour.

**What it does.** In px: the Marker's outline sits this far inside where the Brush's edge would put it.

**How it was chosen.** Fitted on the reference's 165 px line at Sharpness 0 / 50 / 100:

- the last two rows 0.64 / 0.22 -> 0.56 / 0.00, against 0.56 / 0.02 at 50;
- the edge 1.11 -> 0.42 levels;
- -0.3 / -0.8 gave 0.52 / 1.01;
- the 35 Marker readings 6.79 -> 6.29.

## Blur

### `kBlurGrow`

**What it does.** Blur, fitted to the reference's. The entry covers:

- the reach as a share of the radius at Blur Width 100 %, 4 px at the starting values;
- the passes of a box each way it's as wide as, a Gaussian of 4.5 px, and how much further it reaches at full pressure;
- the pressure share's power, and the Density's, for how much of the blur is laid;
- and the bell across the stroke, a share of the radius, on stamps kBlurGrow times as wide.

## Eraser

### `kEraserFifthFullAt`

**What it does.** On the 5th Shape, where the Eraser takes the Pencil's coverage, it counts this share of the merge's full point
as full, so one stroke at full pressure through full paint leaves nothing in its middle, as chosen. It takes about 3 % more below
that: a click 76.1 of 255 against 73.8, the rims 2 to 4 levels more.

**How it was chosen.**

- The line's middle reaches about 0.976 of the full point, which left 5.9 of 255.
- 0.98 leaves 0.9; 0.975 clears it only just.
- 0.97 was tried and kept, with room for a middle a little thinner.
