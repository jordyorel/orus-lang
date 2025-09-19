#!/usr/bin/env python3
import time

def main():
    print("=== Python Control Flow (For Loop) Benchmark ===")

    start = time.perf_counter()

    N1 = 2_000_000  # simple loop iterations
    O2 = 1000       # nested outer
    I2 = 1000       # nested inner
    N3 = 1_000_000  # while-like iterations

    # Phase 1: simple for-loop sum
    print("Phase 1: simple sum loop")
    s1 = 0
    for i in range(1, N1 + 1):
        s1 += i

    # Phase 2: nested loops with branch
    print("Phase 2: nested loops with branch")
    acc2 = 0
    for i in range(O2 + 1):
        base = i
        for j in range(I2 + 1):
            t = base + j
            if (t & 1) == 0:
                acc2 += t
            else:
                acc2 -= 1

    # Phase 3: even sum with stepping
    print("Phase 3: even sum with stepping loop")
    s3 = 0
    for k in range(0, N3 * 2 + 1, 2):
        s3 += k

    checksum = s1 + acc2 + s3

    elapsed = time.perf_counter() - start
    print("Checksum:", checksum)
    print("Total execution time:", elapsed)
    print("=== Python Control Flow Benchmark Complete ===")

if __name__ == "__main__":
    main()

