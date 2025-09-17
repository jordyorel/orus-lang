import time

def main() -> None:
    start = time.perf_counter()
    s = 0
    for i in range(1, 10001):
        s += i
    elapsed = time.perf_counter() - start
    print(f"Sum 1..10000: {s}")
    print(f"Elapsed seconds: {elapsed:.8f}")

if __name__ == "__main__":
    main()

