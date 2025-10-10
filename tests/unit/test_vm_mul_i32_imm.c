#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "errors/error_interface.h"
#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"

static void write_int32(Chunk* chunk, int32_t value, int line, int column) {
    uint32_t encoded = (uint32_t)value;
    writeChunk(chunk, (uint8_t)(encoded & 0xFF), line, column, "mul_i32_imm");
    writeChunk(chunk, (uint8_t)((encoded >> 8) & 0xFF), line, column, "mul_i32_imm");
    writeChunk(chunk, (uint8_t)((encoded >> 16) & 0xFF), line, column, "mul_i32_imm");
    writeChunk(chunk, (uint8_t)((encoded >> 24) & 0xFF), line, column, "mul_i32_imm");
}

static void write_mul_i32_imm_instruction(Chunk* chunk, uint8_t dst_reg, uint8_t src_reg, int32_t imm) {
    const int line = 1;
    const int column = 10;
    writeChunk(chunk, OP_MUL_I32_IMM, line, column, "mul_i32_imm");
    writeChunk(chunk, dst_reg, line, column, "mul_i32_imm");
    writeChunk(chunk, src_reg, line, column, "mul_i32_imm");
    write_int32(chunk, imm, line, column);
}

static void write_mul_i32_imm_program(Chunk* chunk, uint8_t dst_reg, uint8_t src_reg, int32_t imm) {
    write_mul_i32_imm_instruction(chunk, dst_reg, src_reg, imm);
    writeChunk(chunk, OP_HALT, 1, 1, "mul_i32_imm");
}

static bool test_mul_i32_imm_success(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_mul_i32_imm_program(&chunk, 0, 0, 3);

    vm_store_i32_typed_hot(0, 4);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_MUL_I32_IMM, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value reg0 = vm_get_register_safe(0);
    if (!(IS_I32(reg0) && AS_I32(reg0) == 12)) {
        fprintf(stderr, "Expected register 0 to be 12 after multiplication, got type %d value %d\n",
                reg0.type,
                IS_I32(reg0) ? AS_I32(reg0) : -1);
        success = false;
        goto cleanup;
    }

    int32_t typed_value = 0;
    if (!vm_try_read_i32_typed(0, &typed_value) || typed_value != 12) {
        fprintf(stderr, "Expected typed register 0 to be 12 after multiplication, got %d\n", typed_value);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_mul_i32_imm_overflow(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    write_mul_i32_imm_program(&chunk, 1, 0, 2);

    const char* source_text = "r1 = r0 * 2\n";
    if (init_feature_errors() != ERROR_REPORT_SUCCESS ||
        set_error_source_text(source_text, strlen(source_text)) != ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "Failed to set error source text for multiplication overflow test\n");
        freeChunk(&chunk);
        freeVM();
        return false;
    }

    vm_store_i32_typed_hot(0, INT32_MAX);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "Expected INTERPRET_RUNTIME_ERROR for multiplication overflow, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (!(IS_ERROR(vm.lastError) && AS_ERROR(vm.lastError)->type == ERROR_VALUE)) {
        fprintf(stderr, "Expected ERROR_VALUE for multiplication overflow\n");
        success = false;
        goto cleanup;
    }

    ObjError* err = AS_ERROR(vm.lastError);
    if (!(err->location.file && strcmp(err->location.file, "mul_i32_imm") == 0)) {
        fprintf(stderr, "Expected runtime error to report file mul_i32_imm, got %s\n",
                err->location.file ? err->location.file : "(null)");
        success = false;
        goto cleanup;
    }

cleanup:
    set_error_source_text(NULL, 0);
    cleanup_feature_errors();
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_mul_i32_imm_reuses_typed_cache(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);

    write_mul_i32_imm_instruction(&chunk, 0, 0, 3);
    write_mul_i32_imm_instruction(&chunk, 0, 0, 3);
    writeChunk(&chunk, OP_HALT, 1, 0, "mul_i32_imm");

    vm_store_i32_typed_hot(0, 2);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for repeated OP_MUL_I32_IMM, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[0] != REG_TYPE_I32) {
        fprintf(stderr, "Expected register 0 to stay typed as i32 after repeated multiplies\n");
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.dirty[0]) {
        fprintf(stderr, "Expected register 0 to reconcile after repeated multiplies\n");
        success = false;
        goto cleanup;
    }

    int32_t typed_value = 0;
    if (!vm_try_read_i32_typed(0, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_i32_typed to hit for register 0 after multiplies\n");
        success = false;
        goto cleanup;
    }

    if (typed_value != 18) {
        fprintf(stderr, "Expected typed register value 18 after two multiplies, got %d\n", typed_value);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.dirty[0]) {
        fprintf(stderr, "Expected dirty flag to remain clear after typed read\n");
        success = false;
        goto cleanup;
    }

    if (!IS_I32(vm.registers[0]) || AS_I32(vm.registers[0]) != 18) {
        fprintf(stderr, "Expected boxed register to reconcile to 18, got type %d value %d\n",
                vm.registers[0].type,
                IS_I32(vm.registers[0]) ? AS_I32(vm.registers[0]) : -1);
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
        {"OP_MUL_I32_IMM multiplies immediate with register", test_mul_i32_imm_success},
        {"OP_MUL_I32_IMM detects overflow", test_mul_i32_imm_overflow},
        {"OP_MUL_I32_IMM reuses typed cache on repeated execution", test_mul_i32_imm_reuses_typed_cache},
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

    printf("%d/%d OP_MUL_I32_IMM tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
