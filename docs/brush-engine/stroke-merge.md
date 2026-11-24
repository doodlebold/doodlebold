# StrokeMerge.cpp and StrokeMerge.h: laying a stroke on the layer

The named values in `src/core/stroke/StrokeMerge.cpp` and `src/core/stroke/StrokeMerge.h`: what they are, what they do, and how
they were chosen. The values themselves are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## Edges through the paper

### `kSoftEdgeBar`, `kSoftEdgeBarPower`, `kSoftEdgeRise`

**What they do.** A soft Shape's edge breaks into the paper's tops, as the reference's does. Where its outline covers a share S
of a pixel, the paper takes paint only where its own share there is above a bar, kSoftEdgeBar times (1 - S) to the power
kSoftEdgeBarPower, and above the bar its share counts from it to 1, to the power kSoftEdgeRise.

- So a faint rim reaches only the paper's highest tops, and where the outline is whole the bar is 0 and the coat shows the
  paper's grain as a share of itself.
- A lower Intensity narrows the broken band by raising the paper's shares, not the bar, and a share of 1 always lands, so at
  Intensity 0 a soft line is its plain self (the soft ramp at Intensity 50: 22.8 levels off, floor 12.0; with the bar scaled by
  Intensity, 35.2).

**How they were chosen.** Fitted to the reference's soft Pencil (the soft ramp: 18.3 levels of 8 bits off, against its plain
floor of 12.0). Neighbours:

| Tried | Levels off |
| --- | --- |
| A bar of 1.0 / 1.2 | 21.0 / 19.1 |
| A power of 0.35 / 0.5 | 21.5 / 26.2 |
| A rise of 0.5 / 0.7 | 21.0 / 20.8 |

### `kHardEdgeBarDepth`

**What it does.** A hard Shape's edge breaks into the paper's tops too, as the reference's does: where its outline covers S < 1,
the thinned coat keeps clamp(1 + (share - bar) / kHardEdgeBarWidth) of itself, bar = kHardEdgeBarDepth times the soft edge's.

**How it was chosen.** Fitted to the reference's hard Pencil and Eraser against the margin (each case within its plain floor + 2
levels of 8 bits). The depth and width lie on a ridge, depth about 0.85 over width:

- 1.25 and 0.4 leave the most room, the Pencil's C at 200 px 2.39 against 2.53;
- 1.2 and 0.35, 1.3 and 0.45 alike;
- 1.0 and 0.3: the full-pressure ramp 4.30 against 3.96.

### `kSoftEdgeHardFade`

**What it does.** On the 2nd to 4th Shapes, the hard edge's thinning in their blend fades out below a bar
StrokePaint::softHardDepth times the soft one, over this much of the paper's share, as a hard edge's does below its own, so no
haze is left past the clumps.

**How it was chosen.** Fitted to the second round's readings with the Shapes' depths (RebuiltStroke's kSoftEdgeHardDepth):

- the 2nd Shape 19.68 levels of 8 bits off (20.18 with its haze), its total error 38 % lower;
- the 4th 14.42 (15.20).

A width of 0.8 left a haze; 0.2 was worse on both.

## Mixing tools' coats

### `kCoatLeastRise`, `kCoatWholeAlpha`

**What they do.** They guard the step where a mixing tool's alpha says how much of a pixel it coated:

- **kCoatLeastRise:** a rise of alpha under this (about 6.5 levels of 16 bits) is rounding on a pixel whose colour alone
  changed, not a coat.
- **kCoatWholeAlpha:** over paint at this or more (about 65 levels below whole), the gap left, 1 - a0, is so small that a
  rounding step in either alpha moves the coat (a1 - a0) / (1 - a0) by a lot.

Both go the paper's share instead.

**How they were chosen.** Guards on the arithmetic, not fitted: the readings are the same to the hundredth for a least rise of
1e-5 to 1e-2 and a whole alpha of 0.99 to never (above 1). At 0.9, the Brush's two-stroke reading goes from 5.07 to 5.45 levels
of 8 bits.

### `uncoatedDensity`, `uncoatedTop`, `edgeBarDepth`, `edgeBarWidth`

**What they do.** The Brush (thinsCoat), where its alpha can't say how much it coated (over paint already whole):

- **uncoatedDensity, uncoatedTop:** a whole coat through the paper, its painted tile going uncoatedTop · (1 - exp(-uncoatedDensity
  · share)) of its way; 0 keeps the paper's share.
- **edgeBarDepth, edgeBarWidth:** on its edge, where its outline covers less than all of a pixel, the paper's tops alone take it,
  as a hard stamp's edge: a bar edgeBarDepth times the soft edge's, faded over edgeBarWidth; 0 for none.

## Erasing through the paper

### `kPaperFullDensityErasing`

**What it does.** Erasing through the paper, the coat counts as full from this density.

**How it was chosen.** On the reference's Eraser over solid paint (the C at 35 / 100 / 400 px, Canvas and Paper Grain at Strength
95 and 100):

- 8 left the twelve cases 12.6 to 32.8 levels off, its coat erasing 0.86 to 0.92 on average where the reference's erases 0.81 to
  0.85.
- At 100 px, Canvas / Paper Grain 95:

  | Density | 7 | 6 | 5 | 4 |
  | --- | --- | --- | --- | --- |
  | Levels off | 19.8 / 12.7 | 14.9 / 13.8 | 15.0 / 18.1 | 21.4 / 27.3 |

- At 6 every case is closer: Canvas 13.8 to 22.7, Paper Grain 11.9 to 20.2.
- Canvas alone would take 5 and Paper Grain 7: the curve itself still differs a little.

### `kErasingDensityShare`

**What it does.** Erasing through the paper, where the coat is full, the paper bites by the density the stamps laid at the
pixel, summed, this share of it, at most kPaperFullDensity. Short of full, it bites by that or by what the coat itself says
(kPaperFullDensityErasing), the more.

**How it was chosen.** The reference's Eraser takes more where more of its stamps overlap, at the same paper: at a mid-light paper
0.835 at the C's middle, 0.69 at its edge, where a coat counting as full from kPaperFullDensityErasing took 0.755 across. Fitted
on its Eraser over solid paint, Canvas and Paper Grain at 35 / 100 / 400 px, Strength 95 and 100:

| Share | 0.8 | 0.85 | 0.9 | 0.95 | 1.0 |
| --- | --- | --- | --- | --- | --- |
| Levels on average | 9.77 | 8.02 | 7.63 | 7.68 | 8.10 |

That's against 11.49 before. By the summed density alone everywhere took them to 6.28, but the light end of the texture
readings' Eraser and transparent-colour ramps (Density on pressure) went 2.0 to 3.3 -> 5.8 levels.
