#!/usr/bin/env python3
"""Run the canonical hot-loop corpus and record interpreter baselines."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import List, Optional

REPO_ROOT = Path(__file__).resolve().parents[1]
ORUS_BINARY = REPO_ROOT / "orus"
ARTIFACT_DIR = REPO_ROOT / "artifacts" / "benchmarks"
INTERPRETER_RUNTIME_RE = re.compile(r"\[JIT Benchmark\] interpreter runtime: ([0-9.]+) ms")


@dataclass
class BenchmarkSpec:
    name: str
    category: str
    path: Path
    trials: Optional[int] = None
    status: str = "pending"
    interpreter_ms: Optional[float] = None
    notes: Optional[str] = None

    def command(self) -> List[str]:
        return [str(ORUS_BINARY), "--jit-benchmark", str(self.path)]

    def summary(self) -> dict:
        data = asdict(self)
        data["path"] = str(self.path)
        return data


def run_benchmark(spec: BenchmarkSpec) -> BenchmarkSpec:
    if not spec.path.exists():
        spec.status = "missing"
        spec.notes = f"benchmark file {spec.path} not found"
        return spec

    if not ORUS_BINARY.exists():
        spec.status = "blocked"
        spec.notes = "build the release binary with `make release` before running"
        return spec

    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    log_path = ARTIFACT_DIR / f"{spec.name}.log"

    try:
        result = subprocess.run(
            spec.command(),
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
            text=True,
        )
    except subprocess.CalledProcessError as exc:  # pragma: no cover - smoke runner
        spec.status = "failed"
        spec.notes = (
            f"command exited with {exc.returncode}; see {log_path} for details"
        )
        log_path.write_text(exc.stdout or "")
        return spec

    output = result.stdout
    log_path.write_text(output)

    match = INTERPRETER_RUNTIME_RE.search(output)
    if not match:
        spec.status = "failed"
        spec.notes = "interpreter runtime not found in benchmark output"
        return spec

    spec.interpreter_ms = float(match.group(1))
    spec.status = "ok"
    if spec.trials is not None:
        spec.notes = f"{spec.trials} trials recorded"
    return spec


def main() -> int:
    corpus = [
        BenchmarkSpec(
            name="optimized_loop",
            category="numeric_loops_fused",
            path=REPO_ROOT / "tests/benchmarks/optimized_loop_benchmark.orus",
            trials=5,
        ),
        BenchmarkSpec(
            name="string_concat",
            category="mixed_object_access",
            path=REPO_ROOT / "tests/benchmarks/string_concat_benchmark.orus",
            trials=5,
        ),
        BenchmarkSpec(
            name="typed_fastpath",
            category="numeric_micro_loops",
            path=REPO_ROOT / "tests/benchmarks/typed_fastpath_benchmark.orus",
            trials=5,
        ),
        BenchmarkSpec(
            name="ffi_ping_pong",
            category="ffi_churn",
            path=REPO_ROOT / "tests/benchmarks/ffi_ping_pong_benchmark.orus",
            trials=0,
        ),
    ]

    results = [run_benchmark(spec) for spec in corpus]
    summary_path = ARTIFACT_DIR / "hot_loop_baselines.json"
    summary = {
        "benchmarks": [spec.summary() for spec in results],
        "repository": REPO_ROOT.name,
        "orus_binary": str(ORUS_BINARY),
    }
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")

    for spec in results:
        status = spec.status
        runtime = f"{spec.interpreter_ms:.2f} ms" if spec.interpreter_ms else "--"
        note = spec.notes or ""
        print(f"{spec.category:24} {status:8} {runtime:>12} {spec.path}")
        if note:
            print(f"    {note}")

    missing = [spec for spec in results if spec.status != "ok"]
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
