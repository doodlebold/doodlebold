# BrushDab.h: the reference's dabs

The named values in `src/core/stroke/BrushDab.h`: what they are, what they do, and how they were chosen. The values themselves
are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## `kWaterStampShare`

**What it does.** Water Color laid in groups, 1st to 5th Shape: its stamp is the Brush's at full pressure times this, and times
pressure's share of Density, straight.

**How it was chosen.** Its click is the Brush's times these, at every radius, against the Brush's clicks:

| Shape | 1st | 2nd | 3rd | 4th | 5th |
| --- | --- | --- | --- | --- | --- |
| Click against the Brush's | 0.571 | 0.765 | 0.787 | 0.782 | 0.785 |

That is this stamp under the (1 - Dilution)^kWaterDilutionPower = 0.60 a group carries over bare paper at the starting
settings.

- Its pen clicks at 0.6 to 1.0 on the 3rd and 5th Shapes are then the reference's within 0.002 (the 1st within 0.03 at 0.75,
  stronger than ours below full pressure).
- All its clicks are 0.45 levels from the reference's on average.

## `kWaterShapePressurePower`

**What it does.** The stamp follows pressure's share of Density to this power, by Shape.

**How it was chosen.** Read from the reference's pen clicks at 0.6 / 0.75 / 0.9 / 1.0 against the straight rule's 3rd and 5th:

- **1st Shape:** its clicks are 0.80 / 0.88 / 0.95 of that rule in proportion; the share to 0.44 gives 0.799 / 0.881 / 0.955.
- **2nd:** 0.054 / 0.169 / 0.323 against 0.052 / 0.162 / 0.311 straight; at 0.85, 0.056 / 0.169 / 0.316 (0.8: 0.172 at 0.75;
  0.9: 0.167).
- **4th:** lies on the straight rule (0.021 / 0.065 / 0.124).

| Measure (levels) | Before | After |
| --- | --- | --- |
| Clicks | 0.45 | 0.16 |
| The lines | 1.96 | 1.12 |
| The 36 Water readings | 4.58 | 4.43 |

On the lines, the 1st Shape at 0.6 went 0.168 -> 0.212, against 0.205.

## `kBrushPressureSteps`

**What it does.** The reference solves a Brush stamp's opacity rather than setting it: the opacity at which a straight line of
such stamps, at the stamp's own spacing and falloff, each taking its share of what's left, builds to a target. The target is
Density times the pressure's share, a touch under full.

**How it was chosen.** The pressure's share was read from the reference's clicks against pen pressure at 200 px, as the line
each click's centre builds (brushLineBuild) over the one at full pressure: full from 0.625 on.

The rows are the 1st to 5th Shapes, then the 5th with Anti-Ripple off. The 2nd and 4th, not painted, lie between their
neighbours.
