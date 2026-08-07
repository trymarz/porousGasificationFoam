#!/usr/bin/env python3
"""Structured run-state emitter for the PGF regression runners.

The regression drivers (`Allrun`, `Allrun.yade`) and the per-case runners
(`runCase.sh`, `runDEMCase.sh`) are the authority on what a case did. This
helper is the *serialisation* side of that authority: it turns shell-side facts
into JSON that an external observer (a CI job, a TUI, `foamcli test`) can read
while the suite runs, without any consumer having to parse human log output.

It is deliberately a leaf: standard library only, no project imports, no
knowledge of any consumer. The exit-code contract in `regressionLib.sh` stays
authoritative for shell callers — this JSON is an *additional* representation,
never a replacement.

State directory layout (paths are supplied by the caller, never invented here):

    <state-dir>/
      suite.json            suite-level snapshot (selection, options, progress)
      events.ndjson         append-only suite event stream
      result.json           final suite result
      cases/<case-id>/
        state.json          current per-case snapshot (phase, timing)
        events.ndjson       append-only per-case event stream
        result.json         terminal per-case result
        stdout.log          captured runner stdout
        stderr.log          captured runner stderr

Every record carries `schema_version` and a `record` discriminator, so a
consumer can tell a stream header from an event without positional assumptions:

    {"schema_version":1,"record":"stream_header",...}   first line of a stream
    {"schema_version":1,"record":"event","event":"case_started",...}
    {"schema_version":1,"record":"snapshot",...}        suite.json / state.json
    {"schema_version":1,"record":"result",...}          result.json

Guarantees
----------
* Snapshots and results are written to a temporary file in the destination
  directory and then `os.replace`d, so a reader never sees a partial JSON
  document and a killed writer cannot leave a truncated one behind.
* Event lines are emitted as a single `write(2)` to an `O_APPEND` descriptor.
  On Linux that makes each line atomic with respect to other appenders as long
  as it stays under `PIPE_BUF`, which is why long free-text values are
  truncated (see `_MAX_VALUE_CHARS`) — concurrent case runners share the suite
  stream.
* Values are JSON-encoded, so quotes, newlines, tabs and non-ASCII bytes in
  paths or error messages cannot break the one-object-per-line invariant.

Usage (all subcommands are safe to call when the caller has no state dir — the
shell wrappers simply do not call them):

    regressionState.py header   --stream S [--field k=v ...]
    regressionState.py event    --type NAME --stream S [--stream S2] [fields]
    regressionState.py snapshot --path P [--record NAME] [fields]
    regressionState.py result   --path P --exit-code N [--status S] [fields]
    regressionState.py listing  --suite S --driver D --project-root R \
                                --case-id ID --case-tag TAG ... [--out P]
    regressionState.py suite-result --path P --exit-code N --cases-root R \
                                --case-id ID --case-exit-code N ...

Field-typing options, usable on every subcommand that takes fields:

    --field k=v   string        --int k=v    integer
    --num k=v     float         --bool k=v   true/false/1/0/yes/no
    --json k=v    raw JSON (objects, arrays, null)
    --artifact k=v  string, collected under an "artifacts" object

Exit status: 0 on success, 1 on a write or usage failure. Callers must treat a
failure here as a reporting problem, never as a case outcome — a state write
that fails must not turn a PASS into a FAIL.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
import tempfile
from datetime import datetime

SCHEMA_VERSION = 1

_MAX_VALUE_CHARS = 1500
"""Cap on a single string value, so one event line stays inside PIPE_BUF.

