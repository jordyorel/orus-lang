import time

ITERATIONS = 100_000
TRIALS = 5

print("=== Python String Concatenation Benchmark ===")
print("iterations:", ITERATIONS)
print("trials:", TRIALS)

total_time = 0.0
checksum = 0

chars = ["a"] * ITERATIONS

for trial in range(TRIALS):
    start = time.perf_counter()
    s = ""
    for c in chars:
        s = s + c
    elapsed = time.perf_counter() - start
    total_time += elapsed
    checksum += len(s)
    print(f"trial {trial} length {len(s)} elapsed {elapsed}")

avg_time = total_time / TRIALS
print("average_time:", avg_time)
print("checksum:", checksum)
print("=== Benchmark complete ===")
