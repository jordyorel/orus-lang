#!/usr/bin/env bash

# Helper to execute the Orus test suite with configurable parameters.
# Usage: run_orus_tests.sh [interpreter] [test_root] [exclude_benchmarks]

interpreter_path="${1:-${ORUS:-orus}}"
test_root="${2:-${TESTDIR:-tests}}"
exclude_benchmarks="${3:-${ORUS_TEST_EXCLUDE_BENCHMARKS:-}}"

if [[ -z "$interpreter_path" ]]; then
    echo "Error: interpreter path must not be empty." >&2
    exit 1
fi

if [[ "$interpreter_path" == */* ]]; then
    interpreter_cmd=("$interpreter_path")
else
    interpreter_cmd=("./$interpreter_path")
fi

passed=0
failed=0
current_section=""

echo "Running Orus .orus test suite..."
echo "==================================="

if [[ "$exclude_benchmarks" == "1" ]]; then
    pass_tests=$(find "$test_root" -type f -name "*.orus" ! -path "$test_root/expected_failures/*" ! -path "$test_root/unit/*" ! -path "$test_root/benchmarks/*" | sort)
else
    pass_tests=$(find "$test_root" -type f -name "*.orus" ! -path "$test_root/expected_failures/*" ! -path "$test_root/unit/*" | sort)
fi

if [[ -n "$pass_tests" ]]; then
    echo " === Orus Program Tests === "
fi

for test_file in $pass_tests; do
    test_dir=$(dirname "$test_file")
    rel_dir=${test_dir#"$test_root/"}
    if [[ "$rel_dir" != "$current_section" ]]; then
        current_section="$rel_dir"
        echo ""
        echo " --- $rel_dir --- "
    fi

    stdin_file="${test_file%.orus}.stdin"
    tmp_output=$(mktemp)
    printf "Testing: %s ... " "$test_file"

    if [[ -f "$stdin_file" ]]; then
        if "${interpreter_cmd[@]}" "$test_file" <"$stdin_file" >"$tmp_output" 2>&1; then
            printf "PASS \n"
            passed=$((passed + 1))
        else
            status=$?
            printf "FAIL  (exit %d)\n" "$status"
            failed=$((failed + 1))
            if [[ -s "$tmp_output" ]]; then
                echo "        --- output (first 20 lines) ---"
                sed -n '1,20p' "$tmp_output" | sed 's/^/        /'
            fi
        fi
    elif "${interpreter_cmd[@]}" "$test_file" >"$tmp_output" 2>&1; then
        printf " PASS \n"
        passed=$((passed + 1))
    else
        status=$?
        printf "FAIL  (exit %d)\n" "$status"
        failed=$((failed + 1))
        if [[ -s "$tmp_output" ]]; then
            echo "        --- output (first 20 lines) ---"
            sed -n '1,20p' "$tmp_output" | sed 's/^/        /'
        fi
    fi

    rm -f "$tmp_output"
done

fail_tests=$(find "$test_root/expected_failures" -type f -name "*.orus" 2>/dev/null | sort)
if [[ -n "$fail_tests" ]]; then
    echo ""
    echo " === Expected Failure Tests === "
fi

for fail_test in $fail_tests; do
    stdin_file="${fail_test%.orus}.stdin"
    tmp_output=$(mktemp)
    printf "Testing: %s ... " "$fail_test"

    if [[ -f "$stdin_file" ]]; then
        if "${interpreter_cmd[@]}" "$fail_test" <"$stdin_file" >"$tmp_output" 2>&1; then
            printf " UNEXPECTED PASS \n"
            failed=$((failed + 1))
            if [[ -s "$tmp_output" ]]; then
                echo "        --- output (first 20 lines) ---"
                sed -n '1,20p' "$tmp_output" | sed 's/^/        /'
            fi
        else
            printf "... (CORRECT FAIL)\n"
            passed=$((passed + 1))
        fi
    elif "${interpreter_cmd[@]}" "$fail_test" >"$tmp_output" 2>&1; then
        printf " [31mUNEXPECTED PASS \n"
        failed=$((failed + 1))
        if [[ -s "$tmp_output" ]]; then
            echo "        --- output (first 20 lines) ---"
            sed -n '1,20p' "$tmp_output" | sed 's/^/        /'
        fi
    else
        printf "... (CORRECT FAIL)\n"
        passed=$((passed + 1))
    fi

    rm -f "$tmp_output"
done

echo ""
echo "========================"
echo " === Test Summary ==="
if [[ $failed -eq 0 ]]; then
    echo "✓ All $passed tests passed!"
else
    echo "✗ $failed test(s) failed, $passed test(s) passed. "
fi

echo ""

if [[ $failed -ne 0 ]]; then
    exit 1
fi