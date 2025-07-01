#!/usr/bin/env python3
# Universal Arithmetic Benchmark for Cross-Language Performance Testing
# Equivalent to arithmetic_benchmark.orus

import time
import sys

def main():
    start_time = time.time()
    
    total = 0
    
    # Test 1: Basic Addition Loop (1 million iterations)
    for i in range(1000000):
        total += i
    
    print(total)
    
    # Test 2: Mixed Arithmetic Operations (100K iterations)
    result = 1.0
    for i in range(100000):
        result += 1.5
        result *= 1.01
        result /= 1.005
        result -= 0.5
    
    print(result)
    
    # Test 3: Integer Arithmetic Performance
    factorial_approx = 1
    for i in range(1, 20):
        factorial_approx *= i
    
    print(factorial_approx)
    
    # Test 4: Division and Modulo Operations
    division_sum = 0
    for i in range(1, 10000):
        division_sum += (1000000 // i) + (1000000 % i)
    
    print(division_sum)
    
    # Test 5: Floating Point Precision
    precision_test = 0.0
    for i in range(50000):
        precision_test += 0.1
        precision_test -= 0.05
        precision_test *= 1.001
    
    print(precision_test)
    
    end_time = time.time()
    print(f"Python execution time: {end_time - start_time:.6f} seconds", file=sys.stderr)

if __name__ == "__main__":
    main()