import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


@dataclass
class SmokeResult:
    name: str
    passed: bool
    expect_success: bool
    details: str = ""


@dataclass
class SmokeCase:
    program: Path
    expect_success: bool
    expected_stdout: Optional[str] = None
    stdin_data: Optional[str] = None
    expected_stderr_substrings: Optional[List[str]] = None
    run_without_local_std: bool = False
    env: Optional[Dict[str, str]] = None


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


def run_smoke(binary: Path, case: SmokeCase) -> SmokeResult:
    program = case.program
    temp_dir: Optional[tempfile.TemporaryDirectory[str]] = None
    binary_to_run = binary
    working_dir = binary.parent
    name = f"{binary.name} {program.as_posix()}"
    completed: Optional[subprocess.CompletedProcess[str]] = None

    try:
        if case.run_without_local_std:
            temp_dir = tempfile.TemporaryDirectory(prefix="orus-stdlib-missing-")
            binary_copy = Path(temp_dir.name) / binary.name
            shutil.copy2(binary, binary_copy)
            binary_to_run = binary_copy
            working_dir = binary_copy.parent
            name = f"{binary_to_run.name} {program.as_posix()}"

        env = os.environ.copy()
        if case.env:
            env.update(case.env)

        completed = subprocess.run(
            [str(binary_to_run), program.as_posix()],
            input=case.stdin_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(working_dir),
            env=env,
        )
    except OSError as exc:  # pragma: no cover - defensive
        return SmokeResult(
            name=name,
            passed=False,
            expect_success=case.expect_success,
            details=f"Failed to execute: {exc}",
        )
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()

    if completed is None:
        return SmokeResult(
            name=name,
            passed=False,
            expect_success=case.expect_success,
            details="Interpreter did not produce a result.",
        )

    exit_ok = (
        completed.returncode == 0
        if case.expect_success
        else completed.returncode != 0
    )

    combined_output = (completed.stdout or "") + (completed.stderr or "")

    if exit_ok:
        if case.expect_success and case.expected_stdout is not None:
            if completed.stdout != case.expected_stdout:
                detail = (
                    f"Expected stdout {case.expected_stdout!r} but received {completed.stdout!r}."
                )
                if completed.stderr:
                    detail += f" Stderr:\n{completed.stderr.strip()}"
                return SmokeResult(
                    name=name,
                    passed=False,
                    expect_success=case.expect_success,
                    details=detail,
                )

        if case.expected_stderr_substrings:
            missing = [
                snippet
                for snippet in case.expected_stderr_substrings
                if snippet not in combined_output
            ]
            if missing:
                detail = (
                    "Missing expected stderr substrings: "
                    + ", ".join(repr(s) for s in missing)
                )
                if completed.stderr:
                    detail += f"\nObserved stderr:\n{completed.stderr.strip()}"
                return SmokeResult(
                    name=name,
                    passed=False,
                    expect_success=case.expect_success,
                    details=detail,
                )

        return SmokeResult(name=name, passed=True, expect_success=case.expect_success)

    detail = (
        f"Expected {'success' if case.expect_success else 'failure'}, "
        f"but exit code was {completed.returncode}.\n{combined_output.strip()}"
    )
    return SmokeResult(name=name, passed=False, expect_success=case.expect_success, details=detail)


def format_status(result: SmokeResult) -> str:
    if result.passed:
        label = "PASS" if result.expect_success else "CORRECT FAIL"
        color = "\033[32m"
    else:
        label = "FAIL"
        color = "\033[31m"
    return f"{color}{label}\033[0m"


def load_tests(repo_root: Path) -> List[SmokeCase]:
    cases = [
        SmokeCase(
            repo_root / "tests" / "comprehensive" / "comprehensive_language_test.orus",
            True,
            None,
        ),
        SmokeCase(
            repo_root / "tests" / "error_reporting" / "undefined_variable.orus",
            False,
            None,
        ),
        SmokeCase(
            repo_root / "tests" / "comprehensive" / "result_builtins_cli.orus",
            True,
            "result builtins done\n",
        ),
        SmokeCase(
            repo_root / "tests" / "strings" / "string_equality_control_flow.orus",
            True,
            "ready\n",
        ),
        SmokeCase(
            repo_root / "tests" / "io" / "input_prompt_echo.orus",
            True,
            "Prompt> First:Hello\nSecond:<empty>\n",
            stdin_data="Hello\n\n",
        ),
    ]
    
    return cases


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
    for case in tests:
        if not case.program.exists():
            results.append(
                SmokeResult(
                    name=case.program.name,
                    passed=False,
                    expect_success=case.expect_success,
                    details="Program file not found",
                )
            )
            continue
        results.append(run_smoke(binary_path, case))

    failures = [result for result in results if not result.passed]
    for result in results:
        print(f"[{format_status(result)}] {result.name}")
        if result.details:
            for line in result.details.splitlines():
                print(f"    {line}")

    print(f"\nSummary: {len(results) - len(failures)}/{len(results)} smoke tests passed.")
    return 0 if not failures else 1


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
