#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"

static void write_dec_instruction(Chunk* chunk, uint8_t opcode, uint8_t reg, const char* file_tag) {
    const int line = 1;
    const int column = 0;
    writeChunk(chunk, opcode, line, column, file_tag);
    writeChunk(chunk, reg, line, column, file_tag);
}

static void write_dec_program(Chunk* chunk, uint8_t opcode, uint8_t reg, int repeat, const char* file_tag) {
    for (int i = 0; i < repeat; ++i) {
        write_dec_instruction(chunk, opcode, reg, file_tag);
    }
    writeChunk(chunk, OP_HALT, 1, 0, file_tag);
}

static bool test_dec_i32_typed_hot_path(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    const uint16_t reg = FRAME_REG_START;
    write_dec_program(&chunk, OP_DEC_I32_R, (uint8_t)reg, 2, "dec_i32_hot");

    vm_store_i32_typed_hot(reg, 5);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_DEC_I32_R hot path, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I32) {
        fprintf(stderr, "Expected register %u to stay typed as i32 after decrements\n", reg);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected typed register %u to reconcile after decrements\n", reg);
        success = false;
        goto cleanup;
    }

    int32_t typed_value = 0;
    if (!vm_try_read_i32_typed(reg, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_i32_typed to succeed for register %u\n", reg);
        success = false;
        goto cleanup;
    }

    if (typed_value != 3) {
        fprintf(stderr, "Expected typed register %u to equal 3 after decrements, got %d\n", reg, typed_value);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected dirty flag to remain clear after typed read\n");
        success = false;
        goto cleanup;
    }

    if (!IS_I32(vm.registers[reg]) || AS_I32(vm.registers[reg]) != 3) {
        fprintf(stderr, "Expected boxed register to reconcile to 3, got type %d value %d\n",
                vm.registers[reg].type,
                IS_I32(vm.registers[reg]) ? AS_I32(vm.registers[reg]) : -1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_dec_i32_fallback_rehydrates_cache(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    const uint16_t reg = FRAME_REG_START;
    write_dec_program(&chunk, OP_DEC_I32_R, (uint8_t)reg, 1, "dec_i32_fallback");

    vm_set_register_safe(reg, I32_VAL(11));

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_DEC_I32_R fallback, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I32) {
        fprintf(stderr, "Expected fallback path to cache register %u as i32\n", reg);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected typed cache for register %u to be reconciled after fallback\n", reg);
        success = false;
        goto cleanup;
    }

    int32_t typed_value = 0;
    if (!vm_try_read_i32_typed(reg, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_i32_typed to succeed after fallback for register %u\n", reg);
        success = false;
        goto cleanup;
    }

    if (typed_value != 10) {
        fprintf(stderr, "Expected typed register %u to equal 10 after fallback decrement, got %d\n", reg, typed_value);
        success = false;
        goto cleanup;
    }

    if (!IS_I32(vm.registers[reg]) || AS_I32(vm.registers[reg]) != 10) {
        fprintf(stderr, "Expected boxed register to reconcile to 10, got type %d value %d\n",
                vm.registers[reg].type,
                IS_I32(vm.registers[reg]) ? AS_I32(vm.registers[reg]) : -1);
        success = false;
        goto cleanup;
    }

    Value reconciled = vm_reconcile_typed_register(reg);
    if (!IS_I32(reconciled) || AS_I32(reconciled) != 10) {
        fprintf(stderr, "Expected reconciliation to update boxed register to 10, got type %d value %d\n",
                reconciled.type,
                IS_I32(reconciled) ? AS_I32(reconciled) : -1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

int main(void) {
    bool success = true;

    if (!test_dec_i32_typed_hot_path()) {
        success = false;
    }

    if (!test_dec_i32_fallback_rehydrates_cache()) {
        success = false;
    }

    return success ? 0 : 1;
}
