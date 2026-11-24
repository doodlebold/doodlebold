# StampForm.cpp: blot maps and brush shapes

The named values in `src/core/stroke/StampForm.cpp`: what they are, what they do, and how they were chosen.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels unless a line
> says otherwise. See the [index](README.md#terms).

## Blot maps

A blot map's stamp is a cone, and the picture eats into it by its lightness: what's left of the cone passes each Shape's window,
from its foot (nothing below) to its top (whole above), and the stamp lays its window's share, so a stroke builds up where its
stamps overlap. The values were fitted to the reference's Pencil blot-map readings and its Pastel Marker.

### `kBlotConeReach`

How far out a blot-map stamp's cone reaches, in the stamp's nominal radii (the tip's, before a soft Shape's inset). 1.4 to 1.6
fit alike.

### `kBlotDepth`

How deep the picture eats into the cone at Mess. 100 %: its lightness times this. 1.0 leaves strokes too solid inside (the
Pencil's mean alpha at Mess. 100 % was 216 against the reference's 140); 1.35 fits worse (the readings' sum 71 against 69). The
soft Shapes and every tool not named below use it.

### `kBlotDepthFifth`

The depth on the 5th Shape for the Pencil, Eraser and Marker. Fitted on the reference's constant-pressure lines (the Pencil with
Blots at 100 px, pressure 0.3 / 0.6 / 1.0, Strength 26 / 50, and 100 at 0.6), the C stroke (Blots at 35 / 100 px, Strength 50 /
100, the three tools) and the 175 texture readings at once:

- the lines' seven cases:

  | Depth | 1.2 | 1.3 | 1.35 |
  | --- | --- | --- | --- |
  | Levels off | 106.9 | 68.8 | 74.3 |

- where the picture is flat white, the band at the line's middle at Strength 50: 0.44 / 0.72 / 0.79 at pressure 0.3 / 0.6 / 1.0
  against the reference's 0.42 / 0.74 / 0.79, the same depth at every pressure;
- the C at 100 px, Strength 50: the Pencil 21.75 → 11.98, the Marker 13.54 → 7.46, the Eraser 14.50 → 8.60; Strength 100 within
  half a level;
- the texture readings −5.43 summed (the Pencil's at Strength 26 2.2 to 2.7 better, its Mess. 100 reading 1.2 worse).

Strength times (1.2 + 0.3 (1 − Strength)) took the C a little further but cost the slow ramp's readings +3.5.

### `kBlotDarkWiden`

How far a pixel's lightness is taken toward the darkest of it and its four neighbours, so a dark blot reads a touch wider.

### `kBlotWindowFoot`, `kBlotWindowTop`

Each Shape's window for the stamp tools, softest first: below the foot nothing passes, above the top all of it.

- Each was fitted to its own reading at Mess. 26 % (8 to 12 levels off), and the 5th to Mess. 0, 26, 50 and 100 %.
- On the Pencil's dab model the 1st and 2nd Shapes' stamps are stronger and further apart, so their windows were refitted to the
  same readings: the 1st from (0.1, 1.1), 33.3 levels off, to (0.07, 0.8), 7.62 (0.05 to 0.08 and 0.75 to 1.0 tried, 7.6 to
  17.8); the 2nd from (0.1, 0.7), 9.08, to (0.08, 0.7), 6.51 (0.6 to 0.8: 7.1 to 14.4). The 3rd and 4th came closer as they were
  (7.67 and 6.07 against 8.39 and 6.66).
- The 1st to 4th Shapes' windows and depths were then fitted on the reference's softer-Shape C (the Pencil with Blots and Fine
  Blots at Strength 50, 35 and 100 px, Shapes 1 and 3), where the picture eats less deep and the window is wider than the 5th's.
  Shape 1: depth 0.7 / 0.75 / 0.8 with its top at 0.9 / 1.0, both pictures 18.9 / 13.3 / 14.0 and 12.7 / 11.6 / 15.3. Shape 3:
  depth 0.8 / 1.0, top 0.6 / 0.8 / 1.0, foot 0.15 / 0.05, best 0.8 / 0.8 / 0.15 at 13.4 / 12.1 (from 34 / 31).
- The 2nd, which no reference reaches, lies between the 1st and 3rd (Water Color's blot mix on the 2nd Shape 20.99 → 15.12, the Pencil's
  2nd-Shape blots 6.03 → 6.50 and its C +0.21); the 4th keeps the window and depth its reading was fitted with (the Pencil's
  4th-Shape blots 5.79 → 8.39).

### `kBlotDepthSoft`

The depth on the 1st to 4th Shapes for the stamp tools, fitted with their windows (above).

### `kMixingBlotFoot`, `kMixingBlotTop`

A mixing tool's dab takes its window as its outline, the most any dab gives a pixel, as the reference gathers a pen event's dabs
into one mask. A narrower window, fitted to the Pastel Marker on the 5th Shape; untuned on the others, which no reading reaches.

### `kMixingSoftBlotDepth`, `kMixingSoftBlotTop`

On the Marker's softer Shapes the mixing window is wider and the picture eats less deep. Fitted on the reference's Marker with
Blots and Fine Blots at Strength 50, Shapes 1 and 3 at 100 px: depth 0.8 / 1.0 / 1.2, foot 0.05 / 0.15, top 0.3 / 0.6 / 1.0;
best 1.0 / 0.05 / 0.3, the four cases 13.7 / 14.2 / 13.2 / 14.9 levels off (the 5th Shape's window and depth gave 18.2 / 17.1 /
13.6 / 15.4).

### `kBrushBlotDepth`, `kBrushBlotFoot`, `kBrushBlotTop`

The Brush's 5th Shape takes a window as its outline too, and its picture eats a little deeper. Fitted on the reference's C with
Blots at 35 and 100 px at Strength 25 / 50 / 75 / 100 and Fine Blots at 100, all ten cases at once.

- Where the picture is flat black the reference's edge goes from 0.95 to 0.13 over 1.1 to 1.3 radii (Doodlebold's, at the
  earlier window, 0.80 to 0.19): a window from about 0.1 to 0.25 of the cone.
- Where it's flat white, the edge of the band along the stroke's middle sits where the picture eats 1.35 deep, at Strength 25
  and 50 alike.
- Depth / foot / top tried, levels off on the ten cases:

  | Depth | Foot | Top | Levels off | Notes |
  | --- | --- | --- | --- | --- |
  | 1.2 | 0.05 | 0.3 | 15.48 | Strength 50 23.8 / 23.5 |
  | 1.3 | 0.06 | 0.25 | 11.83 | |
  | 1.35 | 0.06 | 0.25 | 11.60 | Strength 50 11.6 / 12.9, painted within 3 % of the reference's on every Blots case |
  | 1.35 | 0.04 | | 11.67 | |
  | 1.3 | 0.08 | | 11.96 | |
  | 1.25 | 0.08 | | 12.92 | |

- A smooth step in place of the straight window gave 19.0, and Strength to a power below 1 (0.7) 21.0.

### `kGroundFrom`

The lightness, 0 to 1, from which a blot map's picture counts as ground. Doodlebold's own blot maps are dark blots on a flat white
ground (40 to 55 % of each picture pure white), and however deep the rules above let the white eat, the cone's top in a stroke's
middle stood above it: at Mess. 50 a soft band ran along the middle between the blots, and on the 1st to 3rd Shapes even at
Mess. 100 (feedback: "I dont like the line"). So with these pictures the eat goes on from the rule's toward the whole cone as the
picture nears white, from kGroundFrom (no more) to white (all of it).

Chosen on the three pictures, Brush and Pencil, Shapes 1 / 3 / 5, 100 px, Mess. 0 to 100. Against 0.5, which also took away
Mottled Blots' mid greys in the stroke's middle (9001 px over 8 levels lighter at Mess. 50), opening holes its sides don't have,
and 0.9, which painted the same to the eye (1928 px over 8 levels darker there).

### `kGroundEmptyFrom`

The Mess., 0 to 1, from which the ground is empty; below it the band narrows from the round stroke at Mess. 0. Against 0.5, the
Brush's starting Mess., where the band showed: that left a band at Mess. 25 and a hairline at 40; with 0.25 it's gone from 25 up.
Both paint alike from Mess. 50 up.

## Brush shapes

### `kHalfDiagonal`

A picture's corner from its centre is at most √2 / 2 of its longest side; rounded up, so no pixel of a corner is left out.

### `kLitReachPerSpan`, `kLitSlack`

How far from a brush shape's lit pixels, in its own pixels per picture pixel a canvas pixel spans (at least one), its sampling can
still read one: a halved copy's step is at most twice that span (stampedLight's finer copy and the next), a bilinear sample reads
two of its pixels, and held at the picture's edge it reaches up to two steps past the point, so 2 × 2. Past this, and kLitSlack
more for rounding, the picture is exactly 0 there, so `at()` returns that without sampling: the same pixels, faster where a thin
shape's square is mostly dark (Flat Nib at 35 px, its stamps 1 % of the size apart, on one thread: the recorded C replayed in
1.58 → 1.35 s, 0.44 of it start-up).

## Other values

### `kGreyLevels`

An 8-bit grey picture's white, 255.

### `invertedLight`

Not a value but worth its note: Invert, as the reference's, is not a plain flip across black and white but the plain flip made
lighter by twice the picture's own darkest grey d, at the flip's slope and capped at white; with d black it's the plain flip. On
the Pastel picture (greys 47 to 255) it runs from 94 to 255, greys up to 94 white, 89 levels lighter than the plain flip on
average, as the reference's reads.
