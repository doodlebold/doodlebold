#!/usr/bin/env python3
"""Compares how fast this checkout paints with another commit, both built as Release.

  tools/compare_brush_speed.py             CPU cycles each tool's strokes take on one thread: quick, and it needs no quiet machine
  tools/compare_brush_speed.py --time      real time on 2 and 4 cores, on a quiet machine: for threads, memory and final figures
  tools/compare_brush_speed.py --screen    how far behind the pen the program is on screen, with software graphics
  --base REF     what to compare with: unless given, the commit this branch started from, or on master the one before
  --tools a,b    which tools; --sizes 200,1500 which sizes; --cores 2,4 which core counts

tools/README.md explains the modes.
"""
import argparse
import fcntl
import functools
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent
CACHE = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "doodlebold-speed"
LOCK = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp")) / "doodlebold-build.lock"
KEPT_BASES = 3  # built commits kept in CACHE
TOOLS = "pencil brush watercolor marker eraser blur smudge pixelart clone".split()
SCREEN_TOOLS = "brush watercolor marker blur smudge".split()
SCREEN_ALLOWED = {"pencil", "brush", "watercolor", "marker", "eraser", "blur", "smudge"}
VARIANTS = (("", ""), ("-paper-grain", " (textured)"))
BENCH_NAME = {"watercolor": "water"}
SIZES = "10, 50, 200, 600, 1500, 4000"
QUIET_CORES = 1.5  # a machine with fewer cores busy than this counts as quiet
QUIET_TRIES = 40  # at 3 seconds a try
WORK_LINE = re.compile(r"^work: (\d+) (\d+) (\S+)$", re.M)
SECONDS_LINE = re.compile(r"^\s*([\d.]+) ms\s+(grid/\S+)$", re.M)
BEHIND_LINE = re.compile(r"behind the pen at the lift: (\d+) ms")


class Work(NamedTuple):
    cycles: int
    instructions: int


def output(command: list[str], env: dict[str, str] | None = None, timeout: float | None = None) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True, env=env, timeout=timeout).stdout


def git(*args: str) -> str:
    return output(["git", "-C", str(ROOT), *args]).strip()


def default_base() -> str:
    """The commit this branch started from. On master with nothing uncommitted, the commit before this one."""
    base = git("merge-base", "HEAD", "master")
    if base == git("rev-parse", "HEAD") and not git("status", "--porcelain", "--untracked-files=no"):
        return git("rev-parse", "HEAD~1")
    return base


def build(source: Path, directory: Path) -> Path:
    """A Release build with the benches, of any commit: plain CMake, so commits from before the presets build too."""
    env = dict(os.environ, CCACHE_BASEDIR=str(source), CCACHE_NOHASHDIR="1")
    configure = ["cmake", "-S", str(source), "-B", str(directory), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DDOODLEBOLD_BUILD_BENCHES=ON"]
    subprocess.run(configure, check=True, env=env, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(directory)], check=True, env=env, stdout=subprocess.DEVNULL)
    return directory / "tests"


def build_base(ref: str) -> Path:
    sha = git("rev-parse", ref)
    place = CACHE / sha
    tree = place / "tree"  # laid out as a checkout is, so that ccache reuses this checkout's compiled files
    if not (tree / "CMakeLists.txt").exists():
        tree.mkdir(parents=True, exist_ok=True)
        export = ["git", "-C", str(ROOT), "archive", sha]
        with subprocess.Popen(export, stdout=subprocess.PIPE) as archive:
            subprocess.run(["tar", "-x", "-C", str(tree)], check=True, stdin=archive.stdout)
    tests = build(tree, tree / "build-release")
    place.touch()
    for old in sorted(CACHE.iterdir(), key=lambda p: p.stat().st_mtime)[:-KEPT_BASES]:
        shutil.rmtree(old)
    return tests


@functools.cache
def has_systemd() -> bool:
    """Whether heavy runs can be held in a memory-capped scope."""
    if not shutil.which("systemd-run"):
        return False
    return subprocess.run(["systemd-run", "--user", "--scope", "--quiet", "true"], capture_output=True).returncode == 0


def capped(memory: str, command: list[str]) -> list[str]:
    if not has_systemd():
        return command
    limits = ["-p", f"MemoryMax={memory}", "-p", "MemorySwapMax=2G"]
    return ["systemd-run", "--user", "--scope", "--quiet", *limits, *command]


def pinned(cores: int, command: list[str]) -> list[str]:
    return ["taskset", "-c", f"0-{cores - 1}", *command]


def case_name(tool: str, size: int, variant: str = "") -> str:
    return f"grid/{tool}-{size}px-s4-painted{variant}"


def reference_set_run(tests: Path, tool: str, size: int, cores: int) -> list[str]:
    program = tests / "doodlebold_brush_pixels"
    if not program.exists():
        program = tests / "doodlebold_reference_set"  # its name in commits before the rename
    return [str(program), "--time", "--only", case_name(tool, size), "--cores", str(cores)]


