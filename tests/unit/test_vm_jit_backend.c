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

    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.count = 1;

    instructions[0].opcode = ORUS_JIT_IR_OP_ADD_I32;
    instructions[0].value_kind = ORUS_JIT_VALUE_I32;
    instructions[0].operands.arithmetic.dst_reg = dst0;
    instructions[0].operands.arithmetic.lhs_reg = dst0;
    instructions[0].operands.arithmetic.rhs_reg = dst1;

    instructions[1].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 2);
    program.source_chunk = &chunk;

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

static bool run_fused_loop_case(OrusJitValueKind kind,
                                bool is_increment,
                                int64_t start_value,
                                int64_t limit_value,
                                uint64_t expected_iterations) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    if (!backend) {
        freeVM();
        return false;
    }

    const uint16_t counter_reg = FRAME_REG_START;
    const uint16_t limit_reg = FRAME_REG_START + 1u;
    const uint16_t acc_reg = FRAME_REG_START + 2u;
    const uint16_t step_reg = FRAME_REG_START + 3u;

    const uint32_t load_counter_offset = 0u;
    const uint32_t load_limit_offset = 4u;
    const uint32_t load_acc_offset = 8u;
    const uint32_t load_step_offset = 12u;
    const uint32_t body_offset = 16u;
    const uint32_t fused_offset = 20u;
    const uint32_t return_offset = fused_offset + 5u;
    const int16_t jump_offset = (int16_t)((int32_t)body_offset - (int32_t)return_offset);

    OrusJitIROpcode load_opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    OrusJitIROpcode add_opcode = ORUS_JIT_IR_OP_ADD_I32;
    uint8_t reg_type_tag = (uint8_t)REG_TYPE_I32;
    uint64_t start_bits = 0u;
    uint64_t limit_bits = 0u;
    uint64_t step_bits = 0u;

    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            load_opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_I32;
            reg_type_tag = (uint8_t)REG_TYPE_I32;
            start_bits = (uint64_t)(uint32_t)start_value;
            limit_bits = (uint64_t)(uint32_t)limit_value;
            step_bits = (uint64_t)(uint32_t)1;
            break;
        case ORUS_JIT_VALUE_I64:
            load_opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_I64;
            reg_type_tag = (uint8_t)REG_TYPE_I64;
            start_bits = (uint64_t)start_value;
            limit_bits = (uint64_t)limit_value;
            step_bits = (uint64_t)1;
            break;
        case ORUS_JIT_VALUE_U32:
            load_opcode = ORUS_JIT_IR_OP_LOAD_U32_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_U32;
            reg_type_tag = (uint8_t)REG_TYPE_U32;
            start_bits = (uint64_t)(uint32_t)start_value;
            limit_bits = (uint64_t)(uint32_t)limit_value;
            step_bits = (uint64_t)(uint32_t)1u;
            break;
        case ORUS_JIT_VALUE_U64:
            load_opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_U64;
            reg_type_tag = (uint8_t)REG_TYPE_U64;
            start_bits = (uint64_t)start_value;
            limit_bits = (uint64_t)limit_value;
            step_bits = (uint64_t)1u;
            break;
        default:
            orus_jit_backend_destroy(backend);
            freeVM();
            return false;
    }

    OrusJitIRInstruction instructions[7];
    memset(instructions, 0, sizeof(instructions));

    vm.typed_regs.reg_types[counter_reg] = reg_type_tag;
    vm.typed_regs.reg_types[limit_reg] = reg_type_tag;
    vm.typed_regs.reg_types[acc_reg] = reg_type_tag;
    vm.typed_regs.reg_types[step_reg] = reg_type_tag;

    instructions[0].opcode = load_opcode;
    instructions[0].value_kind = kind;
    instructions[0].bytecode_offset = load_counter_offset;
    instructions[0].operands.load_const.dst_reg = counter_reg;
    instructions[0].operands.load_const.immediate_bits = start_bits;

    instructions[1].opcode = load_opcode;
    instructions[1].value_kind = kind;
    instructions[1].bytecode_offset = load_limit_offset;
    instructions[1].operands.load_const.dst_reg = limit_reg;
    instructions[1].operands.load_const.immediate_bits = limit_bits;

    instructions[2].opcode = load_opcode;
    instructions[2].value_kind = kind;
    instructions[2].bytecode_offset = load_acc_offset;
    instructions[2].operands.load_const.dst_reg = acc_reg;
    instructions[2].operands.load_const.immediate_bits = 0u;

    instructions[3].opcode = load_opcode;
    instructions[3].value_kind = kind;
    instructions[3].bytecode_offset = load_step_offset;
    instructions[3].operands.load_const.dst_reg = step_reg;
    instructions[3].operands.load_const.immediate_bits = step_bits;

    instructions[4].opcode = add_opcode;
    instructions[4].value_kind = kind;
    instructions[4].bytecode_offset = body_offset;
    instructions[4].operands.arithmetic.dst_reg = acc_reg;
    instructions[4].operands.arithmetic.lhs_reg = acc_reg;
    instructions[4].operands.arithmetic.rhs_reg = step_reg;

    instructions[5].opcode =
        is_increment ? ORUS_JIT_IR_OP_INC_CMP_JUMP : ORUS_JIT_IR_OP_DEC_CMP_JUMP;
    instructions[5].value_kind = kind;
    instructions[5].bytecode_offset = fused_offset;
    instructions[5].operands.fused_loop.counter_reg = counter_reg;
    instructions[5].operands.fused_loop.limit_reg = limit_reg;
    instructions[5].operands.fused_loop.jump_offset = jump_offset;
    instructions[5].operands.fused_loop.step =
        (int8_t)(is_increment ? ORUS_JIT_IR_LOOP_STEP_INCREMENT
                              : ORUS_JIT_IR_LOOP_STEP_DECREMENT);
    instructions[5].operands.fused_loop.compare_kind =
        (uint8_t)(is_increment ? ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN
                               : ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN);

    instructions[6].opcode = ORUS_JIT_IR_OP_RETURN;
    instructions[6].bytecode_offset = return_offset;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 7);
    program.loop_start_offset = body_offset;

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    entry.entry_point(&vm);

    bool success = true;
    switch (kind) {
        case ORUS_JIT_VALUE_I32: {
            int32_t expected_counter = (int32_t)limit_value;
            int32_t expected_limit = (int32_t)limit_value;
            int32_t expected_acc = (int32_t)expected_iterations;
            if (vm.typed_regs.i32_regs[counter_reg] != expected_counter) {
                fprintf(stderr,
                        "fused loop counter mismatch: got %d expected %d\n",
                        vm.typed_regs.i32_regs[counter_reg], expected_counter);
                success = false;
            }
            if (vm.typed_regs.i32_regs[limit_reg] != expected_limit) {
                fprintf(stderr,
                        "fused loop limit clobbered: got %d expected %d\n",
                        vm.typed_regs.i32_regs[limit_reg], expected_limit);
                success = false;
            }
            if (vm.typed_regs.i32_regs[acc_reg] != expected_acc) {
                fprintf(stderr,
                        "fused loop accumulator mismatch: got %d expected %d\n",
                        vm.typed_regs.i32_regs[acc_reg], expected_acc);
                success = false;
            }
            break;
        }
        case ORUS_JIT_VALUE_I64: {
            int64_t expected_counter = limit_value;
            int64_t expected_limit = limit_value;
            int64_t expected_acc = (int64_t)expected_iterations;
            if (vm.typed_regs.i64_regs[counter_reg] != expected_counter) {
                fprintf(stderr,
                        "fused loop counter mismatch: got %lld expected %lld\n",
                        (long long)vm.typed_regs.i64_regs[counter_reg],
                        (long long)expected_counter);
                success = false;
            }
            if (vm.typed_regs.i64_regs[limit_reg] != expected_limit) {
                fprintf(stderr,
                        "fused loop limit clobbered: got %lld expected %lld\n",
                        (long long)vm.typed_regs.i64_regs[limit_reg],
                        (long long)expected_limit);
                success = false;
            }
            if (vm.typed_regs.i64_regs[acc_reg] != expected_acc) {
                fprintf(stderr,
                        "fused loop accumulator mismatch: got %lld expected %lld\n",
                        (long long)vm.typed_regs.i64_regs[acc_reg],
                        (long long)expected_acc);
                success = false;
            }
            break;
        }
        case ORUS_JIT_VALUE_U32: {
            uint32_t expected_counter = (uint32_t)limit_value;
            uint32_t expected_limit = (uint32_t)limit_value;
            uint32_t expected_acc = (uint32_t)expected_iterations;
            if (vm.typed_regs.u32_regs[counter_reg] != expected_counter) {
                fprintf(stderr,
                        "fused loop counter mismatch: got %u expected %u\n",
                        vm.typed_regs.u32_regs[counter_reg], expected_counter);
                success = false;
            }
            if (vm.typed_regs.u32_regs[limit_reg] != expected_limit) {
                fprintf(stderr,
                        "fused loop limit clobbered: got %u expected %u\n",
                        vm.typed_regs.u32_regs[limit_reg], expected_limit);
                success = false;
            }
            if (vm.typed_regs.u32_regs[acc_reg] != expected_acc) {
                fprintf(stderr,
                        "fused loop accumulator mismatch: got %u expected %u\n",
                        vm.typed_regs.u32_regs[acc_reg], expected_acc);
                success = false;
            }
            break;
        }
        case ORUS_JIT_VALUE_U64: {
            uint64_t expected_counter = (uint64_t)limit_value;
            uint64_t expected_limit = (uint64_t)limit_value;
            uint64_t expected_acc = (uint64_t)expected_iterations;
            if (vm.typed_regs.u64_regs[counter_reg] != expected_counter) {
                fprintf(stderr,
                        "fused loop counter mismatch: got %llu expected %llu\n",
                        (unsigned long long)vm.typed_regs.u64_regs[counter_reg],
                        (unsigned long long)expected_counter);
                success = false;
            }
            if (vm.typed_regs.u64_regs[limit_reg] != expected_limit) {
                fprintf(stderr,
                        "fused loop limit clobbered: got %llu expected %llu\n",
                        (unsigned long long)vm.typed_regs.u64_regs[limit_reg],
                        (unsigned long long)expected_limit);
                success = false;
            }
            if (vm.typed_regs.u64_regs[acc_reg] != expected_acc) {
                fprintf(stderr,
                        "fused loop accumulator mismatch: got %llu expected %llu\n",
                        (unsigned long long)vm.typed_regs.u64_regs[acc_reg],
                        (unsigned long long)expected_acc);
                success = false;
            }
            break;
        }
        default:
            success = false;
            break;
    }

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    return success;
}

