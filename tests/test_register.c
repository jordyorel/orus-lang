// test_register_vm.c - Test program for register-based VM
#include <time.h>

#include "vm.h"

// Test helpers
void test_header(const char* name) { printf("\n=== %s ===\n", name); }

void test_result(const char* test, bool passed) {
    printf("  %s: %s\n", test, passed ? "PASSED" : "FAILED");
}

// Manual bytecode generation for more comprehensive tests
void emit_test_bytecode(Chunk* chunk) {
    // Test 1: Basic arithmetic (R0 = 15 + 25)
    addConstant(chunk, I32_VAL(15));
    addConstant(chunk, I32_VAL(25));

    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 0, 1, 1);  // R0
    writeChunk(chunk, 0, 1, 1);  // constant 0 (15)

    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 1, 1, 1);  // R1
    writeChunk(chunk, 1, 1, 1);  // constant 1 (25)

    writeChunk(chunk, OP_ADD_I32_R, 1, 1);
    writeChunk(chunk, 2, 1, 1);  // R2 = result
    writeChunk(chunk, 0, 1, 1);  // R0
    writeChunk(chunk, 1, 1, 1);  // R1

    writeChunk(chunk, OP_PRINT_R, 1, 1);
    writeChunk(chunk, 2, 1, 1);  // Print R2
}

// Test 2: Register reuse and complex expressions
void emit_complex_expression(Chunk* chunk) {
    // Calculate: (10 + 20) * (30 - 5)
    addConstant(chunk, I32_VAL(10));
    addConstant(chunk, I32_VAL(20));
    addConstant(chunk, I32_VAL(30));
    addConstant(chunk, I32_VAL(5));

    // R0 = 10
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 0, 1, 1);

    // R1 = 20
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 1, 1, 1);
    writeChunk(chunk, 1, 1, 1);

    // R2 = R0 + R1 (10 + 20 = 30)
    writeChunk(chunk, OP_ADD_I32_R, 1, 1);
    writeChunk(chunk, 2, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 1, 1, 1);

    // R0 = 30 (reuse R0)
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 2, 1, 1);

    // R1 = 5 (reuse R1)
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 1, 1, 1);
    writeChunk(chunk, 3, 1, 1);

    // R3 = R0 - R1 (30 - 5 = 25)
    writeChunk(chunk, OP_SUB_I32_R, 1, 1);
    writeChunk(chunk, 3, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 1, 1, 1);

    // R4 = R2 * R3 (30 * 25 = 750)
    writeChunk(chunk, OP_MUL_I32_R, 1, 1);
    writeChunk(chunk, 4, 1, 1);
    writeChunk(chunk, 2, 1, 1);
    writeChunk(chunk, 3, 1, 1);

    writeChunk(chunk, OP_PRINT_R, 1, 1);
    writeChunk(chunk, 4, 1, 1);
}

// Test 3: Loop with register-based counter
void emit_loop_test(Chunk* chunk) {
    // Simple loop: sum = 0; for(i = 0; i < 5; i++) sum += i;
    addConstant(chunk, I32_VAL(0));
    addConstant(chunk, I32_VAL(1));
    addConstant(chunk, I32_VAL(5));

    // R0 = sum = 0
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 0, 1, 1);

    // R1 = i = 0
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 1, 1, 1);
    writeChunk(chunk, 0, 1, 1);

    // R2 = 5 (loop limit)
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 2, 1, 1);
    writeChunk(chunk, 2, 1, 1);

    // Loop start
    int loopStart = chunk->count;

    // R3 = (R1 < R2)
    writeChunk(chunk, OP_LT_I32_R, 1, 1);
    writeChunk(chunk, 3, 1, 1);
    writeChunk(chunk, 1, 1, 1);
    writeChunk(chunk, 2, 1, 1);

    // Jump if not R3 (exit loop)
    writeChunk(chunk, OP_JUMP_IF_NOT_R, 1, 1);
    writeChunk(chunk, 3, 1, 1);
    int exitJumpAddr = chunk->count;
    writeChunk(chunk, 0, 1, 1);  // Placeholder for jump offset (high byte)
    writeChunk(chunk, 0, 1, 1);  // Placeholder for jump offset (low byte)

    // R0 = R0 + R1 (sum += i)
    writeChunk(chunk, OP_ADD_I32_R, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 0, 1, 1);
    writeChunk(chunk, 1, 1, 1);

    // R1 = R1 + 1 (i++)
    writeChunk(chunk, OP_LOAD_CONST, 1, 1);
    writeChunk(chunk, 4, 1, 1);
    writeChunk(chunk, 1, 1, 1);  // constant 1

    writeChunk(chunk, OP_ADD_I32_R, 1, 1);
    writeChunk(chunk, 1, 1, 1);
    writeChunk(chunk, 1, 1, 1);
    writeChunk(chunk, 4, 1, 1);

    // Jump back to loop start
    writeChunk(chunk, OP_LOOP, 1, 1);
    uint16_t loopOffset = chunk->count - loopStart + 2;
    writeChunk(chunk, (loopOffset >> 8) & 0xFF, 1, 1);
    writeChunk(chunk, loopOffset & 0xFF, 1, 1);

    // Patch exit jump
    int exitOffset = chunk->count - exitJumpAddr - 2;  // -2 because we advance past the offset bytes
    chunk->code[exitJumpAddr] = (exitOffset >> 8) & 0xFF;
    chunk->code[exitJumpAddr + 1] = exitOffset & 0xFF;

    // Print sum
    writeChunk(chunk, OP_PRINT_R, 1, 1);
    writeChunk(chunk, 0, 1, 1);
}