def work(tests: Path, tool: str, size: int) -> dict[str, Work]:
    """Each case's CPU cycles and instructions, for its strokes alone, on one thread."""
    run = subprocess.run(capped("4G", reference_set_run(tests, tool, size, 1)), capture_output=True, text=True)
    if run.returncode:
        sys.exit(f"compare_brush_speed.py: painting {tool} at {size} px failed, most likely out of memory. "
                 "Try fewer tools or smaller sizes.")
    found = {name: Work(int(cycles), int(instructions)) for cycles, instructions, name in WORK_LINE.findall(run.stdout)}
    if not found:
        sys.exit(f"compare_brush_speed.py: no results for {tool} at {size} px. The sizes are {SIZES}. "
                 "A commit from before this tool can only be compared with --time.")
    if not all(cost.cycles for cost in found.values()):
        sys.exit("compare_brush_speed.py: this system gives no CPU counters. "
                 "Set kernel.perf_event_paranoid to 2 or lower, or use --time.")
    return found


def cpu_times() -> tuple[int, int]:
    """All the time the processors have had so far, and how much of it they were idle."""
    fields = [int(x) for x in Path("/proc/stat").read_text().splitlines()[0].split()[1:]]
    return sum(fields), fields[3] + fields[4]


def busy_cores() -> float:
    total0, idle0 = cpu_times()
    time.sleep(2)
    total1, idle1 = cpu_times()
    return (1 - (idle1 - idle0) / max(1, total1 - total0)) * (os.cpu_count() or 1)


