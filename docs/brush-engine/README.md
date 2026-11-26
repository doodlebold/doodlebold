# The brush engine's named values

Each page here covers one source file of the brush engine. For every named value (the constants and the longer-lived fields)
it says what the value is, what it does, and how it was chosen, with the measurements behind it.

- **In the code:** each value keeps a one-line comment, and each file points to its page once, near the top.
- **The value itself** lives only in the code, so a page never goes stale when a value is tuned.
- **When a value is refitted,** its entry here says how.

## How a stroke flows through the engine

1. `StrokeController` (src/core/paint) takes the pen's samples.
2. `StrokeSmoother` steadies them.
3. `RebuiltStroke` sets each tool's tip from its settings.
4. `StampTip` lays stamps for the tools that don't mix, and `DabTip` lays dabs for those that do (Brush, Water Color, Marker,
   Blur, Smudge).
5. `StrokeMerge` lays what they built onto the layer, through the paper when there is one.

`StrokeWorker` runs all this on the stroke's own thread.

Painting stops at the canvas's edge, though a layer keeps what was moved past it:

- **Nothing is laid past the edge.** A tile that reaches past it keeps there what the layer held before the stroke (`keptOnCanvas`,
  `RebuiltStroke.cpp`).
- **The mixing tools pick up only from the canvas.** A sample past its edge counts for nothing: it adds neither colour nor weight
  (`DabTipMixing.inc`).
- **The Water Color and Marker melt sees the edge go on.** Past the edge, the melt reads the edge's pixels again
  (`readRow`, `cellAverage`, `DabTipBlur.inc`).
- **The Clone Stamp copies only from the canvas** (`StrokeMerge.cpp`).

## Pages

| Page | Source |
| --- | --- |
| [stroke-smoother.md](stroke-smoother.md) | `src/core/paint/StrokeSmoother.cpp` |
| [rebuilt-stroke.md](rebuilt-stroke.md) | `src/core/stroke/RebuiltStroke.cpp` |
| [stamp-tip.md](stamp-tip.md) | `src/core/stroke/StampTip.cpp` |
| [stamp-form.md](stamp-form.md) | `src/core/stroke/StampForm.cpp` |
| [dab-tip.md](dab-tip.md) | `src/core/stroke/DabTip.cpp` and its parts, `DabTip*.inc` |
| [bristles.md](bristles.md) | `src/core/stroke/Bristles.h` |
| [brush-dab.md](brush-dab.md) | `src/core/stroke/BrushDab.h` |
| [paper-grain.md](paper-grain.md) | `src/core/stroke/PaperGrain.cpp` |
| [stroke-merge.md](stroke-merge.md) | `src/core/stroke/StrokeMerge.cpp`, `StrokeMerge.h` |
| [stroke-worker.md](stroke-worker.md) | `src/core/stroke/StrokeWorker.cpp` |

## Terms

These words mean the same thing on every page.

| Term | Meaning |
| --- | --- |
| The reference | The painting program Doodlebold's brushes are fitted to. |
| Levels | 8-bit levels of 255, unless an entry says 16-bit. |
| The C | The recorded C-shaped stroke in `tests/fixtures/brush-pixels/strokes/`. |
