#!/usr/bin/env python3
"""Draws Doodlebold's built-in brush pictures into resources/brushes/, a brush shape's settings file beside its picture where it
has one, and writes its brushes.qrc.

Every picture is made here from fixed seeds, with nothing but the standard library: tip shapes, blot maps, bristle tips,
brush textures and paper textures. Dark is where paint goes, as in a picture the user imports, but for brush shapes, which
paint where they're light, a light shape on a dark ground, as the reference's own brush-shape files are. Textures and papers
repeat without a seam. Run it again after changing it; the output is the same each time.

Usage: tools/generate_brush_pictures.py [output folder, default resources/brushes]
"""

import argparse
import math
import random
import struct
import zlib
from collections.abc import Callable
from pathlib import Path

TIP = 256  # tip pictures, a side
TILE = 256  # textures and papers, a side
HAIR_MAP = 63  # bristle maps, a side: the reference's own maps'
LIGHT_PAINTS = {"forms/shapes"}  # the folders whose pictures are light where they paint
DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "resources" / "brushes"

# Flat Nib's settings file, in the layout BrushResources.cpp reads: a calligraphic nib held still, so a stroke is thick
# across the nib's width and thin along its edge, with no stamps showing. Judged by eye; neither SAI nor the reference has a Flat Nib.
NIB_ANGLE_CONTROL = 0  # Angle Control "none": the nib keeps its Angle whichever way the stroke goes, as a pen nib in the hand does
# degrees: the nib's edge rises to the right at 45 degrees, the usual calligraphic hold, so a stroke up and right is a hairline and one
# down and right is the nib's full width. Turning is clockwise on the canvas, whose y runs down, so rising to the right is -45.
NIB_ANGLE = -45
# % of the size between stamps, the panel's least. The nib's thin side is about 0.27 of its size, so this is about 4 % of it. Swept
# over 1, 2, 3, 4 and 6 on straight lines (0, 45, 90 and 135 degrees), an S-curve and the recorded C at 35, 100 and 400 px: every one
# keeps its edges straight (within 0.2 px), but the wider the spacing the softer its edges and ends (on the 400 px C, 69, 91, 106 and
# 136 thousand partly covered pixels at 1, 2, 3 and 6), and 2 already feathers the start and the lift. 1 costs most: the C at 400 px
# on 16 threads in 1.2 s against 0.8 s at 2 and 0.6 s at 6.
NIB_SPACING = 1
# Doodlebold's own key, which the reference's files don't have: 1 lays the nib's stamps as one continuous stroke, each pixel as
# dark as the darkest stamp over it, so at 1 % apart they don't build up to solid and Dens., Density and density pressure set its tone.
NIB_ONE_STROKE = 1
# Each value with the reason the file gives beside it.
SHAPE_SETTINGS: dict[str, dict[str, dict[str, tuple[int, str]]]] = {
    "forms/shapes": {"Flat Nib": {
        "AngleControl": (NIB_ANGLE_CONTROL, "none: the nib is held still, as a pen nib in the hand"),
        "Angle": (NIB_ANGLE, "the edge rising to the right at 45 degrees, the usual calligraphic hold: up-right strokes are hairlines, "
                             "down-right ones the nib's full width"),
        "Spacing": (NIB_SPACING, "% of the size, about 4 % of the nib's thin side: swept 1 to 6, all straight-edged, 2 and wider feather "
                                 "the ends; the 400 px C 1.2 s here against 0.6 s at 6"),
        "OneStroke": (NIB_ONE_STROKE, "Doodlebold's own: the stamps lay as one stroke, not adding up, so Dens. sets the tone"),
    }},
}

Shade = Callable[[int, int], float]  # how much paint a picture has at a pixel, 0 … 1
Coverage = list[list[float]]  # rows of 0 … 1, where 1 is full paint (drawn black)


# --- writing ------------------------------------------------------------------------------------------------------------

