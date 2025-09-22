#!/usr/bin/env python3
"""Loop microbenchmark harness for the Phase 2 typed increment fast path.

This script runs the dedicated loop benchmark program with different runtime
configurations to compare the typed increment fast path against its boxed
fallback.  It reports iteration throughput as well as typed loop telemetry so we
can spot regressions before rolling out new runtime changes.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_SOURCE = REPO_ROOT / "tests" / "benchmarks" / "loop_fastpath_phase2.orus"

ELAPSED_PATTERN = re.compile(r"^elapsed:\s*([0-9]+(?:\.[0-9]+)?)$")
TRIALS_PATTERN = re.compile(r"^trials:\s*(\d+)$")
ITERATIONS_PATTERN = re.compile(r"^iterations:\s*(\d+)$")
CHECKSUM_PATTERN = re.compile(r"^checksum:\s*(-?\d+)$")
TRACE_PATTERN = re.compile(
    r"typed_hit=(?P<hit>\d+)\s+typed_miss=(?P<miss>\d+)"
    r".*inc_overflow_bailouts=(?P<overflow>\d+)"
    r".*inc_type_instability=(?P<instability>\d+)"
)


@dataclass
class Variant:
    """Benchmark configuration descriptor."""

    name: str
    description: str
    env: Dict[str, str]


@dataclass
class BenchmarkStats:
    variant: Variant
    average_seconds: float
    stdev_seconds: float
    iterations_per_trial: int
    trials_per_run: int
    samples: int
    checksum: int
    telemetry: Dict[str, int]

    @property
    def iterations_per_second(self) -> float:
        return self.iterations_per_trial / self.average_seconds

    def to_row(self) -> List[str]:
        return [
            self.variant.name,
            f"{self.average_seconds:.6f}",
            f"{self.stdev_seconds:.6f}",
            f"{self.iterations_per_second:.2f}",
            str(self.telemetry.get("typed_hit", 0)),
            str(self.telemetry.get("typed_miss", 0)),
            str(self.telemetry.get("inc_overflow_bailouts", 0)),
            str(self.telemetry.get("inc_type_instability", 0)),
        ]


PHASE_VARIANTS: Dict[str, List[Variant]] = {
    "2": [
        Variant(
            name="typed-fastpath",
            description="Typed increment fast path enabled",
            env={},
        ),
        Variant(
            name="kill-switch",
            description="Fast path disabled via ORUS_DISABLE_INC_TYPED_FASTPATH",
            env={"ORUS_DISABLE_INC_TYPED_FASTPATH": "1"},
        ),
    ]
}

CSV_HEADER = [
    "variant",
    "avg_seconds",
    "stdev_seconds",
    "iterations_per_second",
    "typed_hit",
    "typed_miss",
    "inc_overflow_bailouts",
    "inc_type_instability",
]


def parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run loop microbenchmarks for the typed increment fast path and "
            "emit throughput + telemetry summaries."
        )
    )
    parser.add_argument(
        "--phase",
        default="2",
        choices=sorted(PHASE_VARIANTS.keys()),
        help="Benchmark phase to execute (default: 2).",
    )
    parser.add_argument(
        "--orus",
        type=Path,
        help="Path to the built Orus interpreter binary (defaults to `orus` or `orus_debug` in the repo root).",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=3,
        help="Number of harness invocations per variant (default: 3).",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        help="Optional path to write CSV results.",
    )
    parser.add_argument(
        "--bench",
        type=Path,
        default=BENCH_SOURCE,
        help="Path to the Orus loop benchmark source (default: phase 2 benchmark).",
    )
    return parser.parse_args(argv)


def resolve_orus_binary(cli_choice: Optional[Path]) -> Path:
    if cli_choice:
        return cli_choice

    release_candidate = REPO_ROOT / "orus"
    if release_candidate.exists():
        return release_candidate

    debug_candidate = REPO_ROOT / "orus_debug"
    if debug_candidate.exists():
        return debug_candidate

    return release_candidate


def ensure_prerequisites(orus_binary: Path, bench_source: Path) -> None:
    if not orus_binary.exists():
        raise SystemExit(
            f"Orus binary not found at {orus_binary}. Build the interpreter first (e.g. `make release`)."
        )
    if not bench_source.exists():
        raise SystemExit(f"Benchmark source not found at {bench_source}.")


def run_benchmark(orus_binary: Path, bench_source: Path, env: Dict[str, str]) -> subprocess.CompletedProcess:
    full_env = os.environ.copy()
    full_env.update(env)
    cmd = [str(orus_binary), str(bench_source)]
    return subprocess.run(cmd, capture_output=True, text=True, env=full_env, check=False)


def parse_stdout(stdout: str) -> (List[float], Optional[int], Optional[int], Optional[int]):
    elapsed: List[float] = []
    trials = None
    iterations = None
    checksum = None
    for raw_line in stdout.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if match := ELAPSED_PATTERN.match(line):
            elapsed.append(float(match.group(1)))
            continue
        if match := TRIALS_PATTERN.match(line):
            trials = int(match.group(1))
            continue
        if match := ITERATIONS_PATTERN.match(line):
            iterations = int(match.group(1))
            continue
        if match := CHECKSUM_PATTERN.match(line):
            checksum = int(match.group(1))
            continue
    if not elapsed:
        raise ValueError("Benchmark output did not contain any `elapsed:` samples.")
    return elapsed, trials, iterations, checksum


def parse_trace(stderr: str) -> Dict[str, int]:
    for raw_line in stderr.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if "[loop-trace]" not in line:
            continue
        match = TRACE_PATTERN.search(line)
        if not match:
            continue
        return {
            "typed_hit": int(match.group("hit")),
            "typed_miss": int(match.group("miss")),
            "inc_overflow_bailouts": int(match.group("overflow")),
            "inc_type_instability": int(match.group("instability")),
        }
    raise ValueError("Loop telemetry trace not found in stderr output.")


def collect_stats(
    variant: Variant,
    orus_binary: Path,
    bench_source: Path,
    runs: int,
) -> BenchmarkStats:
    samples: List[float] = []
    trials_per_run: Optional[int] = None
    iterations_per_trial: Optional[int] = None
    checksum: Optional[int] = None

    base_env = variant.env

    for run_index in range(runs):
        result = run_benchmark(orus_binary, bench_source, base_env)
        if result.returncode != 0:
            raise RuntimeError(
                f"Benchmark run failed for variant '{variant.name}' (exit code {result.returncode}).\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
        elapsed, trials, iterations, run_checksum = parse_stdout(result.stdout)
        samples.extend(elapsed)

        if trials is not None:
            if trials_per_run is None:
                trials_per_run = trials
            elif trials_per_run != trials:
                raise ValueError(
                    f"Inconsistent trial count detected: expected {trials_per_run}, got {trials}."
                )
        if iterations is not None:
            if iterations_per_trial is None:
                iterations_per_trial = iterations
            elif iterations_per_trial != iterations:
                raise ValueError(
                    f"Inconsistent iteration count detected: expected {iterations_per_trial}, got {iterations}."
                )
        if run_checksum is not None:
            if checksum is None:
                checksum = run_checksum
            elif checksum != run_checksum:
                raise ValueError(
                    f"Checksum mismatch detected between runs ({checksum} vs {run_checksum})."
                )

    if trials_per_run is None:
        trials_per_run = len(samples) // max(runs, 1)
    if iterations_per_trial is None:
        raise ValueError("Benchmark output did not report iteration count; update the Orus source?")
    if checksum is None:
        raise ValueError("Benchmark output did not report checksum; update the Orus source?")

    average = statistics.mean(samples)
    stdev = statistics.stdev(samples) if len(samples) > 1 else 0.0

    telemetry_env = dict(base_env)
    telemetry_env["ORUS_TRACE_TYPED_FALLBACKS"] = "1"
    telemetry_run = run_benchmark(orus_binary, bench_source, telemetry_env)
    if telemetry_run.returncode != 0:
        raise RuntimeError(
            f"Telemetry run failed for variant '{variant.name}' (exit code {telemetry_run.returncode}).\n"
            f"stdout:\n{telemetry_run.stdout}\n"
            f"stderr:\n{telemetry_run.stderr}"
        )
    _, trace_trials, trace_iterations, trace_checksum = parse_stdout(telemetry_run.stdout)
    telemetry = parse_trace(telemetry_run.stderr)

    if trace_iterations is not None and trace_iterations != iterations_per_trial:
        raise ValueError(
            f"Telemetry iteration count mismatch ({trace_iterations} vs {iterations_per_trial})."
        )
    if trace_trials is not None and trials_per_run is not None and trace_trials != trials_per_run:
        raise ValueError(
            f"Telemetry trial count mismatch ({trace_trials} vs {trials_per_run})."
        )
    if trace_checksum is not None and trace_checksum != checksum:
        raise ValueError(
            f"Telemetry checksum mismatch ({trace_checksum} vs {checksum})."
        )

    return BenchmarkStats(
        variant=variant,
        average_seconds=average,
        stdev_seconds=stdev,
        iterations_per_trial=iterations_per_trial,
        trials_per_run=trials_per_run,
        samples=len(samples),
        checksum=checksum,
        telemetry=telemetry,
    )


def emit_results(stats_list: List[BenchmarkStats], csv_path: Optional[Path]) -> None:
    header = (
        "Phase 2 typed increment microbenchmark"
        if len(stats_list) == len(PHASE_VARIANTS["2"])
        else "Loop microbenchmark results"
    )
    iterations = stats_list[0].iterations_per_trial if stats_list else 0
    trials = stats_list[0].trials_per_run if stats_list else 0
    samples = stats_list[0].samples if stats_list else 0
    runs = samples // max(trials, 1) if trials else 0

    print(header)
    print(
        f"  iterations per trial: {iterations}\n"
        f"  trials per run:       {trials}\n"
        f"  harness runs:         {runs}\n"
    )
    print(",".join(CSV_HEADER))
    for stats in stats_list:
        print(",".join(stats.to_row()))

    baseline = next((s for s in stats_list if s.variant.name == "typed-fastpath"), None)
    if baseline:
        for stats in stats_list:
            if stats is baseline:
                continue
            ratio = stats.average_seconds / baseline.average_seconds
            if ratio >= 1.0:
                print(
                    f"Speed ratio ({stats.variant.name} vs typed-fastpath): {ratio:.3f}x slower"
                )
            else:
                inverse = 1.0 / ratio if ratio > 0 else float('inf')
                print(
                    f"Speed ratio ({stats.variant.name} vs typed-fastpath): {inverse:.3f}x faster"
                )

    if csv_path:
        with csv_path.open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(CSV_HEADER)
            for stats in stats_list:
                writer.writerow(stats.to_row())
        print(f"\nWrote CSV results to {csv_path}")


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = parse_args(argv)
    orus_binary = resolve_orus_binary(args.orus)
    ensure_prerequisites(orus_binary, args.bench)

    variants = PHASE_VARIANTS.get(args.phase)
    if not variants:
        raise SystemExit(f"No benchmark variants registered for phase {args.phase}.")

    stats_list: List[BenchmarkStats] = []
    expected_checksum: Optional[int] = None

    for variant in variants:
        stats = collect_stats(variant, orus_binary, args.bench, args.runs)
        if expected_checksum is None:
            expected_checksum = stats.checksum
        elif stats.checksum != expected_checksum:
            raise ValueError(
                f"Checksum mismatch across variants ({stats.checksum} vs {expected_checksum})."
            )
        stats_list.append(stats)

    emit_results(stats_list, args.csv)
    return 0


if __name__ == "__main__":
    sys.exit(main())
