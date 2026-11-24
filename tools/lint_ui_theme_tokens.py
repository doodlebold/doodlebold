#!/usr/bin/env python3
"""Flags raw visual values in Doodlebold's UI code: everything the chrome shows takes its numbers and colours from Theme
(src/ui/Theme.h) or the shared components (src/ui/Components.h).

Usage: tools/lint_ui_theme_tokens.py [--list-allowed]
Exits 1 and prints file:line: what for each finding; 0 when the UI code is clean.

What it flags, in src/ui, src/platform, src/app and resources/theme:
  - layout calls with a number in them: setContentsMargins, setSpacing, addSpacing, setHorizontal/VerticalSpacing
  - size calls with a number: setFixedSize/Width/Height, setMinimum*/setMaximum*, setIconSize
  - a corner radius written as a number: drawRoundedRect, addRoundedRect
  - fonts sized or weighted in place: setPointSize(F), setPixelSize, setBold
  - colours: #rrggbb, QColor(r, g, b) and QColor(0x…), qRgb/qRgba, Qt::white/black/…, and pens with a number for a width
  - inline style sheets: setStyleSheet, and padding/margin/border-radius/font-size inside a C++ string
  - a visual constant (a size, gap, padding, margin, inset, radius, icon, border, pen, font, alpha or opacity) defined as a
    number instead of from Theme
  - in the style sheet: px values, hex and rgba colours (placeholders such as @space3 and @Accent are the way)
Numbers 0 and 1, and a number that multiplies or divides (x / 2, 2 * gap), are not flagged.

Exceptions:
  - a line that says "component metric" (a component's own geometry, with what it is) or "ui-tokens: allow" (with why)
  - tools/ui_theme_tokens_allowlist.txt: whole files or lines, each with its reason (the canvas, the user's own colours, Theme)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["src/ui", "src/platform", "src/app", "resources/theme"]
ALLOWLIST = os.path.join(ROOT, "tools", "ui_theme_tokens_allowlist.txt")
INLINE_ALLOW = re.compile(r"component metric|ui-tokens: allow")

# A number that counts: not 0 or 1, and not a factor or divisor (`/ 2`, `* 2`, `2 *`).
NUMBER = re.compile(r"(?<![\w.])(?<![*/]\s)(?<![*/])(\d+(?:\.\d+)?)(?!\s*\*)(?![\w.])")


def counts(args):
    for m in NUMBER.finditer(args):
        if float(m.group(1)) > 1:
            return True
    return False


CPP_CALLS = re.compile(r"\b(setContentsMargins|setSpacing|addSpacing|setHorizontalSpacing|setVerticalSpacing|setFixedSize|"
                       r"setFixedWidth|setFixedHeight|setMinimumSize|setMinimumWidth|setMinimumHeight|setMaximumSize|"
                       r"setMaximumWidth|setMaximumHeight|setIconSize)\s*\((.*)\)")
ROUNDED = re.compile(r"\b(drawRoundedRect|addRoundedRect)\s*\((.*)\)")
FONT = re.compile(r"\b(setPointSizeF?|setPixelSize)\s*\(([^;]*)\)|\bsetBold\s*\(\s*true\s*\)")
HEX = re.compile(r"#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{8}\b")
QCOLOR = re.compile(r"\bQColor\s*\(\s*(0x[0-9a-fA-F]+|\d+)\s*,|\bqRgba?\s*\(|\bQt::(white|black|red|green|blue|gray|yellow|cyan|magenta)\b")
QRGB_CONST = re.compile(r"\bQRgb\s+k\w+\s*=\s*0x[0-9a-fA-F]+")
PEN = re.compile(r"\bQPen\s*\((.*)\)")
STYLESHEET = re.compile(r"\bsetStyleSheet\s*\(")
QSS_IN_CPP = re.compile(r"\"[^\"]*(padding|margin|border-radius|font-size)\s*:[^\"]*\"")
VISUAL_NAME = re.compile(r"(Size|Width|Height|Gap|Pad|Padding|Margin|Inset|Radius|Spacing|Icon|Border|Pen|Font|Alpha|Opacity|"
                         r"Indent|Clearance|Thickness|Inner|Stepper|Swatch|Grip)")
CONSTANT = re.compile(r"\bconstexpr\s+(?:int|double|qreal|float)\s+(k\w+)\s*=\s*([^;]+);")
QSS_PX = re.compile(r"(?<![\w@-])-?\d+(?:\.\d+)?(px|pt)\b")
QSS_COLOUR = re.compile(r"#[0-9a-fA-F]{3,8}\b|\brgba?\s*\(")


def load_allowlist():
    files, lines = set(), []
    if not os.path.exists(ALLOWLIST):
        return files, lines
    with open(ALLOWLIST, encoding="utf-8") as f:
        for raw in f:
            entry = raw.split("#", 1)[0].strip()
            if not entry:
                continue
            if "::" in entry:
                path, pattern = entry.split("::", 1)
                lines.append((path.strip(), re.compile(pattern.strip())))
            else:
                files.add(entry)
    return files, lines


def check_cpp(path, text):
    found = []
    in_comment = False
    for number, line in enumerate(text.splitlines(), 1):
        code = line
        if in_comment:
            if "*/" not in code:
                continue
            code = code.split("*/", 1)[1]
            in_comment = False
        if "/*" in code and "*/" not in code:
            in_comment = True
            code = code.split("/*", 1)[0]
        code = code.split("//", 1)[0]
        if not code.strip() or INLINE_ALLOW.search(line):
            continue
        what = []
        for m in CPP_CALLS.finditer(code):
            if counts(m.group(2)):
                what.append(m.group(1) + " with a number")
        for m in ROUNDED.finditer(code):
            radii = m.group(2).rsplit(",", 2)[-2:]
            if any(counts(r) for r in radii):
                what.append("a corner radius as a number")
        for m in FONT.finditer(code):
            if m.group(0).startswith("setBold") or counts(m.group(2) or ""):
                what.append("a font sized or weighted in place")
        if HEX.search(code) or QCOLOR.search(code) or QRGB_CONST.search(code):
            what.append("a raw colour")
        for m in PEN.finditer(code):
            parts = m.group(1).split(",")
            if len(parts) > 1 and counts(parts[1]):
                what.append("a pen width as a number")
        if STYLESHEET.search(code):
            what.append("an inline style sheet")
        if QSS_IN_CPP.search(code):
            what.append("style sheet values in a string")
        for m in CONSTANT.finditer(code):
            if VISUAL_NAME.search(m.group(1)) and "Theme::" not in m.group(2) and counts(m.group(2)):
                what.append(f"{m.group(1)} defined as a number")
        found += [(number, w) for w in what]
    return found


def check_qss(path, text):
    found = []
    in_comment = False
    for number, line in enumerate(text.splitlines(), 1):
        code = line
        # A comment's text is not checked, but a "component metric" note in it excuses its line.
        if INLINE_ALLOW.search(line):
            continue
        if in_comment:
            if "*/" not in code:
                continue
            code = code.split("*/", 1)[1]
            in_comment = False
        code = re.sub(r"/\*.*?\*/", "", code)
        if "/*" in code:
            code, in_comment = code.split("/*", 1)[0], True
        if QSS_PX.search(code):
            found.append((number, "a px or pt value (use a size placeholder)"))
        if QSS_COLOUR.search(code):
            found.append((number, "a raw colour (use a colour placeholder)"))
    return found


def main():
    allowed_files, allowed_lines = load_allowlist()
    findings = []
    for source in SOURCES:
        for directory, _, names in os.walk(os.path.join(ROOT, source)):
            for name in sorted(names):
                path = os.path.relpath(os.path.join(directory, name), ROOT)
                if not name.endswith((".cpp", ".h", ".qss")) or path in allowed_files:
                    continue
                with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                    text = f.read()
                lines = text.splitlines()
                check = check_qss if name.endswith(".qss") else check_cpp
                for number, what in check(path, text):
                    if any(p == path and pattern.search(lines[number - 1]) for p, pattern in allowed_lines):
                        continue
                    findings.append(f"{path}:{number}: {what}")
    if "--list-allowed" in sys.argv:
        for f in sorted(allowed_files):
            print("allowed file:", f)
        for p, pattern in allowed_lines:
            print("allowed line:", p, pattern.pattern)
    for f in findings:
        print(f)
    if findings:
        print(f"\n{len(findings)} raw visual values in UI code. Use Theme tokens or the shared components "
              "(src/ui/Theme.h, src/ui/Components.h); a component's own geometry says \"component metric\" and what it is.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
