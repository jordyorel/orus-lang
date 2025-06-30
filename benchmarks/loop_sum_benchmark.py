#!/usr/bin/env python3

import time

def main():
    x = 0
    start_time = time.time()
    
    for i in range(1000000000):
        x += i
    
    end_time = time.time()
    print(x)
    
    # Print timing info to stderr so it doesn't interfere with result comparison
    import sys
    print(f"Python execution time: {end_time - start_time:.6f} seconds", file=sys.stderr)

if __name__ == "__main__":
    main()