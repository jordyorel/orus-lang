#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm/jit_backend.h"
#include "vm/jit_ir.h"
#include "vm/vm.h"

#define ASSERT_TRUE(cond, message)                                                   \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__,    \
                    __LINE__);                                                       \
            return false;                                                            \
        }                                                                            \
    } while (0)

static void init_parity_program(OrusJitIRProgram* program,
                                OrusJitIRInstruction* instructions,
                                size_t count) {
    memset(program, 0, sizeof(*program));
    memset(instructions, 0, sizeof(*instructions) * count);
    program->instructions = instructions;
    program->count = count;
    program->capacity = count;
}

static void build_sample_program(OrusJitIRProgram* program,
                                 OrusJitIRInstruction* instructions) {
    init_parity_program(program, instructions, 8u);

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_I32;
    instructions[0].operands.load_const.dst_reg = FRAME_REG_START;
    instructions[0].operands.load_const.immediate_bits = 1u;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_I32;
    instructions[1].operands.load_const.dst_reg = FRAME_REG_START + 1u;
    instructions[1].operands.load_const.immediate_bits = 2u;

    instructions[2].opcode = ORUS_JIT_IR_OP_ADD_I32;
    instructions[2].value_kind = ORUS_JIT_VALUE_I32;
    instructions[2].operands.arithmetic.dst_reg = FRAME_REG_START;
    instructions[2].operands.arithmetic.lhs_reg = FRAME_REG_START;
    instructions[2].operands.arithmetic.rhs_reg = FRAME_REG_START + 1u;

    instructions[3].opcode = ORUS_JIT_IR_OP_SAFEPOINT;
    instructions[3].value_kind = ORUS_JIT_VALUE_I32;

    instructions[4].opcode = ORUS_JIT_IR_OP_LT_I32;
    instructions[4].value_kind = ORUS_JIT_VALUE_I32;
    instructions[4].operands.arithmetic.lhs_reg = FRAME_REG_START;
    instructions[4].operands.arithmetic.rhs_reg = FRAME_REG_START + 1u;

    instructions[5].opcode = ORUS_JIT_IR_OP_I32_TO_F64;
    instructions[5].value_kind = ORUS_JIT_VALUE_F64;
    instructions[5].operands.unary.dst_reg = FRAME_REG_START + 2u;
    instructions[5].operands.unary.src_reg = FRAME_REG_START;

    instructions[6].opcode = ORUS_JIT_IR_OP_CONCAT_STRING;
    instructions[6].value_kind = ORUS_JIT_VALUE_STRING;
    instructions[6].operands.arithmetic.dst_reg = FRAME_REG_START + 3u;

    instructions[7].opcode = ORUS_JIT_IR_OP_RETURN;
    instructions[7].value_kind = ORUS_JIT_VALUE_I32;
}

static bool parity_reports_match(const OrusJitParityReport* lhs,
                                 const OrusJitParityReport* rhs) {
    return lhs->total_instructions == rhs->total_instructions &&
           lhs->arithmetic_ops == rhs->arithmetic_ops &&
           lhs->comparison_ops == rhs->comparison_ops &&
           lhs->helper_ops == rhs->helper_ops &&
           lhs->safepoints == rhs->safepoints &&
           lhs->conversion_ops == rhs->conversion_ops &&
           lhs->memory_ops == rhs->memory_ops &&
           lhs->value_kind_mask == rhs->value_kind_mask;
}

static bool test_cross_arch_parity(void) {
    initVM();

    OrusJitIRInstruction instructions[8];
    OrusJitIRProgram program;
    build_sample_program(&program, instructions);

    OrusJitParityReport reports[ORUS_JIT_BACKEND_TARGET_COUNT];
    memset(reports, 0, sizeof(reports));

    for (int target = 0; target < ORUS_JIT_BACKEND_TARGET_COUNT; ++target) {
        JITBackendStatus status = orus_jit_backend_collect_parity(
            &program, (OrusJitBackendTarget)target, &reports[target]);
        ASSERT_TRUE(status == JIT_BACKEND_OK,
                    "expected parity collection to succeed");
    }

    for (int target = 1; target < ORUS_JIT_BACKEND_TARGET_COUNT; ++target) {
        ASSERT_TRUE(parity_reports_match(&reports[0], &reports[target]),
                    "expected parity report to match reference");
    }

    ASSERT_TRUE((reports[0].value_kind_mask & (1u << ORUS_JIT_VALUE_I32)) != 0,
                "expected i32 coverage in parity mask");
    ASSERT_TRUE((reports[0].value_kind_mask & (1u << ORUS_JIT_VALUE_F64)) != 0,
                "expected f64 coverage in parity mask");
    ASSERT_TRUE((reports[0].value_kind_mask & (1u << ORUS_JIT_VALUE_STRING)) != 0,
                "expected string coverage in parity mask");

    freeVM();
    return true;
}

int main(void) {
    if (!test_cross_arch_parity()) {
        return 1;
    }
    return 0;
}