// Performance comparison test
void performance_test() {
    test_header("Performance Test - Register vs Stack Operations");

    const int iterations = 1000000;  // 1 million iterations (much smaller for safety)

    // Simulate register-based operations
    clock_t start = clock();

    volatile int reg[8] = {0};  // volatile to prevent optimization
    for (int i = 1; i <= iterations; i++) {
        reg[0] = i;
        reg[1] = i + 1;
        reg[2] = reg[0] + reg[1];
        reg[3] = reg[2] * 2;
    }

    clock_t reg_time = clock() - start;

    // Simulate stack-based operations
    start = clock();

    volatile int stack[64];  // Much smaller stack
    volatile int sp = 0;
    for (int i = 1; i <= iterations; i++) {
        stack[sp++] = i;        // push i
        stack[sp++] = i + 1;    // push i+1
        int b = stack[--sp];    // pop
        int a = stack[--sp];    // pop
        stack[sp++] = a + b;    // push result
        stack[sp++] = 2;        // push 2
        b = stack[--sp];        // pop
        a = stack[--sp];        // pop
        stack[sp++] = a * b;    // push result
        sp--;                   // pop final result
    }

    clock_t stack_time = clock() - start;

    double reg_ms = (double)reg_time * 1000.0 / CLOCKS_PER_SEC;
    double stack_ms = (double)stack_time * 1000.0 / CLOCKS_PER_SEC;

    printf("  Register-based time: %.1f ms\n", reg_ms);
    printf("  Stack-based time: %.1f ms\n", stack_ms);
    
    if (reg_ms > 0.1) {
        printf("  Speedup: %.2fx\n", stack_ms / reg_ms);
    } else {
        printf("  Both operations completed very quickly\n");
    }
}

// Main test runner
int main() {
    printf("Register-Based VM Test Suite\n");
    printf("============================\n");

    // Initialize VM
    initVM();

    // Test 1: Basic arithmetic
    test_header("Test 1: Basic Arithmetic");
    {
        Chunk chunk;
        initChunk(&chunk);
        emit_test_bytecode(&chunk);
        writeChunk(&chunk, OP_HALT, 1, 1);

        printf("  Expected: 40\n  Actual: ");
        fflush(stdout);

        vm.chunk = &chunk;
        vm.ip = chunk.code;
        InterpretResult result = run();

        test_result("Basic addition", result == INTERPRET_OK);

        if (vm.trace) {
            disassembleChunk(&chunk, "Basic Arithmetic");
        }

        freeChunk(&chunk);
    }

    // Test 2: Complex expression
    test_header("Test 2: Complex Expression");
    {
        Chunk chunk;
        initChunk(&chunk);
        emit_complex_expression(&chunk);
        writeChunk(&chunk, OP_HALT, 1, 1);

        printf("  Expected: 750\n  Actual: ");
        fflush(stdout);

        vm.chunk = &chunk;
        vm.ip = chunk.code;
        InterpretResult result = run();

        test_result("Complex expression", result == INTERPRET_OK);

        freeChunk(&chunk);
    }

    // Test 3: Loop
    test_header("Test 3: Loop with Accumulator");
    {
        Chunk chunk;
        initChunk(&chunk);
        emit_loop_test(&chunk);
        writeChunk(&chunk, OP_HALT, 1, 1);

        printf("  Expected: 10 (0+1+2+3+4)\n  Actual: ");
        fflush(stdout);

        vm.chunk = &chunk;
        vm.ip = chunk.code;
        InterpretResult result = run();

        test_result("Loop execution", result == INTERPRET_OK);

        freeChunk(&chunk);
    }

    // Test 4: Register allocation demonstration
    test_header("Test 4: Register Allocation");
    {
        printf("  Demonstrating register usage:\n");

        Chunk chunk;
        initChunk(&chunk);

        // Load values into multiple registers and print each one
        for (int i = 0; i < 8; i++) {
            addConstant(&chunk, I32_VAL(i * 10));
            
            // Load value into register
            writeChunk(&chunk, OP_LOAD_CONST, 1, 1);
            writeChunk(&chunk, i, 1, 1);        // register number
            writeChunk(&chunk, i, 1, 1);        // constant index
            
            // Print the register (this will be done during execution)
            writeChunk(&chunk, OP_PRINT_R, 1, 1);
            writeChunk(&chunk, i, 1, 1);
        }

        writeChunk(&chunk, OP_HALT, 1, 1);

        printf("  Expected values: 0, 10, 20, 30, 40, 50, 60, 70\n");
        printf("  Actual values: ");
        fflush(stdout);

        vm.chunk = &chunk;
        vm.ip = chunk.code;
        run();

        freeChunk(&chunk);
    }

    // Performance test
    performance_test();

    // Test 5: Using the simplified compiler
    test_header("Test 5: Compiler Integration");
    {
        printf("  Expected: 30\n  Actual: ");
        fflush(stdout);

        InterpretResult result = interpret("10 + 20");
        test_result("Compiler test", result == INTERPRET_OK);
    }

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("All tests completed.\n");
    printf("\nKey differences from stack-based VM:\n");
    printf("1. Instructions directly specify source/destination registers\n");
    printf("2. No stack pointer management needed\n");
    printf("3. Better performance for complex expressions\n");
    printf(
        "4. Easier to optimize (register allocation, dead code elimination)\n");
    printf("5. More compact code for expressions with register reuse\n");

    // Cleanup
    freeVM();

    return 0;
}
