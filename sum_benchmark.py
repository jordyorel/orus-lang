import time

N = 200_000
TRIALS = 10

def run_once() -> float:
    start = time.perf_counter()
    s = 0
    for i in range(1, N + 1):
        s += i
    elapsed = time.perf_counter() - start
    # Print each trial line similar to Orus program
    print(f"trial sum: {s} elapsed: {elapsed:.8f}")
    return elapsed

def main() -> None:
    print("=== Python Sum Benchmark ===")
    total = 0.0
    for _ in range(TRIALS):
        total += run_once()
    avg = total / TRIALS
    print(f"Average seconds: {avg:.8f}")
    print("=== Done ===")

if __name__ == "__main__":
    main()
