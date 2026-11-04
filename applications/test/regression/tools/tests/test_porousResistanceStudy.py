#!/usr/bin/env python3
"""Focused tests for the Darcy-Forchheimer dp expectation (#179)."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))
import checkInvariants  # noqa: E402
import porousResistanceStudy as study  # noqa: E402


class ExpectationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.case = Path(self.temp_dir.name) / "case"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def write_run(self, df: float, forchheimer: float, ratio: float) -> None:
        """Fake a steady run whose dp is `ratio` times the analytic value."""
        mass = 2.8e-06
        expected = study.expected_pressure_drop(1e5, mass / study.AREA, df, forchheimer)
        values = {
            "regressionPInlet": 1e5 + ratio * expected,
            "regressionPOutlet": 1e5,
            "regressionMassInlet": -mass,
            "regressionMassOutlet": mass,
            "regressionUAverage": 0.1,
        }
        for name, value in values.items():
            path = self.case / "postProcessing" / name / "0" / "x.dat"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"# Time\tvalue\n0.05\t0\n0.1\t{value!r}\n")

    def ratio(self, df: float, forchheimer: float) -> float:
        out = study.write_expectation(self.case, df, forchheimer)
        return checkInvariants.column_values(out.parents[2], study.EXPECTATION_FUNCTION, "dpRatio")[-1]

    def test_ratio_is_measured_over_analytic(self) -> None:
        self.write_run(1e8, 1e5, 0.98)
        self.assertAlmostEqual(self.ratio(1e8, 1e5), 0.98, places=9)

    def test_pinned_resistance_differing_from_the_run_moves_the_ratio(self) -> None:
        self.write_run(1e8, 0.0, 1.0)
        self.assertGreater(abs(self.ratio(2e8, 0.0) - 1.0), 0.4)
        self.assertGreater(abs(self.ratio(1e8, 1e5) - 1.0), 0.4)

    def test_cli_needs_both_resistances(self) -> None:
        done = subprocess.run(
            [sys.executable, str(TOOLS_DIR / "porousResistanceStudy.py"), "--expect", str(self.case), "--df", "1e8"],
            capture_output=True, text=True,
        )
        self.assertEqual(done.returncode, 2)
        self.assertIn("--forchheimer", done.stderr)

    def test_study_copy_pins_its_own_spec(self) -> None:
        shutil.copytree(study.DEFAULT_TEMPLATE / "0.orig", self.case / "0.orig")
        shutil.copytree(study.DEFAULT_TEMPLATE / "constant", self.case / "constant")
        shutil.copy(study.DEFAULT_TEMPLATE / "Allrun", self.case / "Allrun")
        study.configure_case(self.case, study.StudySpec("x", 1e9, 2e5, 0.2))
        allrun = (self.case / "Allrun").read_text()
        self.assertIn(f"python3 {TOOLS_DIR / 'porousResistanceStudy.py'} \\\n", allrun)
        self.assertIn("--expect . --df 1e+09 --forchheimer 200000 ", allrun)


if __name__ == "__main__":
    unittest.main()
