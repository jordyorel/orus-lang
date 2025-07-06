#!/usr/bin/env python3
"""Scope Management Benchmark for Cross-Language Performance Testing."""
import time
import sys

def main():
    start = time.time()
    total = 0

    # Test 1: Nested if scopes with shadowing (50K iterations)
    for i in range(50000):
        value = i
        if value % 2 == 0:
            value = value + 1
            if value % 3 == 0:
                value = value + 2
                total += value
            else:
                total += value
        else:
            total += value

    # Test 2: Deeply nested loops with shadowed variables
    for outer in range(100):
        for inner in range(100):
            outer_val = inner
            for deep in range(10):
                outer_val = deep
                total += outer_val

    print(total)
    end = time.time()
    print(f"Python execution time: {end - start:.6f} seconds", file=sys.stderr)

if __name__ == "__main__":
    main()
