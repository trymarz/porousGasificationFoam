#!/usr/bin/env python3
"""Focused tests for the regression run-state serialisation helper."""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))
import regressionState  # noqa: E402


class RegressionStateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def run_main(self, *args: str) -> str:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(regressionState.main(list(args)), 0)
        return output.getvalue()

    def test_classify_covers_the_shared_exit_code_contract(self) -> None:
        self.assertEqual(regressionState.classify(0), "PASS")
        self.assertEqual(regressionState.classify(1), "FAIL")
        self.assertEqual(regressionState.classify(2), "ERROR")
        self.assertEqual(regressionState.classify(124), "TIMEOUT")
        self.assertEqual(regressionState.classify(134), "CRASH")
        self.assertEqual(regressionState.classify(3), "ERROR")

    def test_signal_name_uses_platform_names_with_fallback(self) -> None:
        self.assertEqual(regressionState.signal_name(6), "SIGABRT")
        self.assertEqual(regressionState.signal_name(999), "SIG999")

    def test_snapshot_writes_typed_fields_and_replaces_atomically(self) -> None:
        path = self.root / "state.json"

        self.run_main(
            "snapshot",
            "--path",
            str(path),
            "--field",
            "phase=run",
            "--int",
            "attempt=2",
            "--num",
            "fraction=0.25",
            "--bool",
            "active=true",
            "--json",
            'details={"rank": 1}',
            "--artifact",
            "stdout=/tmp/stdout.log",
        )
        first = json.loads(path.read_text())
        self.assertEqual(first["record"], "snapshot")
        self.assertEqual(first["phase"], "run")
        self.assertEqual(first["attempt"], 2)
        self.assertEqual(first["fraction"], 0.25)
        self.assertIs(first["active"], True)
        self.assertEqual(first["details"], {"rank": 1})
        self.assertEqual(first["artifacts"]["stdout"], "/tmp/stdout.log")

        self.run_main("snapshot", "--path", str(path), "--field", "phase=compare")
        second = json.loads(path.read_text())
        self.assertEqual(second["phase"], "compare")
        self.assertEqual(list(path.parent.glob(".regstate-*.tmp")), [])

    def test_result_adds_status_signal_and_elapsed_metadata(self) -> None:
        path = self.root / "result.json"

        self.run_main(
            "result",
            "--path",
            str(path),
            "--exit-code",
            "134",
            "--case-id",
            "tutorials/cases/example",
            "--suite-id",
            "regression",
            "--phase",
            "run",
            "--started-epoch",
            "100",
            "--ended-epoch",
            "103.5",
        )
        result = json.loads(path.read_text())

        self.assertEqual(result["status"], "CRASH")
        self.assertEqual(result["exit_code"], 134)
        self.assertEqual(result["signal"], 6)
        self.assertEqual(result["signal_name"], "SIGABRT")
        self.assertEqual(result["elapsed_s"], 3.5)
        self.assertEqual(result["case_id"], "tutorials/cases/example")

    def test_event_stream_is_self_describing_and_valid_ndjson(self) -> None:
        path = self.root / "events.ndjson"

        self.run_main(
            "event",
            "--type",
            "case_started",
            "--stream",
            str(path),
            "--field",
            "case_id=example",
        )
        self.run_main(
            "event",
            "--type",
            "case_finished",
            "--stream",
            str(path),
            "--int",
            "exit_code=0",
        )
        records = [json.loads(line) for line in path.read_text().splitlines()]

        self.assertEqual(records[0]["record"], "stream_header")
        self.assertEqual(records[1]["event"], "case_started")
        self.assertEqual(records[1]["case_id"], "example")
        self.assertEqual(records[2]["event"], "case_finished")
        self.assertEqual(records[2]["exit_code"], 0)

    def test_event_values_are_truncated_without_breaking_json(self) -> None:
        path = self.root / "events.ndjson"
        value = "x" * (regressionState._MAX_VALUE_CHARS + 25)

        self.run_main(
            "event",
            "--type",
            "diagnostic",
            "--stream",
            str(path),
            "--field",
            f"message={value}",
        )
        event = json.loads(path.read_text().splitlines()[1])

        self.assertLessEqual(len(event["message"]), regressionState._MAX_VALUE_CHARS + 40)
        self.assertIn("[truncated,", event["message"])

    def test_malformed_typed_field_is_rejected(self) -> None:
        with self.assertRaises(SystemExit):
            regressionState.main(
                [
                    "snapshot",
                    "--path",
                    str(self.root / "state.json"),
                    "--int",
                    "attempt=not-an-integer",
                ]
            )

    def test_listing_preserves_case_ids_and_tags(self) -> None:
        output = self.run_main(
            "listing",
            "--suite",
            "regression",
            "--driver",
            "Allrun",
            "--project-root",
            "/repo",
            "--runner",
            "runCase.sh",
            "--case-id",
            "tutorials/cases/a/shared",
            "--case-id",
            "tutorials/cases/b/shared",
            "--case-tag",
            "fast",
            "--case-tag",
            "convergence",
        )
        listing = json.loads(output)

        self.assertEqual([case["id"] for case in listing["cases"]], [
            "tutorials/cases/a/shared",
            "tutorials/cases/b/shared",
        ])
        self.assertEqual([case["name"] for case in listing["cases"]], ["shared", "shared"])
        self.assertEqual(listing["cases"][1]["tag"], "convergence")
        self.assertEqual(listing["cases"][0]["path"], "/repo/tutorials/cases/a/shared")

    def test_ragged_case_metadata_is_rejected(self) -> None:
        with self.assertRaises(SystemExit):
            regressionState._zip_cases(["a", "b"], ["fast"], None)

    def test_suite_result_synthesises_missing_case_and_flags_mismatch(self) -> None:
        cases_root = self.root / "cases"
        mismatch_dir = cases_root / "mismatch"
        mismatch_dir.mkdir(parents=True)
        (mismatch_dir / "result.json").write_text(
            json.dumps({
                "schema_version": 1,
                "record": "result",
                "status": "PASS",
                "exit_code": 0,
            })
        )
        output_path = self.root / "suite-result.json"

        self.run_main(
            "suite-result",
            "--path",
            str(output_path),
            "--exit-code",
            "1",
            "--suite",
            "regression",
            "--driver",
            "Allrun",
            "--cases-root",
            str(cases_root),
            "--case-id",
            "mismatch",
            "--case-id",
            "missing",
            "--case-exit-code",
            "1",
            "--case-exit-code",
            "134",
        )
        result = json.loads(output_path.read_text())

        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["n_cases"], 2)
        self.assertEqual(result["counts"], {"FAIL": 1, "CRASH": 1})
        self.assertEqual(result["cases"][0]["status"], "FAIL")
        self.assertEqual(result["cases"][0]["result_mismatch"], {
            "recorded_exit_code": 0,
            "observed_exit_code": 1,
        })
        self.assertEqual(result["cases"][1]["status"], "CRASH")
        self.assertEqual(result["cases"][1]["signal_name"], "SIGABRT")


if __name__ == "__main__":
    unittest.main()