def write_png(path: Path, size: int, coverage: Coverage, light_paints: bool = False) -> None:
    raw = bytearray()
    for row in coverage:
        raw.append(0)
        raw.extend((lambda v: v if light_paints else 255 - v)(max(0, min(255, round(c * 255)))) for c in row)

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)  # 8-bit greyscale
    image = zlib.compress(bytes(raw), 9)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", image) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def write_shape_settings(path: Path, values: dict[str, tuple[int, str]]) -> None:
    """A brush shape's settings file: a [Parameters] section of `Key = value` lines, each after its reason as a `;` comment; a key
    left out keeps its default."""
    lines = ["; Written by tools/generate_brush_pictures.py", "[Parameters]"]
    for key, (value, reason) in values.items():
        lines += [f"; {reason}", f"{key} = {value}"]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def picture(size: int, shade: Shade) -> Coverage:
    return [[shade(x, y) for x in range(size)] for y in range(size)]


# --- helpers ------------------------------------------------------------------------------------------------------------

def clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return lo if v < lo else hi if v > hi else v


def smoothstep(e0: float, e1: float, v: float) -> float:
    t = clamp((v - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


class Noise:
    """Value noise that repeats every `period` cells, summed over octaves (each twice as fine, half as strong)."""

    def __init__(self, seed: int, period: int, octaves: int = 4, size: int = TILE) -> None:
        rnd = random.Random(seed)
        self.size = size
        self.layers = []
        p = period
        for _ in range(octaves):
            self.layers.append((p, [[rnd.random() for _ in range(p)] for _ in range(p)]))
            p *= 2

    def __call__(self, x: float, y: float) -> float:
        total, weight, amp = 0.0, 0.0, 1.0
        for period, grid in self.layers:
            fx = x * period / self.size
            fy = y * period / self.size
            x0, y0 = int(math.floor(fx)), int(math.floor(fy))
            tx, ty = fx - x0, fy - y0
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            x0 %= period
            y0 %= period
            x1, y1 = (x0 + 1) % period, (y0 + 1) % period
            top = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx
            bottom = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx
            total += amp * (top + (bottom - top) * ty)
            weight += amp
            amp *= 0.5
        return total / weight


def centred(x: int, y: int, size: int = TIP) -> tuple[float, float]:
    """-1 … 1 across the picture, pixel centres."""
    return (2 * (x + 0.5) / size - 1, 2 * (y + 0.5) / size - 1)


def contrast(v: float, amount: float, mid: float = 0.5) -> float:
    return clamp(mid + (v - mid) * amount)


# --- tip shapes ---------------------------------------------------------------------------------------------------------

def soft_edge() -> Shade:
    """A solid disc of 254/255 with a lobed outline and a short soft edge, as the reference's Soft Edge stamps (a
    click at 200 px): half its darkness at 0.717 of the radius, the outline wandering by 0.041 in a few wide lobes (harmonics 2 to
    14), the edge a smoothstep 0.12 of the radius either side. The click then lays 21,765 px against the reference's 21,470, mean 0.746
    against 0.754, its mean by radius 1.7 levels off (RMS), 4.4 at worst."""
    rnd = random.Random(11)
    turns = 4096
    harmonics = range(2, 15)
    amplitude = 0.041 * math.sqrt(2.0 / len(harmonics))
    waves = [(k, amplitude, rnd.random() * 2 * math.pi) for k in harmonics]
    half = [0.717 + sum(a * math.cos(k * 2 * math.pi * i / turns + p) for k, a, p in waves) for i in range(turns)]

    def shade(x: int, y: int) -> float:
        u, v = centred(x, y)
        r = math.hypot(u, v)
        i = int((math.atan2(v, u) / (2 * math.pi) % 1.0) * turns) % turns
        return 254 / 255 * (1 - smoothstep(half[i] - 0.12, half[i] + 0.12, r))
    return shade


def chalk() -> Shade:
    grain = Noise(12, 24, 3, TIP)
    edge = Noise(13, 5, 2, TIP)

    def shade(x: int, y: int) -> float:
        u, v = centred(x, y)
        r = math.hypot(u, v) / (0.85 + 0.1 * (edge(x, y) - 0.5))
        disc = 1 - smoothstep(0.8, 1.0, r)
        return disc * smoothstep(0.32, 0.6, grain(x, y))
    return shade


def charcoal() -> Shade:
    streaks = Noise(14, 8, 4, TIP)
    angle = math.radians(30)
    ca, sa = math.cos(angle), math.sin(angle)

    def shade(x: int, y: int) -> float:
        u, v = centred(x, y)
        a, b = u * ca + v * sa, -u * sa + v * ca
        r = math.hypot(a / 0.9, b / 0.5)
        body = 1 - smoothstep(0.7, 1.0, r)
        # Streaks along the long side: the noise is read stretched across it.
        g = streaks(TIP / 2 + a * TIP * 0.08, TIP / 2 + b * TIP * 1.2)
        return body * smoothstep(0.3, 0.62, g)
    return shade


def splatter() -> Shade:
    rnd = random.Random(15)
    drops = []
    for _ in range(46):
        d = 0.85 * math.sqrt(rnd.random())
        t = rnd.random() * 2 * math.pi
        drops.append((d * math.cos(t), d * math.sin(t), 0.025 + 0.11 * rnd.random() * (1 - d)))

    def shade(x: int, y: int) -> float:
        u, v = centred(x, y)
        best = 0.0
        for cx, cy, r in drops:
            best = max(best, 1 - smoothstep(r * 0.8, r, math.hypot(u - cx, v - cy)))
        return best
    return shade


def flat_nib() -> Shade:
    def shade(x: int, y: int) -> float:
        u, v = centred(x, y)
        dx, dy = max(abs(u) - 0.72, 0.0), max(abs(v) - 0.12, 0.0)
        return 1 - smoothstep(0.1, 0.2, math.hypot(dx, dy))
    return shade


# --- blot maps ----------------------------------------------------------------------------------------------------------

def blots(seed: int, period: int, mottled: bool = False) -> Shade:
    noise = Noise(seed, period, 3, TIP)
    mottling = Noise(seed + 100, 32, 2, TIP) if mottled else None

    def shade(x: int, y: int) -> float:
        c = smoothstep(0.47, 0.53, noise(x, y))
        if mottling:
            c *= 0.45 + 0.55 * smoothstep(0.3, 0.7, mottling(x, y))
        return c
    return shade


# --- bristle tips -------------------------------------------------------------------------------------------------------

def hair_map(seed: int, count: int, place: Callable[[random.Random], tuple[float, float]]) -> Shade:
    """A bristle map: one dark pixel a hair, each a pixel of its own."""
    rnd = random.Random(seed)
    hairs: set[tuple[int, int]] = set()
    while len(hairs) < count:
        u, v = place(rnd)
        hairs.add((int((u + 1) / 2 * HAIR_MAP), int((v + 1) / 2 * HAIR_MAP)))
    return lambda x, y: 1.0 if (x, y) in hairs else 0.0


def in_disc(rnd: random.Random) -> tuple[float, float]:
    d = 0.86 * math.sqrt(rnd.random())
    t = rnd.random() * 2 * math.pi
    return d * math.cos(t), d * math.sin(t)


def in_band(rnd: random.Random) -> tuple[float, float]:
    return -0.86 + 1.72 * rnd.random(), -0.22 + 0.44 * rnd.random()


# --- textures and papers ------------------------------------------------------------------------------------------------

def grain(seed: int, period: int, octaves: int, amount: float) -> Shade:
    noise = Noise(seed, period, octaves)
    return lambda x, y: contrast(noise(x, y), amount)


def canvas_weave(seed: int, threads: int, noise_amount: float) -> Shade:
    """Threads over and under each other: in each cell one is on top, and its raised middle takes paint."""
    noise = Noise(seed, 32, 3)

    def shade(x: int, y: int) -> float:
        fx, fy = (x + 0.5) * threads / TILE, (y + 0.5) * threads / TILE
        across = (int(fx) + int(fy)) % 2 == 0
        top = math.sin(math.pi * ((fy if across else fx) % 1.0))
        end = math.sin(math.pi * ((fx if across else fy) % 1.0)) ** 0.3  # the thread dips where it goes under
        return clamp(0.15 + 0.7 * top * end + noise_amount * (noise(x, y) - 0.5))
    return shade


def linen(seed: int) -> Shade:
    rows = Noise(seed, 64, 2)
    cols = Noise(seed + 1, 64, 2)

    def shade(x: int, y: int) -> float:
        h = rows(0, y * 1.0) * 0.6 + rows(x * 0.02, y) * 0.4
        v = cols(x, 0) * 0.6 + cols(x, y * 0.02) * 0.4
        return contrast((h + v) / 2, 2.2)
    return shade


def sponge(seed: int) -> Shade:
    noise = Noise(seed, 12, 3)

    def shade(x: int, y: int) -> float:
        ridge = 1 - abs(noise(x, y) - 0.5) * 2
        return smoothstep(0.55, 0.9, ridge)
    return shade


def watercolor_paper(seed: int) -> Shade:
    bumps = Noise(seed, 6, 3)
    fine = Noise(seed + 1, 64, 2)
    return lambda x, y: contrast(0.65 * bumps(x, y) + 0.35 * fine(x, y), 2.4)


def fibres(seed: int) -> Shade:
    rnd = random.Random(seed)
    base = Noise(seed + 1, 48, 2)
    strands = []
    for _ in range(140):
        x, y = rnd.random() * TILE, rnd.random() * TILE
        t = rnd.random() * math.pi
        strands.append((x, y, math.cos(t), math.sin(t), 10 + 30 * rnd.random()))
    field = [[0.0] * TILE for _ in range(TILE)]
    for x, y, dx, dy, length in strands:
        for i in range(int(length * 2)):
            px, py = int(x + dx * i * 0.5) % TILE, int(y + dy * i * 0.5) % TILE
            field[py][px] = min(1.0, field[py][px] + 0.5)
    return lambda x, y: clamp(0.35 + 0.4 * (base(x, y) - 0.5) + 0.45 * field[y][x])


# --- the set ------------------------------------------------------------------------------------------------------------

PICTURES: dict[str, dict[str, tuple[int, Shade]]] = {
    "forms/shapes": {
        "Soft Edge": (TIP, soft_edge()),
        "Chalk": (TIP, chalk()),
        "Charcoal": (TIP, charcoal()),
        "Splatter": (TIP, splatter()),
        "Flat Nib": (TIP, flat_nib()),
    },
    "forms/blots": {
        "Blots": (TIP, blots(21, 4)),
        "Fine Blots": (TIP, blots(22, 10)),
        "Mottled Blots": (TIP, blots(23, 5, mottled=True)),
    },
    "forms/bristles": {
        "Round Bristle": (HAIR_MAP, hair_map(31, 70, in_disc)),
        "Flat Bristle": (HAIR_MAP, hair_map(32, 60, in_band)),
        "Sparse Bristle": (HAIR_MAP, hair_map(33, 22, in_disc)),
    },
    "textures": {
        "Paper Grain": (TILE, grain(41, 32, 3, 1.8)),
        "Canvas": (TILE, canvas_weave(42, 32, 0.3)),
        "Rough": (TILE, grain(43, 8, 4, 2.2)),
        "Linen": (TILE, linen(44)),
        "Sponge": (TILE, sponge(45)),
    },
    "papers": {
        "Paper": (TILE, grain(51, 64, 2, 1.5)),
        "Canvas": (TILE, canvas_weave(52, 24, 0.25)),
        "Watercolor Paper": (TILE, watercolor_paper(53)),
        "Fine Grain": (TILE, grain(54, 128, 1, 1.4)),
        "Fibres": (TILE, fibres(55)),
    },
}


def resource_list(files: list[str]) -> str:
    """The brushes.qrc that lists these pictures."""
    lines = ['<!DOCTYPE RCC>', '<RCC version="1.0">', '  <!-- Written by tools/generate_brush_pictures.py -->',
             '  <qresource prefix="/brushes">']
    lines += [f"    <file>{file}</file>" for file in files]
    lines += ["  </qresource>", "</RCC>", ""]
    return "\n".join(lines)


def write_set(out: Path, verbose: bool = False) -> None:
    files = []
    for folder, pictures in PICTURES.items():
        for name, (size, shade) in pictures.items():
            path = out / folder / f"{name}.png"
            write_png(path, size, picture(size, shade), folder in LIGHT_PAINTS)
            files.append(f"{folder}/{name}.png")
            if verbose:
                print(path)
            if name in SHAPE_SETTINGS.get(folder, {}):
                write_shape_settings(path.with_suffix(".ini"), SHAPE_SETTINGS[folder][name])
                files.append(f"{folder}/{name}.ini")
                if verbose:
                    print(path.with_suffix(".ini"))
    (out / "brushes.qrc").write_text(resource_list(files), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", nargs="?", type=Path, default=DEFAULT_OUTPUT, help="the folder to write into")
    write_set(parser.parse_args().output, verbose=True)


if __name__ == "__main__":
    main()
