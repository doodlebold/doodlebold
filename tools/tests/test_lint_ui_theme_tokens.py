"""Tests tools/lint_ui_theme_tokens.py: the UI code is clean, and the check still catches what it is for.

python3 -m unittest discover -s tools/tests (the tool-scripts tests run it).
"""

import importlib.util
import os
import subprocess
import sys
import unittest

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(TOOLS, "lint_ui_theme_tokens.py")
spec = importlib.util.spec_from_file_location("lint_ui_theme_tokens", SCRIPT)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class CheckUiTokens(unittest.TestCase):
    def test_the_ui_code_is_clean(self):
        run = subprocess.run([sys.executable, "-I", SCRIPT], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def flags(self, line):
        return [what for _, what in check.check_cpp("x.cpp", line)]

    def test_raw_values_are_flagged(self):
        self.assertTrue(self.flags("layout->setContentsMargins(12, 8, 12, 8);"))
        self.assertTrue(self.flags("row->setSpacing(6);"))
        self.assertTrue(self.flags("b->setFixedSize(24, 24);"))
        self.assertTrue(self.flags("p.drawRoundedRect(r, 10, 10);"))
        self.assertTrue(self.flags("p.setPen(QPen(ink, 2));"))
        self.assertTrue(self.flags("const QColor c(0x25, 0x63, 0xeb); QColor d = QColor(37, 99, 235);"))
        self.assertTrue(self.flags('label->setStyleSheet(QStringLiteral("color: #9b9ba5;"));'))
        self.assertTrue(self.flags("f.setBold(true);"))
        self.assertTrue(self.flags("constexpr int kButtonSize = 18;"))

    def test_tokens_and_marked_metrics_pass(self):
        self.assertFalse(self.flags("layout->setContentsMargins(0, 0, 0, 0);"))
        self.assertFalse(self.flags("row->setSpacing(Theme::kSpace2);"))
        self.assertFalse(self.flags("b->setFixedSize(Theme::compact::kControlMd, Theme::compact::kControlMd);"))
        self.assertFalse(self.flags("p.drawRoundedRect(r, height() / 2.0, height() / 2.0);"))
        self.assertFalse(self.flags("constexpr int kRowHeight = 44;  // component metric: a layer row"))
        self.assertFalse(self.flags("constexpr int kMaxRows = 3;"), "not a visual constant")

    def test_the_style_sheet_takes_placeholders(self):
        self.assertTrue(check.check_qss("x.qss", "QMenu { padding: 4px; }"))
        self.assertTrue(check.check_qss("x.qss", "QLabel { color: #0b0c0e; }"))
        self.assertFalse(check.check_qss("x.qss", "QMenu { padding: @space1; color: @Text~200; }"))
        self.assertFalse(check.check_qss("x.qss", "QScrollBar { width: 14px; } /* component metric: its width */"))


if __name__ == "__main__":
    unittest.main()