@contextmanager
def build_lock() -> Iterator[None]:
    """Holds a lock of its own, so that two comparisons never time against each other."""
    with open(LOCK, "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield


@contextmanager
def quiet_machine() -> Iterator[float]:
    """The build lock, with the machine quiet when it starts, or as near as two minutes allow. Gives the cores busy."""
    with build_lock():
        busy = busy_cores()
        for _ in range(QUIET_TRIES - 1):
            if busy < QUIET_CORES:
                break
            time.sleep(1)
            busy = busy_cores()
        yield busy


def seconds(tests: Path, tool: str, size: int, cores: int) -> dict[str, float]:
    """Each case's seconds, for its strokes alone."""
    with quiet_machine() as busy:
        out = output(capped("6G", pinned(cores, reference_set_run(tests, tool, size, cores))))
    if busy >= QUIET_CORES:
        print(f"  note: {busy:.1f} cores were busy when {tool} {size} px started", file=sys.stderr)
    found = {name: float(ms) / 1000 for ms, name in SECONDS_LINE.findall(out)}
    if not all(case_name(tool, size, variant) in found for variant, _ in VARIANTS):
        sys.exit(f"compare_brush_speed.py: no results for {tool} at {size} px. The sizes are {SIZES}.")
    return found


def screen_environment(home: str, tool: str, cores: int, layer: Path) -> dict[str, str]:
    """Software graphics, a private home folder, and the bench's settings for one stroke played in the whole window."""
    return dict(os.environ, __GLX_VENDOR_LIBRARY_NAME="mesa", QT_QPA_PLATFORM="xcb", LIBGL_ALWAYS_SOFTWARE="1",
                LP_NUM_THREADS=str(cores), HOME=home, XDG_CONFIG_HOME=f"{home}/config", XDG_DATA_HOME=f"{home}/data",
                XDG_CACHE_HOME=f"{home}/cache", DOODLEBOLD_TIMING_LOG=f"{home}/log", BENCH_CORES=str(cores),
                BENCH_TOOL=BENCH_NAME.get(tool, tool), BENCH_MINSIZE="100", BENCH_STROKE="6", BENCH_AS_RECORDED="1",
                BENCH_DEVICE="pen", BENCH_WINDOW="1", BENCH_LAYER=str(layer))


def behind_the_pen(tests: Path, tool: str, sizes: list[int], cores: int) -> list[int]:
    """Milliseconds behind the pen at the lift, for each size, of the stroke played in the whole window."""
    fixtures = ROOT / "tests/fixtures"
    command = pinned(cores, ["xvfb-run", "-a", "-s", "-screen 0 1600x1000x24",
                             str(tests / "doodlebold_stroke_view_bench"), str(fixtures / "recorded-3k-strokes.jsonl"),
                             ",".join(map(str, sizes)), "3000", "3000", str(len(sizes))])
    with tempfile.TemporaryDirectory() as home, quiet_machine():
        env = screen_environment(home, tool, cores, fixtures / "reference/strokes/field-3k.png")
        out = output(capped("6G", command), env=env, timeout=600)
    found = [int(x) for x in BEHIND_LINE.findall(out)]
    if len(found) != len(sizes):
        sys.exit(f"compare_brush_speed.py: the screen test gave no result for {tool}. It covers {', '.join(sorted(SCREEN_ALLOWED))}.")
    return found


def percent(before: float, after: float) -> str:
    return f"{100 * (after - before) / before:+.1f} %"


def runs_side_by_side(sizes: list[int]) -> int:
    # Measured: up to 0.7 GB a run up to 1500 px, and up to 2.6 GB at 4000, so that these stay near 6 GB together.
    return min(2 if max(sizes) > 1500 else 8, len(os.sched_getaffinity(0)))


def cycle_change(before: Work, after: Work) -> str:
    return f"{percent(before.cycles, after.cycles)} ({percent(before.instructions, after.instructions)})"


def compare_cycles(base: Path, here: Path, tools: list[str], sizes: list[int]) -> None:
    jobs = [(tests, tool, size) for tool in tools for size in sizes for tests in (base, here)]
    with build_lock(), ThreadPoolExecutor(runs_side_by_side(sizes)) as pool:
        counts = dict(zip(jobs, pool.map(lambda job: work(*job), jobs)))
    print("Change in CPU cycles on one thread, against the base (change in instructions in brackets)")
    print(f"{'':24}" + "".join(f"{size:>20} px" for size in sizes))
    for tool in tools:
        for variant, label in VARIANTS:
            cells = [cycle_change(counts[base, tool, size][case_name(tool, size, variant)],
                                  counts[here, tool, size][case_name(tool, size, variant)]) for size in sizes]
            print(f"{tool + label:24}" + "".join(f"{cell:>23}" for cell in cells))
    print("This can't show how work spreads over threads, or the screen: use --time or --screen for those.")


def lower_of_two(base: Path, here: Path, tool: str, size: int, cores: int) -> tuple[dict[str, float], dict[str, float]]:
    """Each side's seconds, measured twice in opposite orders so that neither side always goes first."""
    first_base, first_here = seconds(base, tool, size, cores), seconds(here, tool, size, cores)
    second_here, second_base = seconds(here, tool, size, cores), seconds(base, tool, size, cores)
    return ({name: min(first_base[name], second_base[name]) for name in first_base},
            {name: min(first_here[name], second_here[name]) for name in first_here})


def compare_time(base: Path, here: Path, tools: list[str], sizes: list[int], core_counts: list[int]) -> None:
    for cores in core_counts:
        print(f"Seconds on {cores} cores, base -> this checkout")
        for tool in tools:
            for size in sizes:
                before, after = lower_of_two(base, here, tool, size, cores)
                for variant, label in VARIANTS:
                    name = case_name(tool, size, variant)
                    change = f"{before[name]:7.2f} -> {after[name]:7.2f}  ({percent(before[name], after[name])})"
                    print(f"  {tool + label:24}{size:>6} px  {change}")


def compare_screen(base: Path, here: Path, tools: list[str], sizes: list[int], core_counts: list[int]) -> None:
    for cores in core_counts:
        print(f"ms behind the pen at the lift on {cores} cores, software graphics, base -> this checkout")
        for tool in tools:
            before, after = behind_the_pen(base, tool, sizes, cores), behind_the_pen(here, tool, sizes, cores)
            print(f"  {tool:12}" + "   ".join(f"{s} px {b} -> {a}" for s, b, a in zip(sizes, before, after)))


def numbers(text: str) -> list[int]:
    try:
        return [int(part) for part in text.split(",")]
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected numbers separated by commas, got {text!r}") from None


def parse_options() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", default=None, help="the commit, branch or tag to compare with")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--time", action="store_true", help="real seconds, on a quiet machine")
    mode.add_argument("--screen", action="store_true", help="how far behind the pen the program is, on a quiet machine")
    parser.add_argument("--tools", default=None, help="tools to measure, separated by commas (default: all)")
    parser.add_argument("--sizes", type=numbers, default=None,
                        help="brush sizes in px (default: 200,1500; 50,200,600 with --screen)")
    parser.add_argument("--cores", type=numbers, default=[2, 4],
                        help="core counts for --time and --screen (default: 2,4)")
    options = parser.parse_args()
    options.tools = options.tools.split(",") if options.tools else (SCREEN_TOOLS if options.screen else TOOLS)
    allowed = SCREEN_ALLOWED if options.screen else set(TOOLS)
    if unknown := [tool for tool in options.tools if tool not in allowed]:
        parser.error(f"unknown tool {', '.join(unknown)}: choose from {', '.join(sorted(allowed))}")
    options.sizes = options.sizes or ([50, 200, 600] if options.screen else [200, 1500])
    return options


def main() -> None:
    options = parse_options()
    base_ref = options.base or default_base()
    print(f"Comparing with {git('rev-parse', '--short', base_ref)}")
    base, here = build_base(base_ref), build(ROOT, ROOT / "build-release")
    if options.screen:
        compare_screen(base, here, options.tools, options.sizes, options.cores)
    elif options.time:
        compare_time(base, here, options.tools, options.sizes, options.cores)
    else:
        compare_cycles(base, here, options.tools, options.sizes)


if __name__ == "__main__":
    main()
