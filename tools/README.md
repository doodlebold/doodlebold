# Tools

How to build, test and package Doodlebold. Everything goes through CMake presets (`CMakePresets.json`), so a developer, the
workflows in `.github/workflows/` and every operating system run the same commands. CMake 3.25 or newer.

## Which one to run

| You want to | Run | Takes about (16 cores) |
|---|---|---|
| Build and check a change before committing | `cmake --workflow --preset test-before-commit` | 1 minute after an incremental build |
| Check a branch before merging | `cmake --workflow --preset test-before-merge` | 3 minutes after an incremental build |
| Check a change to threading code | `ctest --preset test-thread-counts` | 5 minutes |
| Only build | `cmake --preset linux-develop` once, then `cmake --build --preset linux-develop` | Seconds to 2 minutes |
| Run some tests by name | `ctest --test-dir build -R 'BrushEngine'` | |
| Look for memory errors | `cmake --workflow --preset linux-address-sanitizer` | 16 minutes |
| Look for data races (by hand only) | `cmake --workflow --preset linux-thread-sanitizer` | |
| Build an RPM and install it | `cmake --workflow --preset fedora-rpm`, then `sudo dnf install build-linux-release/*.rpm` | |
| Build the Windows zip on Windows | `cmake --workflow --preset windows-zip` (a Developer prompt, `VCPKG_ROOT` set) | |
| Build and test on Windows without Visual Studio | `cmake --workflow --preset windows-mingw` (an MSYS2 MINGW64 shell with its gcc, cmake, ninja, qt6-base, qt6-svg, gtest and libjpeg-turbo packages) | |
| Build the Windows zip on Linux | `cmake --workflow --preset windows-mingw-zip` (Fedora's mingw64 packages, or a `fedora:44` container) | 1.5 minutes |
| See whether a change made painting faster | `tools/compare_brush_speed.py` | 1 minute, plus building |
| Check formatting, clang-tidy and every warning | `cmake --workflow --preset lint` | 4 minutes after a build |
| Lay out the sources as `.clang-format` says | `cmake --build build --target format` | Seconds |

The everyday build goes in `build/`, every other preset in `build-<preset>/`. `cmake --list-presets all` lists them.
Installing the same version again takes `sudo dnf reinstall`, or a higher release: `cmake --preset linux-release
-DDOODLEBOLD_RPM_RELEASE=2`.

Tests never touch the desktop: the UI tests run on Qt's `offscreen` platform, each shard with a home folder of its own.
Only release.yml starts the program itself, on a Windows runner.

## Formatting and lint

- **Layout:** `.clang-format` (LLVM's style, adjusted to the code) lays out the sources.
- **Checks:** `.clang-tidy` picks the checks: bugprone, performance, modernize, readability and the C++ Core Guidelines checks
  that fit Qt and the painting code. Each check left out says why in the file. `tests/`, `plugins/`, `tests/plugins/` and the
  painting folders (`src/core/stroke`, `paint`, `raster`) add a `.clang-tidy` of their own that leaves a few more out, each with
  its reason.
- **Warnings:** the project's own code builds with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (`/W4` on MSVC). Qt's and
  miniz's headers are system headers, so their warnings don't show.
- **The `lint` preset** makes every warning an error, then fails on a file clang-format would change or anything clang-tidy
  reports. It needs clang-format and run-clang-tidy (Fedora's `clang-tools-extra`).

## Test labels

The test presets pick tests by their CTest label.

| Label | What | In |
|---|---|---|
| `unit` | About 1,050 unit tests, the plugin loader's included | before commit, before merge |
| `ui` | The UI tests, in 8 shards | before commit, before merge |
| `tool-scripts` | The Python tools' tests (`tools/tests/`) and the UI theme-token lint | before commit, before merge |
| `brush-pixels-subset` | 102 of the brush-pixel cases | before commit |
| `brush-pixels-all` | All 479 brush-pixel cases, in 15 parts | before merge |
| `brush-pixels-cores` | All 479 again on 2 and on 4 threads | `test-thread-counts`, and ci.yml |
| `speed-budget` | Speed checks, built only with `-DDOODLEBOLD_BUILD_BENCHES=ON` | Never a gate |

Two unit tests that check wall-clock time are left out of the gates by name, and so is `LargeTips.SavingWork…` (90 s) before a
commit. The presets say which.

## Brush pixels

The brush pixels are Doodlebold's golden results for its brushes: a saved record of the correct output, used to notice when it
changes by accident. `tests/fixtures/brush-pixels/` holds 479 cases. Each is a stroke painted with a certain tool and settings,
kept as a SHA-256 of the layer's 16-bit pixels, and for some cases a picture.

`build/tests/doodlebold_brush_pixels` paints every case again and compares it with its checksum. If even one pixel is different,
the case fails, and its new picture is saved in `build/tests/brush-pixels-differences/`, with the pixels that changed in magenta.
A change to brush code that isn't meant to change how brushes look must leave every case as it was.

The subset before a commit covers every tool at 200 px and at 1500 px over paint, and two stabilizer sweeps. All 479 run before a
merge and on every merge to master.

If you did mean to change how a brush looks, write the checksums again and say so in your commit:

```bash
build/tests/doodlebold_brush_pixels --write tests/fixtures/brush-pixels
```

### Why the pixels match to the bit

`cmake/FloatingPoint.cmake` forbids fused multiply-adds, fast-math and processor-specific flags, and the painting files are built
with AVX2 and nothing newer (`-mavx2`, `/arch:AVX2`). The program refuses processors without AVX2 at start-up. The thread count
doesn't change a pixel (`brush-pixels-cores` proves it).

Measured on 2026-11-22, when there were 408 cases: all of them identical with GCC 16 and Clang 22, on 16 and on 4 cores, and on a processor without
FMA (whose maths library takes other paths).

### Brush pixels on other platforms

`checksums.txt` is the reference: Linux x86-64 with glibc. A maths library that rounds `expm1`, `log1p`, `pow` and the like
differently moves a few pixels by a level or two of 65,535. MinGW-w64 moves 97 of the 408 cases, by at most 3 levels, in at
most 0.093 % of a case's pixels.

Such a platform keeps a checksum file of its own, `checksums-<platform>.txt`, and is checked against it to the bit. Configure
it with `-DDOODLEBOLD_BRUSH_PIXELS_CHECKSUMS=checksums-<platform>.txt`. The file is written only after the platform's renders
pass a comparison with Linux's at the same commit, within 4 levels of 65,535 in at most 0.1 % of a case's pixels
(`kPlatformMostLevels` and `kPlatformMostShare` in `tests/unit/brush_pixels.cpp`):

```bash
# on Linux, at the commit to compare
build/tests/doodlebold_brush_pixels --check tests/fixtures/brush-pixels --dump linux-dumps
# on the other platform, with linux-dumps copied over
doodlebold_brush_pixels --compare linux-dumps --within
doodlebold_brush_pixels --write tests/fixtures/brush-pixels --checksums checksums-<platform>.txt
```

ci.yml does this for Windows when run by hand. Until Windows has its own file, its brush-pixel runs are information only.

## The workflows

| File | When | Jobs |
|---|---|---|
| `pr.yml` | Every pull request | Linux GCC (`test-before-commit`, in `fedora:44`), Windows MSVC (unit and UI tests; brush pixels as information), the MinGW cross-build |
| `ci.yml` | Every push to master, or by hand | Linux GCC (`test-before-merge` and `test-thread-counts`), Linux Clang, ASan and UBSan, UI tests with OpenGL on Xvfb, Windows MSVC; by hand also the Windows platform checksums |
| `release.yml` | A release is published, or by hand | The Windows zip (started once on a clean runner), the RPM, the AppImage, attached to the release |

They all install their dependencies through `.github/actions/setup/` and build through the presets above. A step that takes
more than one command is a script in `ci/`, named for what it does, so the workflows read as the steps they take and each script
can be run on its own. pr.yml and ci.yml are templates until the repository has a remote: their headers say what to do then.

## Versions and releases

A release takes its version from its GitHub tag. Publish a release on GitHub under a tag `vX.Y.Z` (or `X.Y.Z`), for example
`v1.2.0`. release.yml passes the tag to every job as `DOODLEBOLD_RELEASE_VERSION`, and `cmake/Version.cmake` makes it the
build's version: About shows "Version 1.2.0 (abc1234)", with the commit it was built from, and the zip, RPM and AppImage are
named for it. Nothing in the repository needs changing for a release. A tag in any other form, such as `v1.2.0-beta` (an RPM
version can't hold a `-`), stops every job at configure with a message, before anything is packaged. Once the packages are
attached, release.yml writes the release's version to `VERSION` and commits it to the default branch (`ci/record-release-version.sh`;
only ever forward, so a fix released for an older version leaves it alone).

Any other build names the version in `VERSION`, the last release, with `-dev` after it, for example "Version 1.2.0-dev": work
after 1.2.0, not released. No build asks git anything. The commit comes from GitHub's `GITHUB_SHA`, so builds on GitHub show
it, for example "Version 1.2.0 (abc1234)", and a local build shows the version alone. The version and commit also go into bug
reports (Help > Report a Bug) and the tablet diagnostics report.

To build a release by hand: `cmake --preset linux-release -DDOODLEBOLD_RELEASE_VERSION=v1.2.0`. The version is set at the
first configure of a build folder; to change it, pass `-DDOODLEBOLD_RELEASE_VERSION=` again (empty for a `-dev` build).

## Other tools

| Tool | What it does |
|---|---|
| `compare_brush_speed.py` | Measures how much faster or slower this checkout paints than another commit, which it builds itself with the benches on. Linux only: CPU counters through `perf` (`perf_event_paranoid` at 2 or lower); `--time` needs `taskset`, `--screen` needs `xvfb-run` and Mesa. `--help` for its options. Speed is judged on 2 and 4 cores and software OpenGL, as typical machines have, never on one fast machine alone. |
| `generate_brush_pictures.py` | Draws the built-in brush pictures in `resources/brushes/` again. The output is the same every time. |
| `lint_ui_theme_tokens.py` | Flags raw sizes, spacings, radii, fonts and colours in the UI code: they come from `src/ui/Theme.h` or the shared components. Exceptions, each with its reason, are in `ui_theme_tokens_allowlist.txt`. |
| `windows-start-and-quit.ps1` | Starts `doodlebold.exe`, closes it, and fails if it crashes, hangs or quits with an unexpected code. |
| `sanitizer-suppressions/` | The leaks and races that belong to Qt, Mesa or the system, which the sanitizer presets pass on. |

## Adding a tool

- Start the file with what it does and how to run it, and add it to this page.
- Name it for what it acts on and what it does: `compare_brush_speed.py`, not `speed.py`.
- Don't write absolute paths or anything specific to one computer.
- A check that every change should pass is a CTest test with a label, not a script.

## Testing the Python tools

```bash
python3 -m unittest discover -s tools/tests
```

They build nothing and take a few seconds, and `test-before-commit` runs them too. One of them protects the brush pictures in
`resources/brushes/`, because only three of those pictures are painted by a brush-pixel case. If you did mean to change a picture,
redraw them, save new checksums and say so in your commit:

```bash
tools/generate_brush_pictures.py
python3 tools/tests/test_brush_pictures.py --write
```
