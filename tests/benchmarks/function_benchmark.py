#!/usr/bin/env python3
"""
Function call benchmark for Python
Tests function call overhead and recursion performance
"""

import time

# Recursive factorial (not tail-call optimized in Python)
def factorial_recursive(n, acc=1):
    if n <= 1:
        return acc
    else:
        return factorial_recursive(n - 1, acc * n)

# Fibonacci with manual tail call simulation
def fibonacci_tail(n, a=0, b=1):
    if n == 0:
        return a
    else:
        return fibonacci_tail(n - 1, b, a + b)

# Simple recursive countdown
def countdown(n):
    if n <= 0:
        return 0
    else:
        return countdown(n - 1)

# Nested function calls
def nested_calls(n):
    def level1(x):
        return x + 1
    
    def level2(x):
        return level1(x) + 1
    
    def level3(x):
        return level2(x) + 1
    
    return level3(n)

# Mathematical function with multiple operations
def math_heavy(x):
    def square(n):
        return n * n
    
    def cube(n):
        return n * n * n
    
    return square(x) + cube(x) + x

def main():
    print("=== Python Function Benchmark ===")
    
    start_time = time.time_ns()
    
    # Test 1: Recursive factorial (limited to avoid stack overflow)
    print("Test 1: Recursive factorial")
    result1 = factorial_recursive(20)
    print(f"Factorial of 20: {result1}")
    
    # Test 2: Fibonacci
    print("Test 2: Fibonacci")
    result2 = fibonacci_tail(30)
    print(f"Fibonacci of 30: {result2}")
    
    # Test 3: Simple recursive countdown (limited to avoid stack overflow)
    print("Test 3: Recursive countdown")
    result3 = countdown(900)  # Lower than Orus due to stack limit
    print(f"Countdown result: {result3}")
    
    # Test 4: Nested function calls
    print("Test 4: Nested function calls")
    total = 0
    for i in range(1000):
        total += nested_calls(i)
    print(f"Nested calls total: {total}")
    
    # Test 5: Mathematical functions
    print("Test 5: Mathematical functions")
    math_total = 0
    for i in range(1000):
        math_total += math_heavy(i)
    print(f"Math functions total: {math_total}")
    
    end_time = time.time_ns()
    duration = end_time - start_time
    duration_ms = duration / 1000000
    
    print("=== Benchmark Results ===")
    print(f"Total time (nanoseconds): {duration}")
    print(f"Total time (milliseconds): {duration_ms:.2f}")
    print("Function benchmark completed")

if __name__ == "__main__":
    main()