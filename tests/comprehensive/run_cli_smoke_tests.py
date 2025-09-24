import argparse
import os
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List, Optional, Tuple


class SmokeResult:
    def __init__(self, name: str, passed: bool, details: str = "") -> None:
        self.name = name
        self.passed = passed
        self.details = details


def resolve_binary(binary: str, repo_root: Path) -> Path:
    candidate = Path(binary)
    if candidate.exists():
        return candidate.resolve()

    fallback = repo_root / binary
    if fallback.exists():
        return fallback.resolve()

    # Fallback to debug binary if primary not found
    debug_candidate = repo_root / "orus_debug"
    if debug_candidate.exists():
        return debug_candidate.resolve()

    return candidate


def run_smoke(
    binary: Path,
    program: Path,
    expect_success: bool,
    expected_stdout: Optional[str] = None,
) -> SmokeResult:
    name = f"{binary.name} {program.as_posix()}"
    try:
        completed = subprocess.run(
            [str(binary), program.as_posix()],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(binary.parent),
        )
    except OSError as exc:  # pragma: no cover - defensive
        return SmokeResult(name, False, f"Failed to execute: {exc}")

    exit_ok = completed.returncode == 0 if expect_success else completed.returncode != 0
    if exit_ok and expect_success and expected_stdout is not None:
        if completed.stdout != expected_stdout:
            detail = (
                f"Expected stdout {expected_stdout!r} but received {completed.stdout!r}."
            )
            if completed.stderr:
                detail += f" Stderr:\n{completed.stderr.strip()}"
            return SmokeResult(name, False, detail)
    if exit_ok:
        return SmokeResult(name, True)

    output = completed.stdout + completed.stderr
    detail = (
        f"Expected {'success' if expect_success else 'failure'}, "
        f"but exit code was {completed.returncode}.\n{output.strip()}"
    )
    return SmokeResult(name, False, detail)


def load_tests(repo_root: Path) -> List[Tuple[Path, bool, Optional[str]]]:
    return [
        (
            repo_root / "tests" / "comprehensive" / "comprehensive_language_test.orus",
            True,
            None,
        ),
        (repo_root / "tests" / "error_reporting" / "undefined_variable.orus", False, None),
        (
            repo_root / "tests" / "comprehensive" / "result_builtins_cli.orus",
            True,
            "result builtins done\n",
        ),
        (
            repo_root / "tests" / "strings" / "string_equality_control_flow.orus",
            True,
            "ready\n",
        ),
    ]


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run CLI smoke tests for the Orus interpreter")
    parser.add_argument(
        "binary",
        nargs="?",
        default="./orus",
        help="Interpreter binary to execute (default: ./orus, falls back to ./orus_debug if missing)",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    repo_root = Path(__file__).resolve().parents[2]
    binary_path = resolve_binary(args.binary, repo_root)

    tests = load_tests(repo_root)
    results: List[SmokeResult] = []

    print("Running CLI smoke tests...")
    for program, expect_success, expected_stdout in tests:
        if not program.exists():
            results.append(SmokeResult(program.name, False, "Program file not found"))
            continue
        results.append(run_smoke(binary_path, program, expect_success, expected_stdout))

    failures = [result for result in results if not result.passed]
    for result in results:
        status = "PASS" if result.passed else "FAIL"
        print(f"[{status}] {result.name}")
        if result.details:
            for line in result.details.splitlines():
                print(f"    {line}")

    print(f"\nSummary: {len(results) - len(failures)}/{len(results)} smoke tests passed.")
    return 0 if not failures else 1


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
