#!/usr/bin/env python3
"""Focused tests for the regression scalar/compound-value comparator."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))
import compareScalars  # noqa: E402


class CompareScalarsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def write_dat(self, relative: str, contents: str) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
        return path

    def test_parse_scalar_rows(self) -> None:
        path = self.write_dat(
            "scalar.dat",
            "# Time value\n0.1 2.0\n0.2 3.0\n",
        )

        columns, rows = compareScalars.parse_dat(path)

        self.assertEqual(columns, ["Time", "value"])
        self.assertEqual(rows, [(0.1, 2.0), (0.2, 3.0)])

    def test_parse_vector_rows_as_flat_numeric_values(self) -> None:
        path = self.write_dat(
            "vector.dat",
            "# Time volAverage(U)\n0.1 (1 2 3)\n0.2 (4 5 6)\n",
        )

        columns, rows = compareScalars.parse_dat(path)

        self.assertEqual(columns, ["Time", "volAverage(U)"])
        self.assertEqual(rows, [(0.1, 1.0, 2.0, 3.0), (0.2, 4.0, 5.0, 6.0)])

    def test_malformed_compound_value_is_rejected(self) -> None:
        path = self.write_dat("malformed.dat", "# Time U\n0.1 (1 invalid 3)\n")

        with self.assertRaisesRegex(compareScalars.ComparisonError, "non-numeric"):
            compareScalars.parse_dat(path)

    def test_inconsistent_rows_are_rejected(self) -> None:
        path = self.write_dat("inconsistent.dat", "# Time U\n0.1 1\n0.2 (2 3)\n")

        with self.assertRaisesRegex(compareScalars.ComparisonError, "column count"):
            compareScalars.parse_dat(path)

    def test_empty_file_has_no_comparable_rows(self) -> None:
        reference = self.write_dat("reference/value.dat", "# Time value\n")
        candidate = self.write_dat("candidate/value.dat", "# Time value\n")

        with self.assertRaisesRegex(compareScalars.ComparisonError, "no data rows"):
            compareScalars.compare_file(reference, candidate, 1e-4, 1e-12)

    def test_vector_component_difference_is_a_numeric_failure(self) -> None:
        reference = self.write_dat(
            "reference/value.dat",
            "# Time volAverage(U)\n0.1 (1 2 3)\n",
        )
        candidate = self.write_dat(
            "candidate/value.dat",
            "# Time volAverage(U)\n0.1 (1 4 3)\n",
        )

        failures = compareScalars.compare_file(reference, candidate, 1e-4, 1e-12)

        self.assertEqual(len(failures), 1)
        self.assertIn("volAverage(U)[1]", failures[0])

    def test_row_count_mismatch_is_an_infrastructure_error(self) -> None:
        reference = self.write_dat("reference/value.dat", "# Time value\n0.1 1\n")
        candidate = self.write_dat(
            "candidate/value.dat", "# Time value\n0.1 1\n0.2 2\n"
        )

        with self.assertRaisesRegex(compareScalars.ComparisonError, "row count"):
            compareScalars.compare_file(reference, candidate, 1e-4, 1e-12)

    def test_expanded_shape_mismatch_is_an_infrastructure_error(self) -> None:
        reference = self.write_dat(
            "reference/value.dat", "# Time value\n0.1 (1 2 3)\n"
        )
        candidate = self.write_dat("candidate/value.dat", "# Time value\n0.1 (1 2)\n")

        with self.assertRaisesRegex(compareScalars.ComparisonError, "column count"):
            compareScalars.compare_file(reference, candidate, 1e-4, 1e-12)

    def test_missing_candidate_file_returns_infrastructure_exit_code(self) -> None:
        reference_root = self.root / "reference"
        candidate_root = self.root / "candidate"
        self.write_dat("reference/value.dat", "# Time value\n0.1 1\n")
        candidate_root.mkdir()

        result = subprocess.run(
            [
                sys.executable,
                str(TOOLS_DIR / "compareScalars.py"),
                "--reference",
                str(reference_root),
                "--candidate",
                str(candidate_root),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("no candidate file", result.stderr)


if __name__ == "__main__":
    unittest.main()
