#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/jit_backend.h"
#include "vm/jit_ir.h"
#include "vm/jit_translation.h"
#include "vm/vm.h"
#include "vm/vm_comparison.h"

#define ASSERT_TRUE(cond, message)                                                   \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__,    \
                    __LINE__);                                                       \
            return false;                                                            \
        }                                                                            \
    } while (0)

static void init_ir_program(OrusJitIRProgram* program,
                            OrusJitIRInstruction* instructions,
                            size_t count) {
    memset(program, 0, sizeof(*program));
    program->instructions = instructions;
    program->count = count;
    program->capacity = count;
    program->function_index = 0;
    program->loop_index = 0;
}

static bool compile_program(struct OrusJitBackend* backend,
                            OrusJitIRProgram* program,
                            JITEntry* entry) {
    memset(entry, 0, sizeof(*entry));
    JITBackendStatus status =
        orus_jit_backend_compile_ir(backend, program, entry);
    if (status != JIT_BACKEND_OK) {
        fprintf(stderr, "orus_jit_backend_compile_ir failed: %d\n", status);
        return false;
    }
    return true;
}

static void force_helper_stub_env_on(void) {
#if defined(_WIN32)
    _putenv("ORUS_JIT_FORCE_HELPER_STUB=1");
#else
    setenv("ORUS_JIT_FORCE_HELPER_STUB", "1", 1);
#endif
}

static void force_helper_stub_env_off(void) {
#if defined(_WIN32)
    _putenv("ORUS_JIT_FORCE_HELPER_STUB=");
#else
    unsetenv("ORUS_JIT_FORCE_HELPER_STUB");
#endif
}

static bool test_backend_helper_stub_executes(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    force_helper_stub_env_on();

    struct OrusJitBackend* backend = orus_jit_backend_create();
    if (!backend) {
        force_helper_stub_env_off();
        freeVM();
        return false;
    }

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[2];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_ADD_I32;
    instructions[0].value_kind = ORUS_JIT_VALUE_I32;
    instructions[0].operands.arithmetic.dst_reg = dst0;
    instructions[0].operands.arithmetic.lhs_reg = dst0;
    instructions[0].operands.arithmetic.rhs_reg = dst1;

    instructions[1].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 2);

    JITEntry entry;
    bool success = compile_program(backend, &program, &entry);
    if (!success) {
        force_helper_stub_env_off();
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    if (!entry.debug_name || strcmp(entry.debug_name, "orus_jit_helper_stub") != 0) {
        fprintf(stderr, "expected helper stub, got %s\n",
                entry.debug_name ? entry.debug_name : "(null)");
        orus_jit_backend_release_entry(backend, &entry);
        orus_jit_backend_destroy(backend);
        force_helper_stub_env_off();
        freeVM();
        return false;
    }

    vm_store_i32_typed_hot(dst0, 42);
    vm_store_i32_typed_hot(dst1, 8);

    entry.entry_point(&vm);

    if (vm.typed_regs.i32_regs[dst0] != 50) {
        fprintf(stderr, "helper stub produced %d (expected 50)\n",
                vm.typed_regs.i32_regs[dst0]);
        success = false;
    } else {
        success = true;
    }

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    force_helper_stub_env_off();
    freeVM();
    return success;
}

