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

static void write_short_at(Chunk* chunk, int offset, int16_t value) {
    if (!chunk || !chunk->code || offset < 0 || offset + 1 >= chunk->count) {
        return;
    }
    uint16_t encoded = (uint16_t)value;
    chunk->code[offset] = (uint8_t)(encoded >> 8);
    chunk->code[offset + 1] = (uint8_t)(encoded & 0xFF);
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

static bool test_fused_loop_jump_targets_body(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);

    writeChunk(&chunk, OP_JUMP_IF_NOT_I32_TYPED, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int guard_operand_offset = chunk.count;
    write_short(&chunk, 0);

    int body_start = chunk.count;

    writeChunk(&chunk, OP_MOVE_I32, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 2, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");

    writeChunk(&chunk, OP_INC_CMP_JMP, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int16_t back_off = (int16_t)(body_start - (chunk.count + 2));
    write_short(&chunk, back_off);

    int loop_end = chunk.count;
    writeChunk(&chunk, OP_HALT, 1, 0, "inc_cmp_jmp");

    int16_t guard_offset = (int16_t)(loop_end - (guard_operand_offset + 2));
    write_short_at(&chunk, guard_operand_offset, guard_offset);

    int inc_operand_offset = loop_end - 2;
    int16_t encoded_offset = (int16_t)((chunk.code[inc_operand_offset] << 8) |
                                       chunk.code[inc_operand_offset + 1]);
    int resolved_target = inc_operand_offset + 2 + encoded_offset;

    bool success = true;
    if (resolved_target != body_start) {
        fprintf(stderr,
                "Expected fused loop back edge to land at %d, resolved %d (body_start=%d)\n",
                body_start, resolved_target, body_start);
        success = false;
    }

    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_fused_inc_loop_continue_terminates(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);

    writeChunk(&chunk, OP_JUMP_IF_NOT_I32_TYPED, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int guard_operand_offset = chunk.count;
    write_short(&chunk, 0);

    int body_start = chunk.count;

    writeChunk(&chunk, OP_JUMP, 1, 0, "inc_cmp_jmp");
    int continue_operand_offset = chunk.count;
    write_short(&chunk, 0);

    writeChunk(&chunk, OP_MOVE_I32, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 3, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 2, 1, 0, "inc_cmp_jmp");

    int continue_target = chunk.count;

    writeChunk(&chunk, OP_INC_CMP_JMP, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int16_t back_off = (int16_t)(body_start - (chunk.count + 2));
    write_short(&chunk, back_off);

    int loop_end = chunk.count;
    writeChunk(&chunk, OP_HALT, 1, 0, "inc_cmp_jmp");

    int16_t guard_offset = (int16_t)(loop_end - (guard_operand_offset + 2));
    write_short_at(&chunk, guard_operand_offset, guard_offset);

    int16_t continue_offset = (int16_t)(continue_target - (continue_operand_offset + 2));
    write_short_at(&chunk, continue_operand_offset, continue_offset);

    vm_store_i32_typed_hot(0, 0);
    vm_store_i32_typed_hot(1, 3);
    vm_store_i32_typed_hot(2, 42);
    vm_store_i32_typed_hot(3, -1);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for fused continue loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_I32(counter) && AS_I32(counter) == 3)) {
        fprintf(stderr, "Expected counter to reach 3 with continue, got type %d value %d\n",
                counter.type, IS_I32(counter) ? AS_I32(counter) : -1);
        success = false;
    }

    Value sentinel = vm_get_register_safe(3);
    if (!(IS_I32(sentinel) && AS_I32(sentinel) == -1)) {
        fprintf(stderr, "Continue should skip body writes; register 3 is type %d value %d\n",
                sentinel.type, IS_I32(sentinel) ? AS_I32(sentinel) : -1);
        success = false;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_fused_inc_loop_mutating_limit(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);

    writeChunk(&chunk, OP_JUMP_IF_NOT_I32_TYPED, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int guard_operand_offset = chunk.count;
    write_short(&chunk, 0);

    int body_start = chunk.count;

    writeChunk(&chunk, OP_MOVE_I32, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 2, 1, 0, "inc_cmp_jmp");

    writeChunk(&chunk, OP_INC_CMP_JMP, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int16_t back_off = (int16_t)(body_start - (chunk.count + 2));
    write_short(&chunk, back_off);

    int loop_end = chunk.count;
    writeChunk(&chunk, OP_HALT, 1, 0, "inc_cmp_jmp");

    int16_t guard_offset = (int16_t)(loop_end - (guard_operand_offset + 2));
    write_short_at(&chunk, guard_operand_offset, guard_offset);

    vm_store_i32_typed_hot(0, 0);
    vm_store_i32_typed_hot(1, 5);
    vm_store_i32_typed_hot(2, 5);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for fused limit mutation loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_I32(counter) && AS_I32(counter) == 5)) {
        fprintf(stderr, "Expected counter to reach 5 after mutating limit, got type %d value %d\n",
                counter.type, IS_I32(counter) ? AS_I32(counter) : -1);
        success = false;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_fused_dec_loop_mutating_limit(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);

    writeChunk(&chunk, OP_JUMP_IF_NOT_I32_TYPED, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    int guard_operand_offset = chunk.count;
    write_short(&chunk, 0);

    int body_start = chunk.count;

    writeChunk(&chunk, OP_MOVE_I32, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 2, 1, 0, "inc_cmp_jmp");

    writeChunk(&chunk, OP_DEC_CMP_JMP, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 0, 1, 0, "inc_cmp_jmp");
    writeChunk(&chunk, 1, 1, 0, "inc_cmp_jmp");
    int16_t back_off = (int16_t)(body_start - (chunk.count + 2));
    write_short(&chunk, back_off);

    int loop_end = chunk.count;
    writeChunk(&chunk, OP_HALT, 1, 0, "inc_cmp_jmp");

    int16_t guard_offset = (int16_t)(loop_end - (guard_operand_offset + 2));
    write_short_at(&chunk, guard_operand_offset, guard_offset);

    vm_store_i32_typed_hot(0, 5);
    vm_store_i32_typed_hot(1, 0);
    vm_store_i32_typed_hot(2, 0);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for descending fused loop, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value counter = vm_get_register_safe(0);
    if (!(IS_I32(counter) && AS_I32(counter) == 0)) {
        fprintf(stderr, "Expected counter to reach 0 after descending loop, got type %d value %d\n",
                counter.type, IS_I32(counter) ? AS_I32(counter) : -1);
        success = false;
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
        {"Fused loop back edge targets body", test_fused_loop_jump_targets_body},
        {"Fused loop continue jumps to increment", test_fused_inc_loop_continue_terminates},
        {"Fused INC loop handles limit mutation", test_fused_inc_loop_mutating_limit},
        {"Fused DEC loop handles limit mutation", test_fused_dec_loop_mutating_limit},
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
