#!/usr/bin/env python3
"""Regression tests for the standalone `orus-prof` profiling CLI."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List


@dataclass
class TestCase:
    name: str
    func: Callable[[Path], None]


class TestFailure(Exception):
    pass


def resolve_binary(candidate: str, repo_root: Path) -> Path:
    """Locate the profiler binary for the current build profile.

    The build system appends a profile-specific suffix (``_debug``, ``_profiling``,
    ``_ci``) to produced binaries.  When running the test harness manually we
    therefore need to gracefully fall back to those suffixed variants if the
    unsuffixed name is absent.
    """

    path = Path(candidate)
    if path.exists():
        return path.resolve()

    # Consider both the repository root and any user-specified directory.
    search_roots: list[Path] = [repo_root]
    if path.parent != Path():
        search_roots.insert(0, path.parent.resolve())

    suffixes = ["", "_debug", "_profiling", "_ci"]
    base_name = path.name

    for root in search_roots:
        for suffix in suffixes:
            candidate_path = (root / f"{base_name}{suffix}").resolve()
            if candidate_path.exists():
                return candidate_path

    raise TestFailure(f"Profiler binary '{candidate}' was not found.")


def write_profile_fixture(contents: str) -> tuple[tempfile.TemporaryDirectory, Path]:
    tmpdir = tempfile.TemporaryDirectory()
    path = Path(tmpdir.name) / "profiling.json"
    normalized = textwrap.dedent(contents).strip() + "\n"
    path.write_text(normalized, encoding="utf-8")
    return tmpdir, path


def run_cli(binary: Path, profile_path: Path) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    return subprocess.run(
        [str(binary), str(profile_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )


def test_successful_report(binary: Path) -> None:
    tmpdir, fixture = write_profile_fixture(
        """
        {
          "totalInstructions": 12345,
          "totalCycles": 67890,
          "enabledFlags": 7,
          "instructions": [
            {"opcode": 5, "count": 100, "cycles": 250, "isHot": true},
            {"opcode": 21, "count": 60, "cycles": 320, "isHot": false}
          ],
          "specializations": [
            {"index": 0, "name": "fib.specialized", "tier": "specialized", "currentHits": 200, "specializationHits": 150, "threshold": 128, "eligible": true, "active": true},
            {"index": 1, "name": "helper.baseline", "tier": "baseline", "currentHits": 80, "specializationHits": 0, "threshold": 128, "eligible": false, "active": false}
          ]
        }
        """
    )

    try:
        result = run_cli(binary, fixture)
    finally:
        tmpdir.cleanup()

    if result.returncode != 0:
        raise TestFailure(f"CLI exited with {result.returncode}: {result.stderr}")

    stdout = result.stdout
    assertions = [
        "Orus Profiling Report" in stdout,
        "Total Instructions : 12345" in stdout,
        "Total Cycles       : 67890" in stdout,
        "Enabled Flags      : 0x7" in stdout,
        "Top Instruction Samples" in stdout,
        "OP_ADD" in stdout,
        "OP_JUMP" in stdout,
        "Function Specialization" in stdout,
        "[S]" in stdout,
        "fib.specialized" in stdout,
        "helper.baseline" in stdout,
    ]

    if not all(assertions):
        raise TestFailure(
            "Profiler report missing expected content.\n"
            f"STDOUT:\n{stdout}\nSTDERR:\n{result.stderr}"
        )


def test_handles_empty_sections(binary: Path) -> None:
    tmpdir, fixture = write_profile_fixture(
        """
        {
          "totalInstructions": 0,
          "totalCycles": 0,
          "enabledFlags": 0,
          "instructions": [],
          "specializations": []
        }
        """
    )

    try:
        result = run_cli(binary, fixture)
    finally:
        tmpdir.cleanup()

    if result.returncode != 0:
        raise TestFailure(
            "CLI should succeed on empty profiling data, "
            f"but exited with {result.returncode}: {result.stderr}"
        )

    stdout = result.stdout
    if "No instruction samples recorded." not in stdout:
        raise TestFailure(
            "Empty instruction section was not reported as expected.\n"
            f"STDOUT:\n{stdout}"
        )

    if "No function specialization metadata available." not in stdout:
        raise TestFailure(
            "Empty specialization section was not reported as expected.\n"
            f"STDOUT:\n{stdout}"
        )


def test_missing_profile_file(binary: Path) -> None:
    missing = Path("/tmp/does-not-exist-orus-prof.json")
    result = run_cli(binary, missing)
    if result.returncode == 0:
        raise TestFailure(
            "CLI succeeded unexpectedly when given a missing profile file."
        )

    if "failed to open" not in result.stderr:
        raise TestFailure(
            "CLI error message for missing profile file was not emitted.\n"
            f"STDERR:\n{result.stderr}"
        )


def main(argv: List[str]) -> int:
    repo_root = Path(__file__).resolve().parents[2]
    if len(argv) < 2:
        candidate = "orus-prof"
    else:
        candidate = argv[1]

    try:
        binary = resolve_binary(candidate, repo_root)
    except TestFailure as exc:
        print(f"[FAIL] {exc}")
        return 1

    tests: List[TestCase] = [
        TestCase("renders profiling summary", test_successful_report),
        TestCase("handles empty sections", test_handles_empty_sections),
        TestCase("reports missing file errors", test_missing_profile_file),
    ]

    all_passed = True
    for case in tests:
        try:
            case.func(binary)
        except TestFailure as exc:
            all_passed = False
            print(f"[FAIL] {case.name}: {exc}")
        except Exception as exc:  # pragma: no cover - defensive
            all_passed = False
            print(f"[FAIL] {case.name}: unexpected error: {exc}")
        else:
            print(f"[PASS] {case.name}")

    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
