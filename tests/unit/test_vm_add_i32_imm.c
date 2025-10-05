#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"

static void write_int32(Chunk* chunk, int32_t value) {
    uint32_t encoded = (uint32_t)value;
    writeChunk(chunk, (uint8_t)(encoded & 0xFF), 1, 0, "add_i32_imm");
    writeChunk(chunk, (uint8_t)((encoded >> 8) & 0xFF), 1, 0, "add_i32_imm");
    writeChunk(chunk, (uint8_t)((encoded >> 16) & 0xFF), 1, 0, "add_i32_imm");
    writeChunk(chunk, (uint8_t)((encoded >> 24) & 0xFF), 1, 0, "add_i32_imm");
}

static void write_add_i32_imm_program(Chunk* chunk, uint8_t dst_reg, uint8_t src_reg, int32_t imm) {
    writeChunk(chunk, OP_ADD_I32_IMM, 1, 0, "add_i32_imm");
    writeChunk(chunk, dst_reg, 1, 0, "add_i32_imm");
    writeChunk(chunk, src_reg, 1, 0, "add_i32_imm");
    write_int32(chunk, imm);
    writeChunk(chunk, OP_HALT, 1, 0, "add_i32_imm");
}

static bool test_add_i32_imm_success(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_add_i32_imm_program(&chunk, 0, 0, 3);

    vm_store_i32_typed_hot(0, 5);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_ADD_I32_IMM, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value reg0 = vm_get_register_safe(0);
    if (!(IS_I32(reg0) && AS_I32(reg0) == 8)) {
        fprintf(stderr, "Expected register 0 to be 8 after addition, got type %d value %d\n",
                reg0.type, IS_I32(reg0) ? AS_I32(reg0) : -1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_add_i32_imm_overflow(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_add_i32_imm_program(&chunk, 1, 0, 1);

    vm_store_i32_typed_hot(0, INT32_MAX);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "Expected INTERPRET_RUNTIME_ERROR for overflow, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (!(IS_ERROR(vm.lastError) && AS_ERROR(vm.lastError)->type == ERROR_VALUE)) {
        fprintf(stderr, "Expected ERROR_VALUE for overflow\n");
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
        {"OP_ADD_I32_IMM adds immediate to register", test_add_i32_imm_success},
        {"OP_ADD_I32_IMM detects overflow", test_add_i32_imm_overflow},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        bool ok = tests[i].fn();
        if (ok) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
        }
    }

    printf("%d/%d OP_ADD_I32_IMM tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
