"""How tools/compare_brush_speed.py reads results and picks what to compare. Nothing is built, and no program is started."""
import argparse
import contextlib
import io
import subprocess
import sys
import unittest
from collections.abc import Iterator
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_brush_speed as speed  # noqa: E402

TESTS = Path("tests")
NAME = speed.case_name("brush", 200)
TEXTURED = speed.case_name("brush", 200, "-paper-grain")


def finished(stdout: str = "", returncode: int = 0) -> SimpleNamespace:
    return SimpleNamespace(stdout=stdout, returncode=returncode)


@contextlib.contextmanager
def quiet_already() -> Iterator[float]:
    yield 0.0


@contextlib.contextmanager
def printing(out: str) -> Iterator[None]:
    """The programs print `out`, on a machine that is quiet and has no memory caps."""
    with mock.patch.object(speed, "quiet_machine", quiet_already), mock.patch.object(speed, "output", return_value=out):
        with mock.patch.object(speed, "capped", lambda _, command: command):
            yield


class WorkTest(unittest.TestCase):
    def work(self, stdout: str, returncode: int = 0) -> dict[str, speed.Work]:
        with mock.patch.object(speed, "capped", lambda _, command: command), \
                mock.patch.object(subprocess, "run", return_value=finished(stdout, returncode)):
            return speed.work(TESTS, "brush", 200)

    def test_reads_each_case(self) -> None:
        found = self.work(f"12.0 ms  {NAME}\nwork: 100 200 {NAME}\nwork: 300 400 {TEXTURED}\n12.0 ms  in all\n")
        self.assertEqual(found, {NAME: speed.Work(100, 200), TEXTURED: speed.Work(300, 400)})

    def test_no_cases_names_the_sizes(self) -> None:
        with self.assertRaisesRegex(SystemExit, "no results for brush at 200 px"):
            self.work("0.0 ms  in all\n")

    def test_counters_reading_zero(self) -> None:
        with self.assertRaisesRegex(SystemExit, "no CPU counters"):
            self.work(f"work: 0 0 {NAME}\n")

    def test_a_failed_run_stops(self) -> None:
        with self.assertRaisesRegex(SystemExit, "failed"):
            self.work("", returncode=1)


class SecondsTest(unittest.TestCase):
    def seconds(self, out: str) -> dict[str, float]:
        with printing(out):
            return speed.seconds(TESTS, "brush", 200, 2)

    def test_reads_each_case(self) -> None:
        self.assertEqual(self.seconds(f"  1500.0 ms  {NAME}\n   250.0 ms  {TEXTURED}\n  1750.0 ms  in all\n"),
                         {NAME: 1.5, TEXTURED: 0.25})

    def test_a_missing_case_stops(self) -> None:
        with self.assertRaisesRegex(SystemExit, "no results"):
            self.seconds(f"  1500.0 ms  {NAME}\n")


class BehindThePenTest(unittest.TestCase):
    def behind(self, out: str) -> list[int]:
        with printing(out):
            return speed.behind_the_pen(TESTS, "brush", [50, 200], 2)

    def test_one_result_a_size(self) -> None:
        line = "behind the pen at the lift: {} ms, of a stroke of 900 ms\n"
        self.assertEqual(self.behind(line.format(40) + line.format(70)), [40, 70])

    def test_a_missing_result_stops(self) -> None:
        with self.assertRaisesRegex(SystemExit, "no result for brush"):
            self.behind("behind the pen at the lift: 40 ms\n")


class CompareTimeTest(unittest.TestCase):
    def test_neither_side_always_goes_first(self) -> None:
        order: list[Path] = []

        def seconds(tests: Path, *_: object) -> dict[str, float]:
            order.append(tests)
            return {NAME: 1.0 + len(order), TEXTURED: 1.0 + len(order)}

        base, here = Path("base"), Path("here")
        with mock.patch.object(speed, "seconds", seconds), contextlib.redirect_stdout(io.StringIO()) as printed:
            speed.compare_time(base, here, ["brush"], [200], [2])
        self.assertEqual(order, [base, here, here, base])
        self.assertIn("2.00 ->    3.00", printed.getvalue())  # the lower of each side's two


class DefaultBaseTest(unittest.TestCase):
    def base(self, merge_base: str, tracked_changes: str = "") -> str:
        answers = {("merge-base", "HEAD", "master"): merge_base, ("rev-parse", "HEAD"): "head",
                   ("rev-parse", "HEAD~1"): "before",
                   ("status", "--porcelain", "--untracked-files=no"): tracked_changes}
        with mock.patch.object(speed, "git", lambda *args: answers[args]):
            return speed.default_base()

    def test_on_a_branch_where_it_started(self) -> None:
        self.assertEqual(self.base("fork"), "fork")

    def test_on_clean_master_the_commit_before(self) -> None:
        self.assertEqual(self.base("head"), "before")

    def test_with_uncommitted_changes_this_commit(self) -> None:
        self.assertEqual(self.base("head", " M src/core/brush/x.cpp"), "head")


class RunsTest(unittest.TestCase):
    def side_by_side(self, sizes: list[int], cores: int) -> int:
        with mock.patch("os.sched_getaffinity", return_value=set(range(cores))):
            return speed.runs_side_by_side(sizes)

    def test_follows_sizes_and_cores(self) -> None:
        self.assertEqual(self.side_by_side([200, 1500], 16), 8)
        self.assertEqual(self.side_by_side([200, 4000], 16), 2)
        self.assertEqual(self.side_by_side([200, 1500], 4), 4)

    def test_memory_limits_where_the_system_can(self) -> None:
        with mock.patch.object(speed, "has_systemd", lambda: False):
            self.assertEqual(speed.capped("4G", ["run"]), ["run"])
        with mock.patch.object(speed, "has_systemd", lambda: True):
            command = speed.capped("4G", ["run"])
        self.assertEqual((command[0], command[-1]), ("systemd-run", "run"))
        self.assertIn("MemoryMax=4G", command)
        self.assertIn("MemorySwapMax=2G", command)


class OptionsTest(unittest.TestCase):
    def options(self, *arguments: str) -> argparse.Namespace:
        with mock.patch.object(sys, "argv", ["compare_brush_speed.py", *arguments]), contextlib.redirect_stderr(io.StringIO()):
            return speed.parse_options()

    def test_defaults(self) -> None:
        options = self.options()
        self.assertEqual((options.tools, options.sizes, options.cores), (speed.TOOLS, [200, 1500], [2, 4]))

    def test_screen_defaults(self) -> None:
        options = self.options("--screen")
        self.assertEqual((options.tools, options.sizes), (speed.SCREEN_TOOLS, [50, 200, 600]))

    def test_refused(self) -> None:
        for arguments in (["--tools", "brsh"], ["--screen", "--tools", "clone"], ["--sizes", "abc"],
                          ["--cores", "1,,2"], ["--time", "--screen"]):
            with self.subTest(arguments=arguments), self.assertRaises(SystemExit) as stopped:
                self.options(*arguments)
            self.assertEqual(stopped.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
