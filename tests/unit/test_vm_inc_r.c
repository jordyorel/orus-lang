#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"

static void write_inc_instruction(Chunk* chunk, uint8_t opcode, uint8_t reg, const char* file_tag) {
    const int line = 1;
    const int column = 0;
    writeChunk(chunk, opcode, line, column, file_tag);
    writeChunk(chunk, reg, line, column, file_tag);
}

static void write_inc_program(Chunk* chunk, uint8_t opcode, uint8_t reg, int repeat, const char* file_tag) {
    for (int i = 0; i < repeat; ++i) {
        write_inc_instruction(chunk, opcode, reg, file_tag);
    }
    writeChunk(chunk, OP_HALT, 1, 0, file_tag);
}

static bool test_inc_i32_hot_path_persists(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    const uint16_t reg = FRAME_REG_START;
    write_inc_program(&chunk, OP_INC_I32_R, (uint8_t)reg, 3, "inc_i32_r");

    vm_store_i32_typed_hot(reg, 7);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_INC_I32_R hot path, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I32) {
        fprintf(stderr, "Expected register %u to remain typed as i32 after increments\n", reg);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected register %u to remain dirty after increments\n", reg);
        success = false;
        goto cleanup;
    }

    int32_t typed_value = 0;
    if (!vm_try_read_i32_typed(reg, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_i32_typed to hit for register %u\n", reg);
        success = false;
        goto cleanup;
    }

    if (typed_value != 10) {
        fprintf(stderr, "Expected typed register 0 to equal 10 after increments, got %d\n", typed_value);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected dirty flag to remain set after typed read\n");
        success = false;
        goto cleanup;
    }

    if (!IS_I32(vm.registers[reg]) || AS_I32(vm.registers[reg]) != 7) {
        fprintf(stderr, "Expected boxed register to remain stale at 7, got type %d value %d\n",
                vm.registers[reg].type, IS_I32(vm.registers[reg]) ? AS_I32(vm.registers[reg]) : -1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_i64_hot_path_persists(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    const uint16_t reg = FRAME_REG_START;
    write_inc_program(&chunk, OP_INC_I64_R, (uint8_t)reg, 4, "inc_i64_r");

    vm_store_i64_typed_hot(reg, 42);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_INC_I64_R hot path, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I64) {
        fprintf(stderr, "Expected register %u to remain typed as i64 after increments\n", reg);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected register %u to remain dirty after i64 increments\n", reg);
        success = false;
        goto cleanup;
    }

    int64_t typed_value = 0;
    if (!vm_try_read_i64_typed(reg, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_i64_typed to hit for register %u\n", reg);
        success = false;
        goto cleanup;
    }

    if (typed_value != 46) {
        fprintf(stderr, "Expected typed register 0 to equal 46 after increments, got %" PRId64 "\n", typed_value);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected dirty flag to remain set after typed i64 read\n");
        success = false;
        goto cleanup;
    }

    if (!IS_I64(vm.registers[reg]) || AS_I64(vm.registers[reg]) != 42) {
        fprintf(stderr, "Expected boxed register to remain stale at 42, got type %d value %" PRId64 "\n",
                vm.registers[reg].type, IS_I64(vm.registers[reg]) ? AS_I64(vm.registers[reg]) : (int64_t)-1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_u32_hot_path_persists(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    const uint16_t reg = FRAME_REG_START;
    write_inc_program(&chunk, OP_INC_U32_R, (uint8_t)reg, 5, "inc_u32_r");

    vm_store_u32_typed_hot(reg, 17u);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_INC_U32_R hot path, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_U32) {
        fprintf(stderr, "Expected register %u to remain typed as u32 after increments\n", reg);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected register %u to remain dirty after u32 increments\n", reg);
        success = false;
        goto cleanup;
    }

    uint32_t typed_value = 0;
    if (!vm_try_read_u32_typed(reg, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_u32_typed to hit for register %u\n", reg);
        success = false;
        goto cleanup;
    }

    if (typed_value != 22u) {
        fprintf(stderr, "Expected typed register 0 to equal 22 after increments, got %" PRIu32 "\n", typed_value);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected dirty flag to remain set after typed u32 read\n");
        success = false;
        goto cleanup;
    }

    if (!IS_U32(vm.registers[reg]) || AS_U32(vm.registers[reg]) != 17u) {
        fprintf(stderr, "Expected boxed register to remain stale at 17, got type %d value %" PRIu32 "\n",
                vm.registers[reg].type, IS_U32(vm.registers[reg]) ? AS_U32(vm.registers[reg]) : 0u);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_inc_u64_hot_path_persists(void) {
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    const uint16_t reg = FRAME_REG_START;
    write_inc_program(&chunk, OP_INC_U64_R, (uint8_t)reg, 6, "inc_u64_r");

    vm_store_u64_typed_hot(reg, 100ull);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();

    bool success = true;
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK for OP_INC_U64_R hot path, got %d\n", result);
        success = false;
        goto cleanup;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_U64) {
        fprintf(stderr, "Expected register %u to remain typed as u64 after increments\n", reg);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected register %u to remain dirty after u64 increments\n", reg);
        success = false;
        goto cleanup;
    }

    uint64_t typed_value = 0;
    if (!vm_try_read_u64_typed(reg, &typed_value)) {
        fprintf(stderr, "Expected vm_try_read_u64_typed to hit for register %u\n", reg);
        success = false;
        goto cleanup;
    }

    if (typed_value != 106ull) {
        fprintf(stderr,
                "Expected typed register 0 to equal 106 after increments, got %llu\n",
                (unsigned long long)typed_value);
        success = false;
        goto cleanup;
    }

    if (!vm.typed_regs.dirty[reg]) {
        fprintf(stderr, "Expected dirty flag to remain set after typed u64 read\n");
        success = false;
        goto cleanup;
    }

    if (!IS_U64(vm.registers[reg]) || AS_U64(vm.registers[reg]) != 100ull) {
        fprintf(stderr,
                "Expected boxed register to remain stale at 100, got type %d value %llu\n",
                vm.registers[reg].type,
                (unsigned long long)(IS_U64(vm.registers[reg]) ? AS_U64(vm.registers[reg]) : 0ull));
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
        {"OP_INC_I32_R keeps typed cache dirty", test_inc_i32_hot_path_persists},
        {"OP_INC_I64_R keeps typed cache dirty", test_inc_i64_hot_path_persists},
        {"OP_INC_U32_R keeps typed cache dirty", test_inc_u32_hot_path_persists},
        {"OP_INC_U64_R keeps typed cache dirty", test_inc_u64_hot_path_persists},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        printf("[ RUN      ] %s\n", tests[i].name);
        if (tests[i].fn()) {
            printf("[     OK  ] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[ FAILED ] %s\n", tests[i].name);
        }
    }

    printf("%d/%d OP_INC_* hot-path cache tests passed\n", passed, total);

    return passed == total ? 0 : 1;
}
