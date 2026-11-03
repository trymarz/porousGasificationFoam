#!/usr/bin/env python3
"""Focused tests for the regression invariant checker."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))
import checkInvariants  # noqa: E402


class CheckInvariantsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.post = self.root / "postProcessing"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def write(self, relative: str, contents: str) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
        return path

    def run_cli(self, invariants: str) -> subprocess.CompletedProcess:
        path = self.write("system/regressionInvariants", invariants)
        return subprocess.run(
            [sys.executable, str(TOOLS_DIR / "checkInvariants.py"),
             "--invariants", str(path), "--postprocessing", str(self.post)],
            capture_output=True, text=True,
        )

    def test_each_check_holds_and_breaks(self) -> None:
        cases = [
            ([1.0, 1.0 + 1e-12], "constant", [1e-9], True),
            ([1.0, 1.1], "constant", [1e-9], False),
            ([0.0, -1e-13], "zero", [1e-12], True),
            ([0.0, 1e-6], "zero", [1e-12], False),
            ([0.5, 1.0], "<=", [1.0], True),
            ([0.5, 1.0 + 1e-6], "<=", [1.0], False),
            ([1.0 + 1e-14], "<=", [1.0, 1e-12], True),
            ([0.3, 0.4], ">=", [0.3], True),
            ([0.29], ">=", [0.3], False),
            ([300.0, 300.0 + 1e-7], "==", [300.0, 1e-6], True),
            ([299.0], "==", [300.0, 1e-6], False),
        ]
        for values, check, args, holds in cases:
            with self.subTest(check=check, values=values):
                self.assertEqual(not checkInvariants.violations(values, check, args), holds)

    def test_nan_breaks_every_check(self) -> None:
        nan = float("nan")
        for check, args in [("zero", [1.0]), ("<=", [1.0]), (">=", [0.0]), ("==", [0.0, 1.0])]:
            with self.subTest(check=check):
                self.assertTrue(checkInvariants.violations([0.0, nan], check, args))

    def test_cli_reads_named_column_across_start_times(self) -> None:
        self.write("postProcessing/fo/0/volFieldValue.dat",
                   "# Time\tmin(a)\tmax(a)\n0.1\t0.2\t0.9\n")
        self.write("postProcessing/fo/0.1/volFieldValue.dat",
                   "# Time\tmin(a)\tmax(a)\n0.2\t0.3\t1.5\n")
        result = self.run_cli("fo max(a) <= 1  # bound\nfo min(a) >= 0\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("FAIL  fo max(a) <= 1", result.stdout)
        self.assertIn("OK    fo min(a) >= 0", result.stdout)
        self.assertIn("1/2 invariants hold", result.stdout)

    def test_cli_passes_when_all_hold(self) -> None:
        self.write("postProcessing/fo/0/volFieldValue.dat", "# Time\tx\n0.1\t0\n0.2\t0\n")
        result = self.run_cli("fo x zero 0\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_column_is_an_infrastructure_error(self) -> None:
        self.write("postProcessing/fo/0/volFieldValue.dat", "# Time\tx\n0.1\t0\n")
        result = self.run_cli("fo y zero 0\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("no column y", result.stderr)

    def test_missing_function_object_is_an_infrastructure_error(self) -> None:
        result = self.run_cli("absent x zero 0\n")
        self.assertEqual(result.returncode, 2)

    def test_unknown_check_and_wrong_arity_are_rejected(self) -> None:
        for text in ["fo x between 0 1\n", "fo x == 300\n", "fo x\n", "# only a comment\n"]:
            with self.subTest(text=text):
                self.assertEqual(self.run_cli(text).returncode, 2)

    def test_vector_column_is_rejected(self) -> None:
        self.write("postProcessing/fo/0/volFieldValue.dat", "# Time\tvolAverage(U)\n0.1\t(1 2 3)\n")
        result = self.run_cli("fo volAverage(U) <= 10\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("not a scalar column", result.stderr)


if __name__ == "__main__":
    unittest.main()
