#!/usr/bin/env python3
"""Run and assess disposable Darcy/Forchheimer resistance studies.

The study copies the canonical one-dimensional Darcy case into a disposable
work directory, changes ``Df``, ``forchheimerCoeff``, and inlet velocity, then
runs the case's own ``Allrun``.  It reports pressure drop, mass-flow closure,
velocity extrema, and an isothermal ideal-gas Darcy-Forchheimer estimate.

This is a verification study, not a replacement for the registered regression
cases.  Generated cases and logs are kept in the reported work directory so a
result can be inspected after the command completes.
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
DEFAULT_TEMPLATE = ROOT / "tutorials/cases/canonical/darcy"

LENGTH = 0.05
AREA = 0.005 * 0.005
TEMPERATURE = 300.0
MOLAR_MASS_N2 = 0.0280134
R_UNIVERSAL = 8.31446261815324
SUTHERLAND_AS = 1.67212e-6
SUTHERLAND_TS = 170.672

_COMPOUND_RE = re.compile(r"\([^()]*\)")
_NUMERIC_TIME_RE = re.compile(r"^(?:0|[1-9][0-9]*)(?:\.[0-9]+)?$")


class StudyError(RuntimeError):
    """A study case did not produce a trustworthy result."""


@dataclass(frozen=True)
class StudySpec:
    name: str
    df: float
    forchheimer: float
    velocity: float


@dataclass(frozen=True)
class Measurement:
    spec: StudySpec
    pressure_inlet: float
    pressure_outlet: float
    mass_inlet: float
    mass_outlet: float
    velocity_average: float
    velocity_min: float
    velocity_max: float
    expected_drop: float
    measured_drop: float
    pressure_relative_error: float
    mass_relative_error: float


def parse_row(line: str) -> list[float]:
    """Parse a numeric OpenFOAM data row, including compound values."""
    values: list[float] = []
    position = 0
    while position < len(line):
        if line[position].isspace():
            position += 1
            continue
        if line[position] == "(":
            match = _COMPOUND_RE.match(line, position)
            if match is None:
                raise StudyError(f"malformed compound value: {line!r}")
            contents = match.group()[1:-1].split()
            if not contents:
                raise StudyError(f"empty compound value: {line!r}")
            try:
                values.extend(float(token) for token in contents)
            except ValueError as exc:
                raise StudyError(f"non-numeric compound value: {line!r}") from exc
            position = match.end()
            continue
        end = position
        while end < len(line) and not line[end].isspace():
            end += 1
        token = line[position:end]
        if "(" in token or ")" in token:
            raise StudyError(f"malformed value: {line!r}")
        try:
            values.append(float(token))
        except ValueError as exc:
            raise StudyError(f"non-numeric value {token!r}") from exc
        position = end
    if not values:
        raise StudyError("empty data row")
    return values


def last_row(case_dir: Path, function_name: str) -> list[float]:
    """Read the final row from one function-object data file."""
    root = case_dir / "postProcessing" / function_name
    files = list(root.glob("*/*.dat"))
    if not files:
        raise StudyError(f"no data file for {function_name} in {case_dir}")
    files.sort(key=lambda path: float(path.parent.name))
    rows = []
    with files[-1].open(encoding="utf-8") as stream:
        for raw in stream:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            rows.append(parse_row(line))
    if not rows:
        raise StudyError(f"no data rows for {function_name} in {files[-1]}")
    return rows[-1]


def replace_required(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise StudyError(f"could not update {label}")
    return updated


def configure_case(case_dir: Path, spec: StudySpec) -> None:
    """Apply one study point to a copied case."""
    df_path = case_dir / "0.orig/Df"
    df_text = df_path.read_text(encoding="utf-8")
    diagonal = f"({spec.df:g} 0 0 0 {spec.df:g} 0 0 0 {spec.df:g})"
    df_text = replace_required(
        df_text,
        r"(internalField\s+uniform\s+)\([^;]+\);",
        rf"\g<1>{diagonal};",
        "Df internalField",
    )
    df_path.write_text(df_text, encoding="utf-8")

    resistance_path = case_dir / "constant/porosityProperties"
    resistance_text = resistance_path.read_text(encoding="utf-8")
    resistance_text = replace_required(
        resistance_text,
        r"forchheimerCoeff\s+[^;]+;",
        f"forchheimerCoeff {spec.forchheimer:g};",
        "forchheimerCoeff",
    )
    resistance_path.write_text(resistance_text, encoding="utf-8")

    velocity_path = case_dir / "0.orig/U"
    velocity_text = velocity_path.read_text(encoding="utf-8")
    vector = f"({spec.velocity:g} 0 0)"
    velocity_text = replace_required(
        velocity_text,
        r"(internalField\s+uniform\s+)\([^;]+\);",
        rf"\g<1>{vector};",
        "U internalField",
    )
    velocity_text = replace_required(
        velocity_text,
        r"(value\s+uniform\s+)\([^;]+\);",
        rf"\g<1>{vector};",
        "U inlet value",
    )
    velocity_path.write_text(velocity_text, encoding="utf-8")


def copy_case(template: Path, destination: Path) -> None:
    """Copy source inputs while excluding generated OpenFOAM output."""

    def ignore(_path: str, names: list[str]) -> set[str]:
        ignored = {
            "0",
            "postProcessing",
            "reference",
            "render",
            "processor0",
            "processor1",
            "processor2",
            "processor3",
        }
        ignored.update(name for name in names if _NUMERIC_TIME_RE.match(name))
        return ignored

    shutil.copytree(template, destination, ignore=ignore)
    poly_mesh = destination / "constant/polyMesh"
    if poly_mesh.exists():
        shutil.rmtree(poly_mesh)


def run_case(case_dir: Path) -> None:
    log_path = case_dir / "study.log"
    with log_path.open("w", encoding="utf-8") as log:
        completed = subprocess.run(
            ["./Allrun"],
            cwd=case_dir,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if completed.returncode != 0:
        raise StudyError(f"{case_dir.name} Allrun failed with {completed.returncode}")


def nitrogen_viscosity(temperature: float) -> float:
    return SUTHERLAND_AS * math.sqrt(temperature) / (
        1.0 + SUTHERLAND_TS / temperature
    )


def expected_pressure_drop(
    pressure_outlet: float,
    mass_flux: float,
    df: float,
    forchheimer: float,
) -> float:
    """Estimate pressure drop from the isothermal ideal-gas integral."""
    gas_constant = R_UNIVERSAL / MOLAR_MASS_N2
    viscosity = nitrogen_viscosity(TEMPERATURE)
    pressure_squared = pressure_outlet**2 + 2.0 * gas_constant * TEMPERATURE * LENGTH * (
        viscosity * df * mass_flux + forchheimer * mass_flux**2
    )
    return math.sqrt(pressure_squared) - pressure_outlet


def measure(case_dir: Path, spec: StudySpec) -> Measurement:
    pressure_inlet = last_row(case_dir, "regressionPInlet")[1]
    pressure_outlet = last_row(case_dir, "regressionPOutlet")[1]
    mass_inlet = last_row(case_dir, "regressionMassInlet")[1]
    mass_outlet = last_row(case_dir, "regressionMassOutlet")[1]
    velocity_average = last_row(case_dir, "regressionUAverage")[1]
    velocity_extrema = last_row(case_dir, "regressionUMinMax")
    velocity_min, velocity_max = velocity_extrema[1:3]

    mass_flux = abs(mass_outlet) / AREA
    expected_drop = expected_pressure_drop(
        pressure_outlet,
        mass_flux,
        spec.df,
        spec.forchheimer,
    )
    measured_drop = pressure_inlet - pressure_outlet
    pressure_relative_error = abs(measured_drop - expected_drop) / max(
        abs(expected_drop), 1.0
    )
    mass_relative_error = abs(mass_inlet + mass_outlet) / max(
        abs(mass_inlet), abs(mass_outlet), 1.0e-30
    )
    values = (
        pressure_inlet,
        pressure_outlet,
        mass_inlet,
        mass_outlet,
        velocity_average,
        velocity_min,
        velocity_max,
        expected_drop,
        measured_drop,
        pressure_relative_error,
        mass_relative_error,
    )
    if not all(math.isfinite(value) for value in values):
        raise StudyError(f"non-finite result in {case_dir}")
    return Measurement(spec, *values)


def build_specs(args: argparse.Namespace) -> list[StudySpec]:
    specs = [
        StudySpec(f"darcy_Df{df:g}", df, 0.0, args.darcy_velocity)
        for df in args.darcy_df
    ]
    specs.extend(
        StudySpec(
            f"forchheimer_U{velocity:g}",
            args.forchheimer_df,
            args.forchheimer_coeff,
            velocity,
        )
        for velocity in args.forchheimer_velocity
    )
    return specs


def print_measurement(measurement: Measurement) -> None:
    spec = measurement.spec
    print(
        f"{spec.name:24} Df={spec.df:.3e} F={spec.forchheimer:.3e} "
        f"Uin={spec.velocity:.4g} "
        f"dp={measurement.measured_drop:.6g} Pa "
        f"theory={measurement.expected_drop:.6g} Pa "
        f"err={measurement.pressure_relative_error:.2%} "
        f"mdot={measurement.mass_inlet:.6g}/{measurement.mass_outlet:.6g} kg/s "
        f"U={measurement.velocity_average:.6g} "
        f"range=[{measurement.velocity_min:.6g}, {measurement.velocity_max:.6g}]"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--template",
        type=Path,
        default=DEFAULT_TEMPLATE,
        help="canonical Darcy case used as the disposable template",
    )
    parser.add_argument(
        "--workdir",
        type=Path,
        help="directory for generated study cases (default: /scratch or /tmp)",
    )
    parser.add_argument(
        "--darcy-df",
        type=float,
        nargs="+",
        default=[1.0e8, 1.0e9, 1.0e10],
        help="Darcy Df values in m^-2",
    )
    parser.add_argument(
        "--darcy-velocity",
        type=float,
        default=0.1,
        help="Darcy sweep inlet velocity in m/s",
    )
    parser.add_argument(
        "--forchheimer-df",
        type=float,
        default=1.0e8,
        help="Df used for the Forchheimer velocity sweep in m^-2",
    )
    parser.add_argument(
        "--forchheimer-coeff",
        type=float,
        default=1.0e5,
        help="Forchheimer coefficient used for the velocity sweep",
    )
    parser.add_argument(
        "--forchheimer-velocity",
        type=float,
        nargs="+",
        default=[0.05, 0.1, 0.2],
        help="Forchheimer sweep inlet velocities in m/s",
    )
    parser.add_argument(
        "--mass-rtol",
        type=float,
        default=1.0e-3,
        help="maximum relative inlet/outlet mass-flow mismatch",
    )
    parser.add_argument(
        "--pressure-rtol",
        type=float,
        default=0.15,
        help="maximum relative error against the analytical pressure estimate",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    template = args.template.resolve()
    if not template.is_dir():
        print(f"ERROR: template case not found: {template}", file=sys.stderr)
        return 2

    if args.workdir is None:
        parent = Path("/scratch") if Path("/scratch").is_dir() else None
        workdir = Path(tempfile.mkdtemp(prefix="porous-resistance-", dir=parent))
    else:
        workdir = args.workdir.resolve()
        if workdir.exists() and any(workdir.iterdir()):
            print(f"ERROR: workdir is not empty: {workdir}", file=sys.stderr)
            return 2
        workdir.mkdir(parents=True, exist_ok=True)

    measurements: list[Measurement] = []
    failures: list[str] = []
    print(f"Study output: {workdir}")
    for spec in build_specs(args):
        case_dir = workdir / spec.name
        try:
            copy_case(template, case_dir)
            configure_case(case_dir, spec)
            run_case(case_dir)
            measurement = measure(case_dir, spec)
            measurements.append(measurement)
            print_measurement(measurement)
            if measurement.mass_relative_error > args.mass_rtol:
                failures.append(
                    f"{spec.name}: mass-flow mismatch "
                    f"{measurement.mass_relative_error:.3e} > {args.mass_rtol:.3e}"
                )
            if measurement.measured_drop <= 0.0:
                failures.append(f"{spec.name}: pressure does not drop downstream")
            if measurement.pressure_relative_error > args.pressure_rtol:
                failures.append(
                    f"{spec.name}: pressure error "
                    f"{measurement.pressure_relative_error:.2%} > {args.pressure_rtol:.2%}"
                )
        except (OSError, StudyError) as exc:
            failures.append(f"{spec.name}: {exc}")
            print(f"ERROR {spec.name}: {exc}", file=sys.stderr)

    darcy = [item for item in measurements if item.spec.forchheimer == 0.0]
    if len(darcy) > 1:
        darcy.sort(key=lambda item: item.spec.df)
        if any(a.measured_drop >= b.measured_drop for a, b in zip(darcy, darcy[1:])):
            failures.append("Darcy pressure drop is not strictly increasing with Df")

    forchheimer = [item for item in measurements if item.spec.forchheimer != 0.0]
    if len(forchheimer) > 1:
        forchheimer.sort(key=lambda item: item.spec.velocity)
        if any(
            a.measured_drop >= b.measured_drop
            for a, b in zip(forchheimer, forchheimer[1:])
        ):
            failures.append(
                "Forchheimer pressure drop is not strictly increasing with velocity"
            )

    if failures:
        print("\nStudy FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("\nStudy PASS: finite flow, mass closure, positive pressure drop, and trends verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
