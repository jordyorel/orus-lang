#!/usr/bin/env python3
"""
Closure benchmark for Python
Tests closure creation and invocation performance
"""

import time

# Test closure creation and invocation
def make_counter():
    count = 0
    def increment():
        nonlocal count
        count += 1
        return count
    return increment

# Test closure with captured value
def make_adder(base):
    def add_to_base(x):
        return base + x
    return add_to_base

# Test deep nesting
def make_nested(a):
    def level1(b):
        def level2(c):
            return a + b + c
        return level2
    return level1

# Test multiple closure creation overhead
def create_multiple_closures():
    total = 0
    for i in range(100):
        def local_closure(x):
            return x * 2
        total += local_closure(i)
    return total

def main():
    print("=== Python Closure Benchmark ===")
    
    start_time = time.time_ns()
    
    # Test 1: Counter closure
    print("Test 1: Counter closure creation")
    counter = make_counter()
    counter_total = 0
    for i in range(1000):
        counter_total += counter()
    print(f"Counter total: {counter_total}")
    
    # Test 2: Closure with captured value
    print("Test 2: Adder closure with capture")
    add10 = make_adder(10)
    adder_total = 0
    for i in range(1000):
        adder_total += add10(i)
    print(f"Adder total: {adder_total}")
    
    # Test 3: Nested closures
    print("Test 3: Nested closure creation")
    nested = make_nested(5)
    mid = nested(10)
    nested_total = 0
    for i in range(1000):
        nested_total += mid(i)
    print(f"Nested total: {nested_total}")
    
    # Test 4: Multiple closure creation overhead
    print("Test 4: Multiple closure creation")
    creation_total = 0
    for i in range(100):
        creation_total += create_multiple_closures()
    print(f"Creation total: {creation_total}")
    
    end_time = time.time_ns()
    duration = end_time - start_time
    duration_ms = duration / 1000000
    
    print("=== Closure Benchmark Results ===")
    print(f"Total time (nanoseconds): {duration}")
    print(f"Total time (milliseconds): {duration_ms:.2f}")
    print("Closure benchmark completed")

if __name__ == "__main__":
    main()