"""The committed brush pictures must be what tools/generate_brush_pictures.py draws, and what brush_pictures.txt records.

Only three of the pictures are painted by a golden case, so the golden check can't notice a change to the others.
The saved checksums also notice the generator and its pictures changing together, which comparing those two can't.

After changing a picture on purpose: python3 tools/tests/test_brush_pictures.py --write
"""
import hashlib
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import generate_brush_pictures  # noqa: E402

COMMITTED = ROOT / "resources" / "brushes"
CHECKSUMS = Path(__file__).with_name("brush_pictures.txt")


def pixels(path: Path) -> bytes:
    """The image data, unpacked: another zlib can pack the same pixels into different bytes."""
    data = path.read_bytes()
    position, packed = 8, b""
    while position < len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        if data[position + 4:position + 8] == b"IDAT":
            packed += data[position + 8:position + 8 + length]
        position += 12 + length
    return zlib.decompress(packed)


def pngs(folder: Path) -> list[Path]:
    return sorted(path.relative_to(folder) for path in folder.rglob("*.png"))


def committed_checksums() -> dict[str, str]:
    return {picture.as_posix(): hashlib.sha256(pixels(COMMITTED / picture)).hexdigest() for picture in pngs(COMMITTED)}


def saved_checksums() -> dict[str, str]:
    lines = CHECKSUMS.read_text(encoding="utf-8").splitlines()
    return {name: checksum for checksum, name in (line.split(" ", 1) for line in lines)}


def write_checksums() -> None:
    lines = [f"{checksum} {name}\n" for name, checksum in committed_checksums().items()]
    CHECKSUMS.write_text("".join(lines), encoding="utf-8")


class BrushPicturesTest(unittest.TestCase):
    maxDiff = None

    def test_drawing_again_gives_the_committed_pictures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            drawn = Path(temporary)
            generate_brush_pictures.write_set(drawn)
            self.assertEqual(pngs(drawn), pngs(COMMITTED))
            for picture in pngs(drawn):
                with self.subTest(picture=str(picture)):
                    self.assertEqual(pixels(drawn / picture), pixels(COMMITTED / picture))
            self.assertEqual((drawn / "brushes.qrc").read_bytes(), (COMMITTED / "brushes.qrc").read_bytes())

    def test_the_committed_pictures_are_the_saved_ones(self) -> None:
        self.assertEqual(committed_checksums(), saved_checksums())


if __name__ == "__main__":
    if sys.argv[1:] == ["--write"]:
        write_checksums()
    else:
        unittest.main()