static bool test_backend_emits_i64_add(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_I64;
    instructions[0].operands.load_const.dst_reg = dst0;
    instructions[0].operands.load_const.immediate_bits = (uint64_t)42;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_I64;
    instructions[1].operands.load_const.dst_reg = dst1;
    instructions[1].operands.load_const.immediate_bits = (uint64_t)8;

    instructions[2].opcode = ORUS_JIT_IR_OP_ADD_I64;
    instructions[2].value_kind = ORUS_JIT_VALUE_I64;
    instructions[2].operands.arithmetic.dst_reg = dst0;
    instructions[2].operands.arithmetic.lhs_reg = dst0;
    instructions[2].operands.arithmetic.rhs_reg = dst1;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    vm_store_i64_typed_hot(dst0, 0);
    vm_store_i64_typed_hot(dst1, 0);

    entry.entry_point(&vm);

    bool success = (vm.typed_regs.i64_regs[dst0] == 50);

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

static bool test_backend_emits_u32_add(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_U32_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_U32;
    instructions[0].operands.load_const.dst_reg = dst0;
    instructions[0].operands.load_const.immediate_bits = (uint64_t)100u;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_U32_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_U32;
    instructions[1].operands.load_const.dst_reg = dst1;
    instructions[1].operands.load_const.immediate_bits = (uint64_t)200u;

    instructions[2].opcode = ORUS_JIT_IR_OP_ADD_U32;
    instructions[2].value_kind = ORUS_JIT_VALUE_U32;
    instructions[2].operands.arithmetic.dst_reg = dst0;
    instructions[2].operands.arithmetic.lhs_reg = dst0;
    instructions[2].operands.arithmetic.rhs_reg = dst1;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    vm_store_u32_typed_hot(dst0, 0u);
    vm_store_u32_typed_hot(dst1, 0u);

    entry.entry_point(&vm);

    bool success = (vm.typed_regs.u32_regs[dst0] == 300u);

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

static bool test_backend_emits_u64_add(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_U64;
    instructions[0].operands.load_const.dst_reg = dst0;
    instructions[0].operands.load_const.immediate_bits = (uint64_t)5000000000ULL;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_U64;
    instructions[1].operands.load_const.dst_reg = dst1;
    instructions[1].operands.load_const.immediate_bits = (uint64_t)7ULL;

    instructions[2].opcode = ORUS_JIT_IR_OP_ADD_U64;
    instructions[2].value_kind = ORUS_JIT_VALUE_U64;
    instructions[2].operands.arithmetic.dst_reg = dst0;
    instructions[2].operands.arithmetic.lhs_reg = dst0;
    instructions[2].operands.arithmetic.rhs_reg = dst1;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    vm_store_u64_typed_hot(dst0, 0u);
    vm_store_u64_typed_hot(dst1, 0u);

    entry.entry_point(&vm);

    bool success = (vm.typed_regs.u64_regs[dst0] == 5000000007ULL);

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

static bool test_backend_emits_string_concat(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;
    const uint16_t dst2 = FRAME_REG_START + 2u;

    ObjString* left = allocateString("a", 1);
    ObjString* right = allocateString("b", 1);
    ASSERT_TRUE(left != NULL && right != NULL, "expected string allocation");

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_STRING_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_STRING;
    instructions[0].operands.load_const.dst_reg = dst0;
    instructions[0].operands.load_const.immediate_bits =
        (uint64_t)(uintptr_t)left;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_STRING_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_STRING;
    instructions[1].operands.load_const.dst_reg = dst1;
    instructions[1].operands.load_const.immediate_bits =
        (uint64_t)(uintptr_t)right;

    instructions[2].opcode = ORUS_JIT_IR_OP_CONCAT_STRING;
    instructions[2].value_kind = ORUS_JIT_VALUE_STRING;
    instructions[2].operands.arithmetic.dst_reg = dst2;
    instructions[2].operands.arithmetic.lhs_reg = dst0;
    instructions[2].operands.arithmetic.rhs_reg = dst1;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    entry.entry_point(&vm);

    Value result = vm_get_register_safe(dst2);
    bool success = IS_STRING(result) &&
                   strcmp(string_get_chars(AS_STRING(result)), "ab") == 0;

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

static bool test_backend_emits_i32_to_i64_conversion(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    const uint16_t src = FRAME_REG_START;
    const uint16_t dst = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[2];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_I32_TO_I64;
    instructions[0].value_kind = ORUS_JIT_VALUE_I64;
    instructions[0].operands.unary.dst_reg = dst;
    instructions[0].operands.unary.src_reg = src;

    instructions[1].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 2);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    vm_store_i32_typed_hot(src, 42);

    entry.entry_point(&vm);

    bool success = vm.typed_regs.i64_regs[dst] == 42;

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

static bool test_backend_emits_f64_mul(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    double lhs = 1.5;
    double rhs = 2.0;
    double expected = lhs * rhs;

    uint64_t lhs_bits = 0u;
    uint64_t rhs_bits = 0u;
    memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
    memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_F64_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_F64;
    instructions[0].operands.load_const.dst_reg = dst0;
    instructions[0].operands.load_const.immediate_bits = lhs_bits;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_F64_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_F64;
    instructions[1].operands.load_const.dst_reg = dst1;
    instructions[1].operands.load_const.immediate_bits = rhs_bits;

    instructions[2].opcode = ORUS_JIT_IR_OP_MUL_F64;
    instructions[2].value_kind = ORUS_JIT_VALUE_F64;
    instructions[2].operands.arithmetic.dst_reg = dst0;
    instructions[2].operands.arithmetic.lhs_reg = dst0;
    instructions[2].operands.arithmetic.rhs_reg = dst1;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    vm_store_f64_typed_hot(dst0, 0.0);
    vm_store_f64_typed_hot(dst1, 0.0);

    entry.entry_point(&vm);

    double result = vm.typed_regs.f64_regs[dst0];
    bool success = fabs(result - expected) < 1e-9;

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

int main(void) {
    if (!orus_jit_backend_is_available()) {
        printf("Baseline JIT backend unavailable; skipping backend tests.\n");
        return 0;
    }

    bool success = true;

    if (!test_backend_helper_stub_executes()) {
        fprintf(stderr, "backend helper stub test failed\n");
        success = false;
    }
    if (!test_backend_emits_i64_add()) {
        fprintf(stderr, "backend i64 add test failed\n");
        success = false;
    }
    if (!test_backend_emits_u32_add()) {
        fprintf(stderr, "backend u32 add test failed\n");
        success = false;
    }
    if (!test_backend_emits_u64_add()) {
        fprintf(stderr, "backend u64 add test failed\n");
        success = false;
    }
    if (!test_backend_emits_f64_mul()) {
        fprintf(stderr, "backend f64 mul test failed\n");
        success = false;
    }
    if (!test_backend_emits_string_concat()) {
        fprintf(stderr, "backend string concat test failed\n");
        success = false;
    }
    if (!test_backend_emits_i32_to_i64_conversion()) {
        fprintf(stderr, "backend i32->i64 conversion test failed\n");
        success = false;
    }

    return success ? 0 : 1;
}
