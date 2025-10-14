#!/usr/bin/env python3
"""Run Orus JIT benchmarks and enforce uplift/coverage thresholds."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

SPEEDUP_PATTERN = re.compile(r"speedup:\s*([0-9]+(?:\.[0-9]+)?)x")
COVERAGE_PATTERN = re.compile(
    r"native coverage:\s*([0-9]+(?:\.[0-9]+)?)%\s*\((\d+)/(\d+)\)", re.IGNORECASE
)


def run_benchmark(binary: Path, program: Path) -> str:
    """Execute the benchmark and return the combined stdout/stderr text."""
    process = subprocess.run(
        [str(binary), "--jit-benchmark", str(program)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )

    sys.stdout.write(process.stdout)
    sys.stdout.flush()

    if process.returncode != 0:
        raise RuntimeError(
            f"Benchmark {program} exited with status {process.returncode}"
        )

    return process.stdout


def extract_metrics(output: str) -> tuple[float, float]:
    speedup_match = SPEEDUP_PATTERN.search(output)
    if not speedup_match:
        raise ValueError("Unable to locate speedup in benchmark output")
    speedup = float(speedup_match.group(1))

    coverage_match = COVERAGE_PATTERN.search(output)
    if not coverage_match:
        raise ValueError("Unable to locate native coverage in benchmark output")
    coverage_percent = float(coverage_match.group(1))
    coverage_ratio = coverage_percent / 100.0

    return speedup, coverage_ratio


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Orus JIT benchmarks with performance thresholds",
    )
    parser.add_argument(
        "programs",
        nargs="+",
        type=Path,
        help="Benchmark programs to execute",
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path("./orus"),
        help="Path to the Orus binary (default: ./orus)",
    )
    parser.add_argument(
        "--speedup",
        type=float,
        default=3.0,
        help="Minimum acceptable JIT speedup factor",
    )
    parser.add_argument(
        "--coverage",
        type=float,
        default=0.90,
        help="Minimum acceptable native coverage ratio (0-1 range)",
    )

    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.exists():
        parser.error(f"Orus binary not found: {binary}")

    failures: list[str] = []

    for program in args.programs:
        resolved_program = program.resolve()
        if not resolved_program.exists():
            failures.append(f"Benchmark program not found: {resolved_program}")
            continue

        sys.stdout.write(f"\n==> Running benchmark: {resolved_program}\n")
        sys.stdout.flush()

        try:
            output = run_benchmark(binary, resolved_program)
            speedup, coverage = extract_metrics(output)
        except (RuntimeError, ValueError) as exc:
            failures.append(f"{resolved_program}: {exc}")
            continue

        if speedup < args.speedup:
            failures.append(
                f"{resolved_program}: speedup {speedup:.2f}x below required {args.speedup:.2f}x"
            )
        if coverage < args.coverage:
            failures.append(
                f"{resolved_program}: native coverage {coverage * 100:.1f}% below required"
                f" {args.coverage * 100:.1f}%"
            )

    if failures:
        sys.stderr.write("\nBenchmark thresholds not met:\n")
        for failure in failures:
            sys.stderr.write(f"  - {failure}\n")
        return 1

    sys.stdout.write("\nAll JIT benchmarks satisfied uplift and coverage thresholds.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
