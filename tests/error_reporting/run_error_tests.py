#!/usr/bin/env python3
"""Run Orus error reporting regression tests."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List, Tuple


ANSI_ESCAPE = re.compile(r"\x1B\[[0-9;]*m")


def load_cases(cases_path: Path) -> List[dict]:
    with cases_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, list):  # pragma: no cover - defensive
        raise ValueError("cases.json must contain a list of test case objects")
    return data


def format_block(prefix: str, text: str) -> str:
    indented_lines = [prefix + line if line else prefix.rstrip() for line in text.splitlines()]
    return "\n".join(indented_lines)


def run_case(binary: str, repo_root: Path, cases_dir: Path, case: dict) -> Tuple[bool, List[str]]:
    source_rel = case.get("source")
    if not source_rel:
        return False, ["Missing 'source' field in test case"]

    source_path = (cases_dir / source_rel).resolve()
    if not source_path.exists():
        return False, [f"Source file not found: {source_path}"]

    try:
        invocation_path = os.path.relpath(source_path, repo_root)
    except ValueError:
        invocation_path = str(source_path)

    cmd = [binary, invocation_path]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    combined_output = proc.stdout + proc.stderr
    plain_output = ANSI_ESCAPE.sub("", combined_output)

    errors: List[str] = []
    expected_exit = case.get("expected_exit")
    if expected_exit is not None:
        if proc.returncode != expected_exit:
            errors.append(f"Expected exit code {expected_exit}, but got {proc.returncode}")
    elif proc.returncode == 0:
        errors.append("Expected a failing exit code, but command succeeded")

    for snippet in case.get("expected", []):
        if snippet not in plain_output:
            errors.append(f"Missing expected text: {snippet}")

    if errors:
        errors.append("Command: " + " ".join(cmd))
        errors.append("Output:\n" + plain_output.rstrip())
        return False, errors

    return True, []


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Orus error reporting regression tests")
    parser.add_argument(
        "binary",
        nargs="?",
        default="./orus_debug",
        help="Path to the Orus interpreter binary (default: ./orus_debug)",
    )
    parser.add_argument(
        "--cases",
        dest="cases",
        default=None,
        help="Override path to cases.json (default: tests/error_reporting/cases.json)",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    repo_root = Path(__file__).resolve().parents[2]
    cases_dir = Path(__file__).resolve().parent
    cases_path = Path(args.cases) if args.cases else cases_dir / "cases.json"

    if not cases_path.exists():
        print(f"error: test case description not found: {cases_path}", file=sys.stderr)
        return 1

    cases = load_cases(cases_path)

    # Prefer a repo-relative binary path if it exists
    binary_path = Path(args.binary)
    if not binary_path.exists():
        candidate = (repo_root / args.binary).resolve()
        if candidate.exists():
            binary_path = candidate
        else:
            binary_path = Path(args.binary)  # allow system PATH lookup
    else:
        binary_path = binary_path.resolve()

    os.chdir(repo_root)

    print("Running error reporting regression tests...")
    passed = 0
    failed = 0

    for case in cases:
        ok, problems = run_case(str(binary_path), repo_root, cases_dir, case)
        description = case.get("description")
        label = case.get("name", "<unnamed>")
        if ok:
            if description:
                print(f"[PASS] {label}: {description}")
            else:
                print(f"[PASS] {label}")
            passed += 1
        else:
            print(f"[FAIL] {label}")
            for problem in problems:
                print(format_block("    ", problem))
            failed += 1

    total = passed + failed
    print(f"\nSummary: {passed}/{total} tests passed.")
    return 0 if failed == 0 else 1


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
