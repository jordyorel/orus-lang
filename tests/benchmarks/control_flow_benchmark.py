#!/usr/bin/env python3
# Control Flow Benchmark for Cross-Language Performance Testing
# Equivalent to control_flow_benchmark.orus

import time
import sys

def main():
    start_time = time.time()
    
    # Test 1: Simple For Loop Performance (1 million iterations)
    simple_counter = 0
    for i in range(1000000):
        simple_counter += 1
    
    print(simple_counter)
    
    # Test 2: Nested Loop Performance (1000 x 1000)
    nested_total = 0
    for i in range(1000):
        for j in range(1000):
            nested_total += 1
    
    print(nested_total)
    
    # Test 3: While Loop with Conditional (100K iterations)
    while_counter = 0
    condition_hits = 0
    while while_counter < 100000:
        if while_counter % 2 == 0:
            condition_hits += 1
        while_counter += 1
    
    print(condition_hits)
    
    # Test 4: Conditional Logic (50K iterations)
    complex_result = 0
    for i in range(50000):
        if i % 3 == 0:
            complex_result += 3
        else:
            if i % 5 == 0:
                complex_result += 5
            else:
                complex_result += 1
    
    print(complex_result)
    
    # Test 5: Loop with Conditional Processing (10K iterations)
    break_continue_total = 0
    processed_count = 0
    for i in range(10000):
        if i % 100 == 0:
            break_continue_total += 0
        else:
            break_continue_total += 1
            processed_count += 1
    
    print(break_continue_total)
    print(processed_count)
    
    # Test 6: Short Jump Stress Test - Tight Nested Loops
    tight_nested_total = 0
    for a in range(200):
        for b in range(200):
            for c in range(5):
                tight_nested_total += 1
    
    print(tight_nested_total)
    
    # Test 7: Dense Conditionals
    dense_conditional_total = 0
    for i in range(20000):
        if i % 2 == 0:
            dense_conditional_total += 1
        if i % 3 == 0:
            dense_conditional_total += 2
        if i % 5 == 0:
            dense_conditional_total += 3
        if i % 7 == 0:
            dense_conditional_total += 4
    
    print(dense_conditional_total)
    
    # Test 8: Mixed Control Flow
    mixed_total = 0
    for outer in range(100):
        inner_count = 0
        while inner_count < 50:
            if inner_count % 3 == 0:
                if outer % 2 == 0:
                    mixed_total += 1
                else:
                    mixed_total += 2
            else:
                mixed_total += 1
            inner_count += 1
    
    print(mixed_total)
    
    end_time = time.time()
    print(f"Python execution time: {end_time - start_time:.6f} seconds", file=sys.stderr)

if __name__ == "__main__":
    main()