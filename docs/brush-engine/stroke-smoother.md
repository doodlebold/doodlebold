# StrokeSmoother.cpp: the Stabilizer

The named values in `src/core/paint/StrokeSmoother.cpp`: what they are, what they do, and how they were chosen. The values
themselves are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## `kStrongShares`

**What it does.** The strong levels' second stage: at 16 to 20, the reference's S-1 to S-5, it moves this share of the way each
event.

**How it was chosen.** Fitted level by level to the Pencil's sweep cases, the recorded C (recorded-c5) above all, where at 32 px
an event a share 0.005 off moves the line about 20 px along it. Before, it was 0.7 * k^-1.55, fitted to slower strokes.

The values have four figures: two put the C at the old S-7, gone now, twice as far off.

## `kStartEase`, `kStartEaseExtra`

**What they do.** Over a stroke's first events, until the places are all the pen's and one more, its pressure eases in a little
more slowly than the average alone gives it: times (events so far / (the level's events + kStartEaseExtra))^kStartEase. So it
starts at a fine point and widens over two or three events, as the reference's does. Levels 0 and 1 average one event and don't
ease.

**How they were chosen.** Fitted to the reference's Pencil and Brush at levels 3 and 4 on the recorded C. Before, it was 0.6 over
the level's events alone, fitted at levels 7, 11 and 15.
