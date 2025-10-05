#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"

static void write_short(Chunk* chunk, int16_t value) {
    uint16_t encoded = (uint16_t)value;
    writeChunk(chunk, (uint8_t)(encoded >> 8), 1, 0, "inc_cmp_jmp");
    writeChunk(chunk, (uint8_t)(encoded & 0xFF), 1, 0, "inc_cmp_jmp");
}

static void write_inc_cmp_jmp_program(Chunk* chunk, uint8_t counter_reg, uint8_t limit_reg, int16_t offset) {
    writeChunk(chunk, OP_INC_CMP_JMP, 1, 0, "inc_cmp_jmp");
    writeChunk(chunk, counter_reg, 1, 0, "inc_cmp_jmp");
    writeChunk(chunk, limit_reg, 1, 0, "inc_cmp_jmp");
    write_short(chunk, offset);
    writeChunk(chunk, OP_HALT, 1, 0, "inc_cmp_jmp");
}

static bool test_inc_cmp_jmp_i32_loop(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_inc_cmp_jmp_program(&chunk, 0, 1, (int16_t)-5);

    vm_store_i32_typed_hot(0, 0);
    vm_store_i32_typed_hot(1, 5);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for i32 loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_I32(counter) && AS_I32(counter) == 5)) {
        fprintf(stderr, "Expected counter to reach 5 for i32 loop, got type %d value %d\n",
                counter.type, IS_I32(counter) ? AS_I32(counter) : -1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_cmp_jmp_u32_loop(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_inc_cmp_jmp_program(&chunk, 0, 1, (int16_t)-5);

    vm_store_u32_typed_hot(0, 0u);
    vm_store_u32_typed_hot(1, 4u);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for u32 loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_U32(counter) && AS_U32(counter) == 4u)) {
        fprintf(stderr, "Expected counter to reach 4 for u32 loop, got type %d value %u\n",
                counter.type, IS_U32(counter) ? AS_U32(counter) : 0u);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_cmp_jmp_i64_loop(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_inc_cmp_jmp_program(&chunk, 0, 1, (int16_t)-5);

    vm_store_i64_typed_hot(0, 0);
    vm_store_i64_typed_hot(1, 3);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for i64 loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_I64(counter) && AS_I64(counter) == 3)) {
        fprintf(stderr, "Expected counter to reach 3 for i64 loop, got type %d value %lld\n",
                counter.type, IS_I64(counter) ? (long long)AS_I64(counter) : -1LL);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_cmp_jmp_u64_loop(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_inc_cmp_jmp_program(&chunk, 0, 1, (int16_t)-5);

    vm_store_u64_typed_hot(0, 1u);
    vm_store_u64_typed_hot(1, 3u);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for u64 loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_U64(counter) && AS_U64(counter) == 3u)) {
        fprintf(stderr, "Expected counter to reach 3 for u64 loop, got type %d value %llu\n",
                counter.type, IS_U64(counter) ? (unsigned long long)AS_U64(counter) : 0ULL);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_cmp_jmp_i32_overflow(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_inc_cmp_jmp_program(&chunk, 0, 1, (int16_t)-5);

    vm_store_i32_typed_hot(0, INT32_MAX);
    vm_store_i32_typed_hot(1, INT32_MAX);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "Expected runtime error for i32 overflow, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (!(IS_ERROR(vm.lastError) && AS_ERROR(vm.lastError)->type == ERROR_VALUE)) {
        fprintf(stderr, "Expected ERROR_VALUE for i32 overflow\n");
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_cmp_jmp_i64_overflow(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_inc_cmp_jmp_program(&chunk, 0, 1, (int16_t)-5);

    vm_store_i64_typed_hot(0, INT64_MAX);
    vm_store_i64_typed_hot(1, INT64_MAX);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "Expected runtime error for i64 overflow, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (!(IS_ERROR(vm.lastError) && AS_ERROR(vm.lastError)->type == ERROR_VALUE)) {
        fprintf(stderr, "Expected ERROR_VALUE for i64 overflow\n");
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"OP_INC_CMP_JMP increments i32", test_inc_cmp_jmp_i32_loop},
        {"OP_INC_CMP_JMP increments u32", test_inc_cmp_jmp_u32_loop},
        {"OP_INC_CMP_JMP increments i64", test_inc_cmp_jmp_i64_loop},
        {"OP_INC_CMP_JMP increments u64", test_inc_cmp_jmp_u64_loop},
        {"OP_INC_CMP_JMP detects i32 overflow", test_inc_cmp_jmp_i32_overflow},
        {"OP_INC_CMP_JMP detects i64 overflow", test_inc_cmp_jmp_i64_overflow},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        if (tests[i].fn()) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            return 1;
        }
    }

    printf("%d/%d OP_INC_CMP_JMP tests passed\n", passed, total);
    return 0;
}