static bool test_backend_emits_fused_increment_loops(void) {
    bool success = true;
    success &= run_fused_loop_case(ORUS_JIT_VALUE_I32, true, 0, 4, 4);
    success &= run_fused_loop_case(ORUS_JIT_VALUE_I64, true, 5, 9, 4);
    success &= run_fused_loop_case(ORUS_JIT_VALUE_U32, true, 1, 5, 4);
    success &= run_fused_loop_case(ORUS_JIT_VALUE_U64, true, 2, 6, 4);
    if (!success) {
        fprintf(stderr,
                "incrementing fused loop backend test failed for at least one kind\n");
    }
    return success;
}

static bool test_backend_emits_fused_decrement_loops(void) {
    bool success = true;
    success &= run_fused_loop_case(ORUS_JIT_VALUE_I32, false, 4, 0, 4);
    success &= run_fused_loop_case(ORUS_JIT_VALUE_I64, false, 3, -1, 4);
    success &= run_fused_loop_case(ORUS_JIT_VALUE_U32, false, 5, 1, 4);
    success &= run_fused_loop_case(ORUS_JIT_VALUE_U64, false, 8, 4, 4);
    if (!success) {
        fprintf(stderr,
                "decrementing fused loop backend test failed for at least one kind\n");
    }
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
    if (!test_backend_emits_fused_increment_loops()) {
        fprintf(stderr, "backend fused increment loop test failed\n");
        success = false;
    }
    if (!test_backend_emits_fused_decrement_loops()) {
        fprintf(stderr, "backend fused decrement loop test failed\n");
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
