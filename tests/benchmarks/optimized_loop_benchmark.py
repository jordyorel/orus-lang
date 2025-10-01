#!/usr/bin/env python3
import time

TRIALS = 5
ITER_SIMPLE = 5_000_000
NEST_OUTER = 800
NEST_INNER = 800
ARRAY_LENGTH = 2048
ARRAY_REPEATS = 512

seed_values = [i * 3 for i in range(ARRAY_LENGTH)]

def main():
    total_simple = 0.0
    total_nested = 0.0
    total_array = 0.0
    checksum = 0

    print("=== Python Optimized Loop Benchmark ===")
    print("trials:", TRIALS)

    for trial in range(TRIALS):
        start_simple = time.perf_counter()
        simple_sum = 0
        for i in range(ITER_SIMPLE):
            simple_sum += i
        elapsed_simple = time.perf_counter() - start_simple
        total_simple += elapsed_simple

        start_nested = time.perf_counter()
        nested_acc = 0
        for outer in range(NEST_OUTER):
            inner = 0
            while inner < NEST_INNER:
                combined = outer * inner
                if combined % 2 == 0:
                    nested_acc += combined
                else:
                    nested_acc -= inner
                inner += 1
        elapsed_nested = time.perf_counter() - start_nested
        total_nested += elapsed_nested

        start_array = time.perf_counter()
        array_total = 0
        repeat = 0
        while repeat < ARRAY_REPEATS:
            for element in seed_values:
                array_total += element
            repeat += 1
        elapsed_array = time.perf_counter() - start_array
        total_array += elapsed_array

        checksum += simple_sum + nested_acc + array_total

        print(
            "trial",
            trial,
            "simple:",
            elapsed_simple,
            "nested:",
            elapsed_nested,
            "array:",
            elapsed_array,
        )

    trials_f = float(TRIALS)
    print("average_simple:", total_simple / trials_f)
    print("average_nested:", total_nested / trials_f)
    print("average_array:", total_array / trials_f)
    print("checksum:", checksum)
    print("=== Benchmark complete ===")

if __name__ == "__main__":
    main()
