#!/usr/bin/env julia
# Universal Arithmetic Benchmark for Cross-Language Performance Testing
# Equivalent to arithmetic_benchmark.orus

function main()
    start_time = time()
    
    # Test 1: Basic Addition Loop (1 million iterations)
    total = 0
    for i in 0:999999
        total += i
    end
    
    println(total)
    
    # Test 2: Mixed Arithmetic Operations (100K iterations)
    result = 1.0
    for i in 0:99999
        result += 1.5
        result *= 1.01
        result /= 1.005
        result -= 0.5
    end
    
    println(result)
    
    # Test 3: Integer Arithmetic Performance
    factorial_approx = 1
    for i in 1:19
        factorial_approx *= i
    end
    
    println(factorial_approx)
    
    # Test 4: Division and Modulo Operations
    division_sum = 0
    for i in 1:9999
        division_sum += (1000000 ÷ i) + (1000000 % i)
    end
    
    println(division_sum)
    
    # Test 5: Floating Point Precision
    precision_test = 0.0
    for i in 0:49999
        precision_test += 0.1
        precision_test -= 0.05
        precision_test *= 1.001
    end
    
    println(precision_test)
    
    end_time = time()
    println(stderr, "Julia execution time: $(end_time - start_time:.6f) seconds")
end

if abspath(PROGRAM_FILE) == @__FILE__
    main()
end