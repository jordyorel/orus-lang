#!/bin/bash

# Compile C benchmark with optimizations
gcc -O3 -march=native -mtune=native -ffast-math -funroll-loops -o simple_loop_benchmark_optimized simple_loop_benchmark_optimized.c

echo "C benchmark compiled with optimizations"