Concurrent case runners append to the same suite stream; atomicity of an
appended line only holds while the line is small. A comparator failure dump or
a stack trace would otherwise blow past that, so long values are truncated with
an explicit marker rather than silently risking an interleaved line.
"""

# Statuses, keyed off the child exit code exactly as tools/regressionLib.sh
# classifies them. Kept in one place here so the JSON status can never drift
# from the shell summary.
PASS = "PASS"
FAIL = "FAIL"
ERROR = "ERROR"
TIMEOUT = "TIMEOUT"
CRASH = "CRASH"


def classify(exit_code: int) -> str:
    """Return the outcome label for a child exit code.

    Mirrors `reg_classify` in `tools/regressionLib.sh`: 0 PASS, 1 FAIL,
    2 ERROR, 124 TIMEOUT, >128 CRASH (signal death), anything else ERROR.
    """
    if exit_code == 0:
        return PASS
    if exit_code == 1:
        return FAIL
    if exit_code == 2:
        return ERROR
    if exit_code == 124:
        return TIMEOUT
    if exit_code > 128:
        return CRASH
    return ERROR


def signal_name(number: int) -> str:
    """Conventional name for a signal number (`6` -> `"SIGABRT"`)."""
    try:
        return signal.Signals(number).name
    except (ValueError, AttributeError):
        return f"SIG{number}"


def _now_iso() -> str:
    """Current local time as an ISO-8601 string with offset, ms resolution."""
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def _iso_from_epoch(epoch: float) -> str:
    """Format a Unix epoch (as produced by `date +%s`) the same way."""
    return datetime.fromtimestamp(epoch).astimezone().isoformat(
        timespec="milliseconds"
    )


def _truncate(text: str) -> str:
    """Clip an over-long string value, marking that it was clipped."""
    if len(text) <= _MAX_VALUE_CHARS:
        return text
    return text[:_MAX_VALUE_CHARS] + f"…[truncated, {len(text)} chars]"


# -- field collection --------------------------------------------------------


def _split(pair: str, kind: str) -> tuple[str, str]:
    """Split a `key=value` option argument, erroring out on a missing `=`."""
    key, sep, value = pair.partition("=")
    if not sep or not key:
        raise SystemExit(f"regressionState.py: malformed --{kind} '{pair}' (want key=value)")
    return key, value


def _parse_bool(value: str) -> bool:
    lowered = value.strip().lower()
    if lowered in ("1", "true", "yes", "on"):
        return True
    if lowered in ("0", "false", "no", "off", ""):
        return False
    raise SystemExit(f"regressionState.py: not a boolean: '{value}'")


def collect_fields(args: argparse.Namespace) -> dict:
    """Build the caller-supplied field mapping from the typed field options.

    Merge order is fixed (`--field`, `--int`, `--num`, `--bool`, `--json`) so
    the emitted key order is deterministic regardless of command-line order.
    """
    fields: dict = {}
    for pair in args.field or []:
        key, value = _split(pair, "field")
        fields[key] = _truncate(value)
    for pair in args.int_ or []:
        key, value = _split(pair, "int")
        try:
            fields[key] = int(value)
        except ValueError:
            raise SystemExit(f"regressionState.py: not an integer: '{value}'")
    for pair in args.num or []:
        key, value = _split(pair, "num")
        try:
            fields[key] = float(value)
        except ValueError:
            raise SystemExit(f"regressionState.py: not a number: '{value}'")
    for pair in args.bool_ or []:
        key, value = _split(pair, "bool")
        fields[key] = _parse_bool(value)
    for pair in args.json_ or []:
        key, value = _split(pair, "json")
        try:
            fields[key] = json.loads(value)
        except ValueError as exc:
            raise SystemExit(f"regressionState.py: not valid JSON for '{key}': {exc}")
    artifacts = {}
    for pair in getattr(args, "artifact", None) or []:
        key, value = _split(pair, "artifact")
        artifacts[key] = _truncate(value)
    if artifacts:
        fields["artifacts"] = artifacts
    return fields


def add_field_options(parser: argparse.ArgumentParser) -> None:
    """Attach the shared typed-field options to a subcommand parser."""
    parser.add_argument("--field", action="append", metavar="K=V",
                        help="string field (repeatable)")
    parser.add_argument("--int", dest="int_", action="append", metavar="K=V",
                        help="integer field (repeatable)")
    parser.add_argument("--num", action="append", metavar="K=V",
                        help="float field (repeatable)")
    parser.add_argument("--bool", dest="bool_", action="append", metavar="K=V",
                        help="boolean field (repeatable)")
    parser.add_argument("--json", dest="json_", action="append", metavar="K=V",
                        help="raw-JSON field (repeatable)")
    parser.add_argument("--artifact", action="append", metavar="K=PATH",
                        help="artifact path, collected under 'artifacts' (repeatable)")


# -- writers -----------------------------------------------------------------


def write_json_atomic(path: str, payload: dict) -> None:
    """Write *payload* to *path* atomically (temp file in the same dir, rename).

    A reader either sees the previous content or the complete new content —
    never a half-written document — which is what makes `result.json` safe to
    poll while the suite is still running.
    """
    directory = os.path.dirname(os.path.abspath(path)) or "."
    os.makedirs(directory, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=directory, prefix=".regstate-", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def append_record(path: str, payload: dict) -> None:
    """Append one JSON object as a single line to the NDJSON stream at *path*.

    Creates a stream header first if the file does not exist yet, so every
    stream is self-describing even when the first writer is a case runner
    rather than the suite driver.
    """
    directory = os.path.dirname(os.path.abspath(path)) or "."
    os.makedirs(directory, exist_ok=True)
    if not os.path.exists(path):
        _append_line(path, _header_record({}))
    _append_line(path, payload)


def _append_line(path: str, payload: dict) -> None:
    """One `write(2)` of one JSON line to an `O_APPEND` descriptor."""
    data = (json.dumps(payload, ensure_ascii=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    try:
        written = os.write(fd, data)
        # A regular file in append mode writes fully in practice; finish the
        # job rather than silently dropping the tail if it ever does not.
        while written < len(data):
            written += os.write(fd, data[written:])
    finally:
        os.close(fd)


def _header_record(fields: dict) -> dict:
    record = {
        "schema_version": SCHEMA_VERSION,
        "record": "stream_header",
        "created_at": _now_iso(),
    }
    record.update(fields)
    return record


# -- subcommands -------------------------------------------------------------


def cmd_header(args: argparse.Namespace) -> int:
    """Write a stream header, unless the stream already exists."""
    fields = collect_fields(args)
    for stream in args.stream:
        if os.path.exists(stream):
            continue
        directory = os.path.dirname(os.path.abspath(stream)) or "."
        os.makedirs(directory, exist_ok=True)
        _append_line(stream, _header_record(fields))
    return 0


def cmd_event(args: argparse.Namespace) -> int:
    """Append one event to each requested stream."""
    record = {
        "schema_version": SCHEMA_VERSION,
        "record": "event",
        "event": args.type,
        "ts": _now_iso(),
    }
    record.update(collect_fields(args))
    for stream in args.stream:
        append_record(stream, record)
    return 0


def cmd_snapshot(args: argparse.Namespace) -> int:
    """Atomically replace a snapshot document."""
    record = {
        "schema_version": SCHEMA_VERSION,
        "record": args.record,
        "updated_at": _now_iso(),
    }
    record.update(collect_fields(args))
    write_json_atomic(args.path, record)
    return 0


def cmd_result(args: argparse.Namespace) -> int:
    """Atomically write a terminal result document.

    The status is derived from the raw exit code by the same rule the shell
    uses, so the two representations cannot disagree. `--status` exists for the
    cases where the driver knows more than the exit code does (a synthesised
    result for a child that died before writing its own).
    """
    exit_code = args.exit_code
    record: dict = {
        "schema_version": SCHEMA_VERSION,
        "record": "result",
        "status": args.status or classify(exit_code),
        "exit_code": exit_code,
    }
    if exit_code > 128:
        number = exit_code - 128
        record["signal"] = number
        record["signal_name"] = signal_name(number)
    if args.suite_id:
        record["suite_id"] = args.suite_id
    if args.case_id:
        record["case_id"] = args.case_id
    if args.started_epoch is not None:
        record["started_at"] = _iso_from_epoch(args.started_epoch)
    if args.ended_epoch is not None:
        record["ended_at"] = _iso_from_epoch(args.ended_epoch)
    if args.started_epoch is not None and args.ended_epoch is not None:
        record["elapsed_s"] = round(args.ended_epoch - args.started_epoch, 3)
    if args.phase:
        record["phase"] = args.phase
    if args.message:
        record["message"] = _truncate(args.message)
    if args.runner:
        record["runner"] = args.runner
    record.update(collect_fields(args))
    write_json_atomic(args.path, record)
    return 0


def _zip_cases(ids: list[str], tags: list[str] | None, codes: list[str] | None) -> list[tuple]:
    """Pair up the parallel per-case option lists, refusing ragged input.

    The drivers pass per-case data as repeated `--case-id` / `--case-tag` /
    `--case-exit-code` options rather than a packed delimiter format, so a case
    path containing any character at all is safe. argparse preserves the order
    within one option, so zipping by index is well defined — but only if the
    lists are the same length, which is worth failing loudly on.
    """
    for name, other in (("--case-tag", tags), ("--case-exit-code", codes)):
        if other is not None and len(other) != len(ids):
            raise SystemExit(
                f"regressionState.py: {len(ids)} --case-id but {len(other)} {name}"
            )
    return list(
        zip(
            ids,
            tags if tags is not None else [""] * len(ids),
            codes if codes is not None else [""] * len(ids),
        )
    )


def cmd_listing(args: argparse.Namespace) -> int:
    """Emit the machine-readable case manifest for a suite selection.

    This is the contract an external observer reads *before* a run to learn
    which cases the driver selected, without reimplementing the driver's tag
    and `--case` filter rules. `id` is the case-list relative path (not the
    basename) so duplicate basenames cannot collide.
    """
    cases = []
    for case_id, tag, _ in _zip_cases(args.case_id or [], args.case_tag, None):
        entry = {
            "id": case_id,
            "name": os.path.basename(case_id.rstrip("/")),
            "path": os.path.join(args.project_root, case_id)
            if args.project_root
            else case_id,
            "runner": args.runner,
        }
        if tag:
            entry["tag"] = tag
        cases.append(entry)

    document = {
        "schema_version": SCHEMA_VERSION,
        "record": args.record,
        "suite": args.suite,
        "driver": args.driver,
        "project_root": args.project_root,
        "cases": cases,
    }
    document.update(collect_fields(args))

    if args.out:
        write_json_atomic(args.out, document)
    else:
        json.dump(document, sys.stdout, indent=2)
        sys.stdout.write("\n")
    return 0


def cmd_suite_result(args: argparse.Namespace) -> int:
    """Write the final suite result, folding in each case's own result file.

    Each case's terminal result is written by the case runner, which is the
    authority on what that case did. This command aggregates them in the order
    the driver selected, and — crucially — does not treat an absent case result
    as a pass: when a runner died before writing one, the driver's observed wait
    status is used to synthesise `ERROR`/`TIMEOUT`/`CRASH` instead.
    """
    cases = []
    counts: dict[str, int] = {}
    for case_id, _, observed in _zip_cases(
        args.case_id or [], None, args.case_exit_code
    ):
        entry = _case_result_entry(args.cases_root, case_id, observed)
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
        cases.append(entry)

    document = {
        "schema_version": SCHEMA_VERSION,
        "record": "result",
        "suite": args.suite,
        "driver": args.driver,
        "status": args.status or classify(args.exit_code),
        "exit_code": args.exit_code,
    }
    if args.started_epoch is not None:
        document["started_at"] = _iso_from_epoch(args.started_epoch)
    if args.ended_epoch is not None:
        document["ended_at"] = _iso_from_epoch(args.ended_epoch)
    if args.started_epoch is not None and args.ended_epoch is not None:
        document["elapsed_s"] = round(args.ended_epoch - args.started_epoch, 3)
    document["n_cases"] = len(cases)
    document["counts"] = counts
    document["cases"] = cases
    document.update(collect_fields(args))
    write_json_atomic(args.path, document)
    return 0


def _case_result_entry(cases_root: str, case_id: str, observed: str) -> dict:
    """Build one aggregated case entry for the suite result.

    Prefers the case's own `result.json`. Falls back to the driver's observed
    exit code, and flags a disagreement rather than quietly trusting the file —
    a stale result left over from an earlier run must not be able to turn a
    crashed case green.
    """
    observed_code: int | None
    try:
        observed_code = int(observed)
    except (TypeError, ValueError):
        observed_code = None

    path = os.path.join(cases_root, case_id, "result.json") if cases_root else ""
    payload = None
    if path and os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as handle:
                payload = json.load(handle)
        except (OSError, ValueError):
            payload = None

    if payload is None:
        entry = {
            "id": case_id,
            "status": classify(observed_code) if observed_code is not None else ERROR,
            "exit_code": observed_code if observed_code is not None else 2,
            "message": "no case result written — status derived from the runner's exit status",
            "result_file": path or None,
        }
        if entry["exit_code"] > 128:
            number = entry["exit_code"] - 128
            entry["signal"] = number
            entry["signal_name"] = signal_name(number)
        return entry

    entry = {key: value for key, value in payload.items()
             if key not in ("schema_version", "record")}
    entry["id"] = case_id
    entry["result_file"] = path
    if observed_code is not None and payload.get("exit_code") != observed_code:
        # The runner's own file disagrees with what the driver waited on. Trust
        # the wait status and say so, so a stale file is visible, not silent.
        entry["result_mismatch"] = {
            "recorded_exit_code": payload.get("exit_code"),
            "observed_exit_code": observed_code,
        }
        entry["status"] = classify(observed_code)
        entry["exit_code"] = observed_code
    return entry


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="regressionState.py",
        description="Emit PGF regression run state as JSON / NDJSON.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_header = sub.add_parser("header", help="write a stream header if absent")
    p_header.add_argument("--stream", action="append", required=True, metavar="PATH")
    add_field_options(p_header)
    p_header.set_defaults(func=cmd_header)

    p_event = sub.add_parser("event", help="append one event to one or more streams")
    p_event.add_argument("--type", required=True, help="event name, e.g. case_started")
    p_event.add_argument("--stream", action="append", required=True, metavar="PATH")
    add_field_options(p_event)
    p_event.set_defaults(func=cmd_event)

    p_snap = sub.add_parser("snapshot", help="atomically write a JSON snapshot")
    p_snap.add_argument("--path", required=True, metavar="PATH")
    p_snap.add_argument("--record", default="snapshot", help="record discriminator")
    add_field_options(p_snap)
    p_snap.set_defaults(func=cmd_snapshot)

    p_res = sub.add_parser("result", help="atomically write a terminal result")
    p_res.add_argument("--path", required=True, metavar="PATH")
    p_res.add_argument("--exit-code", type=int, required=True)
    p_res.add_argument("--status", default="", help="override the derived status")
    p_res.add_argument("--suite-id", default="")
    p_res.add_argument("--case-id", default="")
    p_res.add_argument("--phase", default="", help="phase the case was in at exit")
    p_res.add_argument("--message", default="")
    p_res.add_argument("--runner", default="", help="native runner name")
    p_res.add_argument("--started-epoch", type=float, default=None)
    p_res.add_argument("--ended-epoch", type=float, default=None)
    add_field_options(p_res)
    p_res.set_defaults(func=cmd_result)

    p_list = sub.add_parser("listing", help="emit a suite's selected-case manifest")
    p_list.add_argument("--suite", default="", help="suite name, e.g. regression")
    p_list.add_argument("--driver", default="", help="driver name, e.g. Allrun")
    p_list.add_argument("--project-root", default="", help="root case paths are relative to")
    p_list.add_argument("--runner", default="", help="per-case runner name")
    p_list.add_argument("--record", default="listing", help="record discriminator")
    p_list.add_argument("--out", default="", help="write here instead of stdout")
    p_list.add_argument("--case-id", action="append", metavar="ID")
    p_list.add_argument("--case-tag", action="append", metavar="TAG")
    add_field_options(p_list)
    p_list.set_defaults(func=cmd_listing)

    p_suite = sub.add_parser("suite-result", help="write the aggregated suite result")
    p_suite.add_argument("--path", required=True, metavar="PATH")
    p_suite.add_argument("--exit-code", type=int, required=True)
    p_suite.add_argument("--status", default="", help="override the derived status")
    p_suite.add_argument("--suite", default="")
    p_suite.add_argument("--driver", default="")
    p_suite.add_argument("--cases-root", default="", help="dir holding cases/<id>/result.json")
    p_suite.add_argument("--case-id", action="append", metavar="ID")
    p_suite.add_argument("--case-exit-code", action="append", metavar="N",
                         help="exit status the driver waited on, per --case-id")
    p_suite.add_argument("--started-epoch", type=float, default=None)
    p_suite.add_argument("--ended-epoch", type=float, default=None)
    add_field_options(p_suite)
    p_suite.set_defaults(func=cmd_suite_result)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except SystemExit:
        raise
    except OSError as exc:
        print(f"regressionState.py: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
