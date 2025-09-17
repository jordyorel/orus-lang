#!/usr/bin/env python3
"""Run Orus error reporting regression tests."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Tuple


ANSI_ESCAPE = re.compile(r"\x1B\[[0-9;]*m")
DIAGNOSTIC_HEADER = re.compile(r"^-- (?P<category>[^:]+): (?P<title>.+?) -+ (?P<location>.+)$")


@dataclass
class Diagnostic:
    category: str
    title: str
    location: str
    message: Optional[str]
    help_text: Optional[str]
    note_text: Optional[str]


def parse_diagnostics(text: str) -> List[Diagnostic]:
    lines = text.splitlines()
    diagnostics: List[Diagnostic] = []
    i = 0

    while i < len(lines):
        header_match = DIAGNOSTIC_HEADER.match(lines[i])
        if not header_match:
            i += 1
            continue

        category = header_match.group("category").strip()
        title = header_match.group("title").strip()
        location = header_match.group("location").strip()

        i += 1
        message: Optional[str] = None
        help_text: Optional[str] = None
        note_text: Optional[str] = None

        while i < len(lines) and not lines[i].startswith("-- "):
            current = lines[i].strip()
            if "^" in lines[i] and "|" in lines[i]:
                caret_parts = current.split("^", 1)
                if len(caret_parts) == 2:
                    caret_message = caret_parts[1].strip()
                    if caret_message and message is None:
                        message = caret_message
            if current.startswith("= "):
                payload = current[2:].strip()
                if payload.startswith("help: "):
                    help_text = payload[len("help: "):].strip()
                elif payload.startswith("note: "):
                    note_text = payload[len("note: "):].strip()
                elif payload:
                    if message is None:
                        message = payload
            i += 1

        diagnostics.append(
            Diagnostic(
                category=category,
                title=title,
                location=location,
                message=message,
                help_text=help_text,
                note_text=note_text,
            )
        )

    return diagnostics


def match_diagnostic(expected: dict, actual: Diagnostic) -> bool:
    title = expected.get("title")
    if title and title not in actual.title:
        return False

    category = expected.get("category")
    if category and category not in actual.category:
        return False

    location_substring = expected.get("location_contains")
    if location_substring and location_substring not in actual.location:
        return False

    message_snippet = expected.get("message_contains")
    if message_snippet and (not actual.message or message_snippet not in actual.message):
        return False

    help_snippet = expected.get("help_contains")
    if help_snippet and (not actual.help_text or help_snippet not in actual.help_text):
        return False

    note_snippet = expected.get("note_contains")
    if note_snippet and (not actual.note_text or note_snippet not in actual.note_text):
        return False

    return True


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

    diagnostics = parse_diagnostics(plain_output)

    expected_diag_count = case.get("expected_diagnostic_count")
    if expected_diag_count is not None and expected_diag_count != len(diagnostics):
        errors.append(
            f"Expected {expected_diag_count} diagnostics, but observed {len(diagnostics)}"
        )

    for expected in case.get("expected_diagnostics", []):
        matches = [diag for diag in diagnostics if match_diagnostic(expected, diag)]
        expected_count = expected.get("count")
        if expected_count is not None:
            if len(matches) != expected_count:
                summary = (
                    f"Expected {expected_count} diagnostics matching {expected}, "
                    f"but found {len(matches)}"
                )
                errors.append(summary)
        elif not matches:
            errors.append(f"No diagnostic matched: {expected}")

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
