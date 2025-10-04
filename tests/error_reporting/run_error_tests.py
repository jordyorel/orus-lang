#!/usr/bin/env python3
"""Execute error-reporting regression programs and validate diagnostics."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List


@dataclass
class ErrorCase:
    program: Path
    expect_success: bool


def resolve_binary(candidate: str, repo_root: Path) -> Path:
    """Resolve the interpreter binary relative to the repository root."""

    path = Path(candidate)
    if path.is_file():
        return path.resolve()

    release_candidate = repo_root / candidate
    if release_candidate.is_file():
        return release_candidate.resolve()

    debug_candidate = repo_root / "orus_debug"
    if debug_candidate.is_file():
        return debug_candidate.resolve()

    # Fall back to the original path so the subprocess call surfaces the failure.
    return path


def discover_cases(repo_root: Path) -> List[ErrorCase]:
    """Return the ordered list of error-reporting scenarios to execute."""

    tests_dir = repo_root / "tests" / "error_reporting"
    success_cases = {
        "loop_control_valid.orus",
    }

    cases: List[ErrorCase] = []
    for program in sorted(tests_dir.glob("*.orus")):
        expect_success = program.name in success_cases
        cases.append(ErrorCase(program=program, expect_success=expect_success))
    return cases


def run_case(binary: Path, case: ErrorCase) -> tuple[bool, str]:
    """Execute a single test case.

    Returns a tuple of (passed, detail_message).
    """

    try:
        completed = subprocess.run(
            [str(binary), case.program.as_posix()],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(binary.parent),
        )
    except OSError as exc:  # pragma: no cover - defensive runtime guard
        return False, f"Failed to execute interpreter: {exc}"

    combined_output = (completed.stdout or "") + (completed.stderr or "")

    if case.expect_success:
        if completed.returncode != 0:
            detail = (
                "Expected success but received non-zero exit code "
                f"{completed.returncode}.\n{combined_output.strip()}"
            )
            return False, detail
        return True, ""

    # Failure expected.
    if completed.returncode == 0:
        return False, "Expected failure but program exited successfully."

    if not combined_output.strip():
        return False, "Interpreter failed without emitting any diagnostics."

    return True, ""


def format_status(passed: bool, expect_success: bool) -> str:
    if passed:
        label = "PASS" if expect_success else "CORRECT FAIL"
        color = "\033[32m"
    else:
        label = "FAIL"
        color = "\033[31m"
    return f"{color}{label}\033[0m"


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Orus error-reporting regression suite")
    parser.add_argument(
        "binary",
        nargs="?",
        default="./orus",
        help="Interpreter binary to execute (default: ./orus, falls back to ./orus_debug if absent)",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    repo_root = Path(__file__).resolve().parents[2]
    binary = resolve_binary(args.binary, repo_root)

    cases = discover_cases(repo_root)

    print("Running error-reporting tests...")
    failures: list[tuple[ErrorCase, str]] = []
    for case in cases:
        passed, detail = run_case(binary, case)
        status = format_status(passed, case.expect_success)
        print(f"[{status}] {binary.name} {case.program.relative_to(repo_root)}")
        if detail:
            for line in detail.splitlines():
                print(f"    {line}")
        if not passed:
            failures.append((case, detail))

    summary = f"{len(cases) - len(failures)}/{len(cases)} error-reporting tests passed."
    print(f"\nSummary: {summary}")

    return 0 if not failures else 1


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
