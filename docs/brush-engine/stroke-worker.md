# StrokeWorker.cpp: the stroke's own thread

The named values in `src/core/stroke/StrokeWorker.cpp`: what they are, what they do, and how they were chosen. The values
themselves are in the code.

> **Terms:** "the reference" is the painting program Doodlebold's brushes are fitted to; "levels" are 8-bit levels of 255
> unless an entry says otherwise. See the [index](README.md#terms).

## `kTimePerGoUs`

**What it does.** In us: how long the stroke's thread lays for before merging, if a frame's waiting. It lays lots of as many
samples as the costliest so far would fill half a go with.

**How it was chosen.** 15, 30 and 60 ms were timed on the view bench, and the plays of one setting spread wider than the
settings differed, so 30 stayed.

## `kBehindFromDiameter`

**What it does.** A stroke this wide whose oldest waiting pen event is this old has fallen behind: what's waiting goes down in
fewer dabs.

**How it was chosen.** The design was kept. Each value is a trade of look against how late the ink is, timed on the view bench
(a recorded 2 s stroke on a 3000 px canvas, the Brush and Water Color, 3 plays a setting, 2 and 4 cores) with no reading of the
reference's lateness.

- **The width:** without grouping, the Brush's ink is 10 ms behind at 500 px on 4 cores, 242 at 750, 448 at 1000 (73 and 93 with
  it); on 2 cores it's over a second behind from 500 px either way. Grouping lays different pixels (up to 43 levels at 800 px on
  Water Color), so 1000 isn't the narrowest width that would help, but where it was kept.
- **The age:** 50, 100 and 200 ms leave the Brush at 1500 px 183, 214 and 308 ms behind on 4 cores (Water Color 750, 769, 851),
  and alike on 2.
- **The lot:** 4, 8 and 16 samples leave it 341, 213 and 109 ms behind on 4 cores (Water Color 1006, 769, 629), and against 8
  move Water Color's backlog cases by up to 41 and 78 levels.
