#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/jit_ir.h"
#include "vm/jit_translation.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include "vm/vm_tiering.h"

#ifndef OP_EQ_I32_TYPED
#define OP_EQ_I32_TYPED OP_EQ_R
#endif

#define ASSERT_TRUE(cond, message)                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__,      \
                    __LINE__);                                                         \
            return false;                                                              \
        }                                                                              \
    } while (0)

static void init_function(Function* function, Chunk* chunk) {
    memset(function, 0, sizeof(*function));
    function->chunk = chunk;
    function->tier = FUNCTION_TIER_BASELINE;
    function->start = 0;
}

static bool write_load_numeric_const(Chunk* chunk,
                                     uint8_t opcode,
                                     uint16_t dst,
                                     Value value) {
    const int line = 1;
    const int column = 0;
    const char* file_tag = "jit_translation";

    int constant_index = addConstant(chunk, value);
    if (constant_index < 0) {
        return false;
    }

    writeChunk(chunk, opcode, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst, line, column, file_tag);
    writeChunk(chunk, (uint8_t)((constant_index >> 8) & 0xFF), line, column, file_tag);
    writeChunk(chunk, (uint8_t)(constant_index & 0xFF), line, column, file_tag);
    return true;
}

static bool test_translates_i64_linear_loop(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I64_CONST, dst0,
                                         I64_VAL(42)),
                "expected OP_LOAD_I64_CONST to be emitted");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I64_CONST, dst1,
                                         I64_VAL(8)),
                "expected second OP_LOAD_I64_CONST");

    writeChunk(&chunk, OP_ADD_I64_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst0, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst0, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst1, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 4, "expected at least four IR instructions");
    ASSERT_TRUE(program.instructions[0].opcode == ORUS_JIT_IR_OP_LOAD_I64_CONST,
                "first instruction should load i64 const");
    ASSERT_TRUE(program.instructions[0].value_kind == ORUS_JIT_VALUE_I64,
                "first instruction should be tagged as i64");
    ASSERT_TRUE(program.instructions[1].opcode == ORUS_JIT_IR_OP_LOAD_I64_CONST,
                "second instruction should load i64 const");
    ASSERT_TRUE(program.instructions[2].opcode == ORUS_JIT_IR_OP_ADD_I64,
                "third instruction should add i64 values");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translator_promotes_i32_constants_to_i64(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t lhs = FRAME_REG_START;
    const uint16_t rhs = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, lhs,
                                         I32_VAL(7)),
                "expected lhs i32 constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, rhs,
                                         I32_VAL(9)),
                "expected rhs i32 constant");

    writeChunk(&chunk, OP_ADD_I64_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)rhs, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected promotion failure: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 4u,
                "expected promoted program to contain at least four ops");
    ASSERT_TRUE(program.instructions[0].opcode == ORUS_JIT_IR_OP_LOAD_I64_CONST,
                "lhs constant should be promoted to i64 load");
    ASSERT_TRUE(program.instructions[0].value_kind == ORUS_JIT_VALUE_I64,
                "lhs load should advertise i64 kind");
    ASSERT_TRUE(program.instructions[1].opcode == ORUS_JIT_IR_OP_LOAD_I64_CONST,
                "rhs constant should be promoted to i64 load");
    ASSERT_TRUE(program.instructions[1].value_kind == ORUS_JIT_VALUE_I64,
                "rhs load should advertise i64 kind");
    ASSERT_TRUE(program.instructions[2].opcode == ORUS_JIT_IR_OP_ADD_I64,
                "arithmetic should use widened i64 opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_f64_stream(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_F64_CONST, dst0,
                                         F64_VAL(1.5)),
                "expected first f64 constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_F64_CONST, dst1,
                                         F64_VAL(2.5)),
                "expected second f64 constant");

    writeChunk(&chunk, OP_MUL_F64_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst0, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst0, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst1, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 4, "expected at least four IR instructions");
    ASSERT_TRUE(program.instructions[0].opcode == ORUS_JIT_IR_OP_LOAD_F64_CONST,
                "first instruction should load f64 const");
    ASSERT_TRUE(program.instructions[0].value_kind == ORUS_JIT_VALUE_F64,
                "first instruction should be tagged as f64");
    ASSERT_TRUE(program.instructions[2].opcode == ORUS_JIT_IR_OP_MUL_F64,
                "third instruction should multiply f64 values");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_boxed_bool_constant(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t dst0 = FRAME_REG_START;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_CONST, dst0,
                                         BOOL_VAL(true)),
                "expected boxed constant emission");
    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Expected boxed constant translation success, got %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 1, "expected at least one IR instruction");
    ASSERT_TRUE(program.instructions[0].opcode ==
                    ORUS_JIT_IR_OP_LOAD_VALUE_CONST,
                "first instruction should load boxed const");
    ASSERT_TRUE(program.instructions[0].value_kind == ORUS_JIT_VALUE_BOOL,
                "boxed bool should record bool kind");
    ASSERT_TRUE(program.instructions[0].operands.load_const.dst_reg == dst0,
                "load should target dst0");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_string_concat(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    ObjString* left = allocateString("a", 1);
    ObjString* right = allocateString("b", 1);
    ASSERT_TRUE(left != NULL && right != NULL, "expected string allocation");

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;
    const uint16_t dst2 = FRAME_REG_START + 2u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_CONST, dst0,
                                         STRING_VAL(left)),
                "expected first string load");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_CONST, dst1,
                                         STRING_VAL(right)),
                "expected second string load");

    writeChunk(&chunk, OP_CONCAT_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst2, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst0, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst1, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected translation failure for string concat: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 4, "expected concat instructions");
    ASSERT_TRUE(program.instructions[0].opcode == ORUS_JIT_IR_OP_LOAD_STRING_CONST,
                "first instruction should load string const");
    ASSERT_TRUE(program.instructions[1].opcode == ORUS_JIT_IR_OP_LOAD_STRING_CONST,
                "second instruction should load string const");
    ASSERT_TRUE(program.instructions[2].opcode == ORUS_JIT_IR_OP_CONCAT_STRING,
                "third instruction should concat strings");
    ASSERT_TRUE(program.instructions[2].operands.arithmetic.dst_reg == dst2,
                "concat should target dst2");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_type_builtins(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t value_reg = FRAME_REG_START;
    const uint16_t typeof_reg = FRAME_REG_START + 1u;
    const uint16_t type_identifier_reg = FRAME_REG_START + 2u;
    const uint16_t predicate_reg = FRAME_REG_START + 3u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, value_reg,
                                         I32_VAL(42)),
                "expected i32 constant load");

    writeChunk(&chunk, OP_TYPE_OF_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)typeof_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");

    ObjString* type_name = allocateString("int", 3);
    ASSERT_TRUE(type_name != NULL, "expected type name allocation");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_CONST, type_identifier_reg,
                                         STRING_VAL(type_name)),
                "expected string constant load");

    writeChunk(&chunk, OP_IS_TYPE_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)type_identifier_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected translation failure for type helpers: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_typeof = false;
    bool saw_is_type = false;
    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_TYPE_OF) {
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_STRING,
                        "typeof should yield string kind");
            ASSERT_TRUE(inst->operands.type_of.dst_reg == typeof_reg,
                        "typeof destination mismatch");
            ASSERT_TRUE(inst->operands.type_of.value_reg == value_reg,
                        "typeof source mismatch");
            saw_typeof = true;
        } else if (inst->opcode == ORUS_JIT_IR_OP_IS_TYPE) {
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_BOOL,
                        "istype should yield bool kind");
            ASSERT_TRUE(inst->operands.is_type.dst_reg == predicate_reg,
                        "istype destination mismatch");
            ASSERT_TRUE(inst->operands.is_type.value_reg == value_reg,
                        "istype value register mismatch");
            ASSERT_TRUE(inst->operands.is_type.type_reg == type_identifier_reg,
                        "istype type register mismatch");
            saw_is_type = true;
        }
    }

    ASSERT_TRUE(saw_typeof, "expected typeof IR opcode");
    ASSERT_TRUE(saw_is_type, "expected istype IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_i32_to_i64_conversion(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t src_reg = FRAME_REG_START;
    const uint16_t dst_reg = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, src_reg,
                                         I32_VAL(7)),
                "expected i32 constant load");

    writeChunk(&chunk, OP_I32_TO_I64_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)src_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected translation failure for i32->i64: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 3, "expected conversion instructions");
    ASSERT_TRUE(program.instructions[1].opcode == ORUS_JIT_IR_OP_I32_TO_I64,
                "second instruction should be conversion");
    ASSERT_TRUE(program.instructions[1].operands.unary.dst_reg == dst_reg,
                "conversion should target dst register");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_u32_to_i32_conversion(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t src_reg = FRAME_REG_START;
    const uint16_t dst_reg = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_U32_CONST, src_reg,
                                         U32_VAL(1234u)),
                "expected u32 constant load");

    writeChunk(&chunk, OP_U32_TO_I32_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)src_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected translation failure for u32->i32: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    ASSERT_TRUE(program.count >= 3, "expected conversion instructions");
    ASSERT_TRUE(program.instructions[1].opcode == ORUS_JIT_IR_OP_U32_TO_I32,
                "second instruction should be conversion");
    ASSERT_TRUE(program.instructions[1].operands.unary.dst_reg == dst_reg,
                "conversion should target dst register");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_rollout_blocks_f64_before_stage(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_WIDE_INTS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t dst0 = FRAME_REG_START;
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_F64_CONST, dst0,
                                         F64_VAL(3.25)),
                "expected f64 constant load");
    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = (result.status == ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED);
    if (!success) {
        fprintf(stderr,
                "Expected rollout-disabled status, received %s (%d)\n",
                orus_jit_translation_status_name(result.status),
                result.status);
    }

    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_queue_tier_up_skips_stub_install_on_unsupported(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    ASSERT_TRUE(chunk != NULL, "expected chunk allocation");
    initChunk(chunk);

    Function* function = &vm.functions[0];
    memset(function, 0, sizeof(*function));
    function->chunk = chunk;
    function->tier = FUNCTION_TIER_BASELINE;
    function->start = 0;
    vm.functionCount = 1;

    const uint16_t dst = FRAME_REG_START;
    ASSERT_TRUE(write_load_numeric_const(chunk, OP_LOAD_I32_CONST, dst,
                                         BOOL_VAL(true)),
                "expected constant emission");
    writeChunk(chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function->start;

    queue_tier_up(&vm, &sample);

    ASSERT_TRUE(vm.jit_loop_blocklist[sample.loop],
                "expected loop to be blocklisted");
    ASSERT_TRUE(vm.jit_cache.count == 0,
                "expected jit cache to stay empty");
    ASSERT_TRUE(vm_jit_lookup_entry(sample.func, sample.loop) == NULL,
                "expected no cache entry to be installed");
    ASSERT_TRUE(vm.jit_compilation_count == 0,
                "expected compilation count to remain zero");
    ASSERT_TRUE(vm.jit_translation_failures.total_failures == 1,
                "expected one translation failure to be recorded");
    ASSERT_TRUE(
        vm.jit_translation_failures
                .reason_counts[ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND] == 1,
        "expected unsupported constant counter to increment");

    freeVM();
    return true;
}

static bool test_queue_tier_up_counts_only_native_entries(void) {
    const char* previous_env = getenv("ORUS_JIT_FORCE_HELPER_STUB");
    char* previous_copy = NULL;
    if (previous_env) {
        size_t len = strlen(previous_env);
        previous_copy = (char*)malloc(len + 1u);
        if (previous_copy) {
            memcpy(previous_copy, previous_env, len + 1u);
        }
    }
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

#ifdef _WIN32
    _putenv("ORUS_JIT_FORCE_HELPER_STUB=1");
#else
    setenv("ORUS_JIT_FORCE_HELPER_STUB", "1", 1);
#endif

    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    bool success = true;
    if (!chunk) {
        fprintf(stderr, "expected chunk allocation\n");
        success = false;
        goto cleanup;
    }
    initChunk(chunk);

    Function* function = &vm.functions[0];
    memset(function, 0, sizeof(*function));
    function->chunk = chunk;
    function->tier = FUNCTION_TIER_BASELINE;
    function->start = 0;
    vm.functionCount = 1;

    const uint16_t dst = FRAME_REG_START;
    if (!write_load_numeric_const(chunk, OP_LOAD_I64_CONST, dst,
                                  I64_VAL(1234))) {
        fprintf(stderr, "expected constant emission\n");
        success = false;
        goto cleanup;
    }
    writeChunk(chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function->start;

    uint64_t base_compilations = vm.jit_compilation_count;

    queue_tier_up(&vm, &sample);

    if (!vm.jit_loop_blocklist[sample.loop]) {
        fprintf(stderr, "expected helper-stub-only loop to be blocklisted\n");
        success = false;
    }
    if (vm.jit_compilation_count != base_compilations) {
        fprintf(stderr, "expected compilation count to remain unchanged\n");
        success = false;
    }
    if (vm.jit_cache.count != 0) {
        fprintf(stderr, "expected no native cache entries to be installed\n");
        success = false;
    }

cleanup:
    freeVM();

    if (previous_copy) {
#ifdef _WIN32
        size_t restored_len = strlen(previous_copy);
        char* restore_buffer = (char*)malloc(restored_len + strlen("ORUS_JIT_FORCE_HELPER_STUB=") + 1u);
        if (restore_buffer) {
            strcpy(restore_buffer, "ORUS_JIT_FORCE_HELPER_STUB=");
            strcat(restore_buffer, previous_copy);
            _putenv(restore_buffer);
            free(restore_buffer);
        } else {
            _putenv("ORUS_JIT_FORCE_HELPER_STUB=");
        }
#else
        setenv("ORUS_JIT_FORCE_HELPER_STUB", previous_copy, 1);
#endif
        free(previous_copy);
    } else {
#ifdef _WIN32
        _putenv("ORUS_JIT_FORCE_HELPER_STUB=");
#else
        unsetenv("ORUS_JIT_FORCE_HELPER_STUB");
#endif
    }

    return success;
}

static bool test_translates_i32_comparison_branch(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t lhs = FRAME_REG_START;
    const uint16_t rhs = FRAME_REG_START + 1u;
    const uint16_t predicate = FRAME_REG_START + 2u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, lhs,
                                         I32_VAL(0)),
                "expected lhs constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, rhs,
                                         I32_VAL(1)),
                "expected rhs constant");

    writeChunk(&chunk, OP_LT_I32_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)rhs, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_JUMP_IF_NOT_SHORT, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, "jit_translation");
    writeChunk(&chunk, 0u, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    bool found_compare = false;
    bool found_jump = false;
    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_LT_I32) {
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_BOOL,
                        "comparison should yield bool kind");
            ASSERT_TRUE(inst->operands.arithmetic.dst_reg == predicate,
                        "predicate register mismatch");
            ASSERT_TRUE(inst->operands.arithmetic.lhs_reg == lhs,
                        "lhs register mismatch");
            ASSERT_TRUE(inst->operands.arithmetic.rhs_reg == rhs,
                        "rhs register mismatch");
            found_compare = true;
        } else if (inst->opcode == ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT) {
            ASSERT_TRUE(inst->operands.jump_if_not_short.predicate_reg == predicate,
                        "jump predicate mismatch");
            ASSERT_TRUE(inst->operands.jump_if_not_short.offset == 0u,
                        "jump offset mismatch");
            found_jump = true;
        }
    }

    ASSERT_TRUE(found_compare, "expected comparison IR opcode");
    ASSERT_TRUE(found_jump, "expected conditional jump IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_eq_r_with_typed_inputs(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t dst = FRAME_REG_START;
    const uint16_t lhs = FRAME_REG_START + 1u;
    const uint16_t rhs = FRAME_REG_START + 2u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I64_CONST, lhs,
                                         I64_VAL(4)),
                "expected lhs constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I64_CONST, rhs,
                                         I64_VAL(6)),
                "expected rhs constant");

    writeChunk(&chunk, OP_EQ_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)dst, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)rhs, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    bool found_eq = false;
    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_EQ_I64) {
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_BOOL,
                        "eq should yield bool kind");
            ASSERT_TRUE(inst->operands.arithmetic.dst_reg == dst,
                        "dst register mismatch");
            ASSERT_TRUE(inst->operands.arithmetic.lhs_reg == lhs,
                        "lhs register mismatch");
            ASSERT_TRUE(inst->operands.arithmetic.rhs_reg == rhs,
                        "rhs register mismatch");
            found_eq = true;
            break;
        }
    }

    ASSERT_TRUE(found_eq, "expected eq IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_loop_with_forward_exit(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t counter = FRAME_REG_START;
    const uint16_t limit = FRAME_REG_START + 1u;
    const uint16_t step = FRAME_REG_START + 2u;
    const uint16_t predicate = FRAME_REG_START + 3u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, counter,
                                         I32_VAL(0)),
                "expected counter constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, limit,
                                         I32_VAL(3)),
                "expected loop limit constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, step,
                                         I32_VAL(1)),
                "expected loop increment constant");

    writeChunk(&chunk, OP_LT_I32_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)counter, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)limit, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_JUMP_IF_NOT_SHORT, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, "jit_translation");
    writeChunk(&chunk, 6u, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ADD_I32_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)counter, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)counter, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)step, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_LOOP_SHORT, 1, 0, "jit_translation");
    writeChunk(&chunk, 13u, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)(function.start + 12u);

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    bool found_compare = false;
    bool found_jump = false;
    bool found_add = false;
    bool found_loop_back = false;

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LT_I32:
                ASSERT_TRUE(inst->operands.arithmetic.dst_reg == predicate,
                            "loop predicate register mismatch");
                ASSERT_TRUE(inst->operands.arithmetic.lhs_reg == counter,
                            "loop lhs register mismatch");
                ASSERT_TRUE(inst->operands.arithmetic.rhs_reg == limit,
                            "loop rhs register mismatch");
                found_compare = true;
                break;
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
                ASSERT_TRUE(inst->operands.jump_if_not_short.predicate_reg ==
                                predicate,
                            "loop jump predicate mismatch");
                ASSERT_TRUE(inst->operands.jump_if_not_short.offset == 6u,
                            "loop exit offset mismatch");
                found_jump = true;
                break;
            case ORUS_JIT_IR_OP_ADD_I32:
                ASSERT_TRUE(inst->operands.arithmetic.dst_reg == counter,
                            "loop increment destination mismatch");
                ASSERT_TRUE(inst->operands.arithmetic.lhs_reg == counter,
                            "loop increment lhs mismatch");
                ASSERT_TRUE(inst->operands.arithmetic.rhs_reg == step,
                            "loop increment rhs mismatch");
                found_add = true;
                break;
            case ORUS_JIT_IR_OP_LOOP_BACK:
                ASSERT_TRUE(inst->operands.loop_back.back_offset == 13u,
                            "loop back offset mismatch");
                found_loop_back = true;
                break;
            default:
                break;
        }
    }

    ASSERT_TRUE(found_compare, "expected loop compare IR instruction");
    ASSERT_TRUE(found_jump, "expected loop conditional jump IR instruction");
    ASSERT_TRUE(found_add, "expected loop body arithmetic instruction");
    ASSERT_TRUE(found_loop_back, "expected loop back-edge IR instruction");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_loop_with_nested_branches_and_helper(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const char* tag = "jit_translation";

    const uint16_t counter = FRAME_REG_START;
    const uint16_t limit = FRAME_REG_START + 1u;
    const uint16_t step = FRAME_REG_START + 2u;
    const uint16_t predicate = FRAME_REG_START + 3u;
    const uint16_t nested_predicate = FRAME_REG_START + 4u;
    const uint16_t helper_dst = FRAME_REG_START + 5u;
    const uint16_t helper_arg = FRAME_REG_START + 6u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, counter,
                                         I32_VAL(0)),
                "expected counter constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, limit,
                                         I32_VAL(8)),
                "expected loop limit constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, step,
                                         I32_VAL(1)),
                "expected loop step constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, helper_dst,
                                         I32_VAL(0)),
                "expected helper accumulator constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, helper_arg,
                                         I32_VAL(2)),
                "expected helper argument constant");

    size_t loop_start = chunk.count;

    writeChunk(&chunk, OP_LT_I32_TYPED, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)limit, 1, 0, tag);

    size_t guard_jump_index = chunk.count;
    writeChunk(&chunk, OP_JUMP_IF_NOT_R, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, tag);
    size_t guard_jump_hi = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);
    size_t guard_jump_lo = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);

    writeChunk(&chunk, OP_EQ_R, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)nested_predicate, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)helper_arg, 1, 0, tag);

    size_t nested_jump_index = chunk.count;
    writeChunk(&chunk, OP_JUMP_IF_NOT_SHORT, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)nested_predicate, 1, 0, tag);
    size_t nested_jump_offset_index = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);

    writeChunk(&chunk, OP_CALL_NATIVE_R, 1, 0, tag);
    writeChunk(&chunk, 0u, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)helper_arg, 1, 0, tag);
    writeChunk(&chunk, 1u, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)helper_dst, 1, 0, tag);

    size_t skip_else_jump_index = chunk.count;
    writeChunk(&chunk, OP_JUMP, 1, 0, tag);
    size_t skip_else_hi = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);
    size_t skip_else_lo = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);

    size_t else_start = chunk.count;
    for (int i = 0; i < 80; ++i) {
        writeChunk(&chunk, OP_ADD_I32_TYPED, 1, 0, tag);
        writeChunk(&chunk, (uint8_t)helper_dst, 1, 0, tag);
        writeChunk(&chunk, (uint8_t)helper_dst, 1, 0, tag);
        writeChunk(&chunk, (uint8_t)step, 1, 0, tag);
    }
    size_t else_end = chunk.count;

    uint8_t nested_offset = (uint8_t)(else_start - (nested_jump_index + 3u));
    chunk.code[nested_jump_offset_index] = nested_offset;

    uint16_t skip_else_offset =
        (uint16_t)(else_end - (skip_else_jump_index + 3u));
    chunk.code[skip_else_hi] = (uint8_t)((skip_else_offset >> 8) & 0xFF);
    chunk.code[skip_else_lo] = (uint8_t)(skip_else_offset & 0xFF);

    writeChunk(&chunk, OP_ADD_I32_TYPED, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)step, 1, 0, tag);

    size_t loop_back_index = chunk.count;
    writeChunk(&chunk, OP_LOOP, 1, 0, tag);
    size_t loop_back_hi = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);
    size_t loop_back_lo = chunk.count;
    writeChunk(&chunk, 0u, 1, 0, tag);

    size_t exit_label = chunk.count;
    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, tag);

    uint16_t loop_back_offset =
        (uint16_t)((loop_back_index + 3u) - loop_start);
    chunk.code[loop_back_hi] = (uint8_t)((loop_back_offset >> 8) & 0xFF);
    chunk.code[loop_back_lo] = (uint8_t)(loop_back_offset & 0xFF);

    uint16_t guard_offset =
        (uint16_t)(exit_label - (guard_jump_index + 4u));
    ASSERT_TRUE(guard_offset > UINT8_MAX,
                "expected long guard exit offset");
    chunk.code[guard_jump_hi] = (uint8_t)((guard_offset >> 8) & 0xFF);
    chunk.code[guard_jump_lo] = (uint8_t)(guard_offset & 0xFF);

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)loop_start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    bool saw_long_exit = false;
    bool saw_helper = false;
    bool saw_long_jump = false;
    bool saw_loop_back = false;

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT) {
            if (inst->bytecode_offset == (uint32_t)guard_jump_index) {
                ASSERT_TRUE(inst->operands.jump_if_not_short.bytecode_length == 4u,
                            "expected long guard branch length");
                ASSERT_TRUE(inst->operands.jump_if_not_short.offset == guard_offset,
                            "guard branch offset mismatch");
                saw_long_exit = true;
            } else if (inst->bytecode_offset == (uint32_t)nested_jump_index) {
                ASSERT_TRUE(inst->operands.jump_if_not_short.bytecode_length == 3u,
                            "expected nested branch to remain short");
            }
        } else if (inst->opcode == ORUS_JIT_IR_OP_JUMP_SHORT &&
                   inst->bytecode_offset == (uint32_t)skip_else_jump_index) {
            ASSERT_TRUE(inst->operands.jump_short.bytecode_length == 3u,
                        "expected long forward jump encoding");
            ASSERT_TRUE(inst->operands.jump_short.offset == skip_else_offset,
                        "skip-else jump offset mismatch");
            saw_long_jump = true;
        } else if (inst->opcode == ORUS_JIT_IR_OP_CALL_NATIVE) {
            uint16_t expected_base = helper_dst < helper_arg ? helper_dst : helper_arg;
            uint16_t expected_high = helper_dst > helper_arg ? helper_dst : helper_arg;
            uint16_t expected_count = (uint16_t)((expected_high - expected_base) + 1u);
            ASSERT_TRUE(inst->operands.call_native.spill_base == expected_base,
                        "call native spill base should cover helper registers");
            ASSERT_TRUE(inst->operands.call_native.spill_count == expected_count,
                        "call native spill range should include dst and args");
            saw_helper = true;
        } else if (inst->opcode == ORUS_JIT_IR_OP_LOOP_BACK) {
            ASSERT_TRUE(inst->operands.loop_back.back_offset == loop_back_offset,
                        "loop back offset mismatch");
            saw_loop_back = true;
        }
    }

    ASSERT_TRUE(saw_long_exit, "expected long conditional exit branch");
    ASSERT_TRUE(saw_long_jump, "expected long unconditional jump");
    ASSERT_TRUE(saw_helper, "expected helper call inside loop");
    ASSERT_TRUE(saw_loop_back, "expected loop back IR instruction");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_if_else_jump_short(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t lhs = FRAME_REG_START;
    const uint16_t rhs = FRAME_REG_START + 1u;
    const uint16_t predicate = FRAME_REG_START + 2u;
    const uint16_t then_dst = FRAME_REG_START + 3u;
    const uint16_t else_dst = FRAME_REG_START + 4u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, lhs,
                                         I32_VAL(0)),
                "expected lhs constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, rhs,
                                         I32_VAL(1)),
                "expected rhs constant");

    writeChunk(&chunk, OP_LT_I32_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)rhs, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_JUMP_IF_NOT_SHORT, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)predicate, 1, 0, "jit_translation");
    writeChunk(&chunk, 6u, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ADD_I32_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)then_dst, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)rhs, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_JUMP_SHORT, 1, 0, "jit_translation");
    writeChunk(&chunk, 4u, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_SUB_I32_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)else_dst, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)rhs, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)lhs, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    bool found_predicate = false;
    bool found_conditional_jump = false;
    bool found_forward_jump = false;

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LT_I32:
                found_predicate = true;
                break;
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
                ASSERT_TRUE(inst->operands.jump_if_not_short.predicate_reg ==
                                predicate,
                            "if/else predicate register mismatch");
                ASSERT_TRUE(inst->operands.jump_if_not_short.offset == 6u,
                            "if/else jump offset mismatch");
                found_conditional_jump = true;
                break;
            case ORUS_JIT_IR_OP_JUMP_SHORT:
                ASSERT_TRUE(inst->operands.jump_short.offset == 4u,
                            "if/else forward jump offset mismatch");
                found_forward_jump = true;
                break;
            default:
                break;
        }
    }

    ASSERT_TRUE(found_predicate, "expected predicate comparison IR instruction");
    ASSERT_TRUE(found_conditional_jump,
                "expected conditional branch IR instruction");
    ASSERT_TRUE(found_forward_jump, "expected forward jump IR instruction");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_frame_window_moves(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t base_reg = FRAME_REG_START;
    const uint8_t store_offset = 1u;
    const uint8_t move_offset = 2u;
    const uint16_t load_dst = FRAME_REG_START + 3u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I64_CONST, base_reg,
                                         I64_VAL(5)),
                "expected initial i64 constant load");

    writeChunk(&chunk, OP_STORE_FRAME, 1, 0, "jit_translation");
    writeChunk(&chunk, store_offset, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)base_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_MOVE_FRAME, 1, 0, "jit_translation");
    writeChunk(&chunk, move_offset, 1, 0, "jit_translation");
    writeChunk(&chunk, store_offset, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_LOAD_FRAME, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)load_dst, 1, 0, "jit_translation");
    writeChunk(&chunk, move_offset, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ADD_I64_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)base_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)base_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)load_dst, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure for frame window moves: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_store = false;
    bool saw_move = false;
    bool saw_load = false;
    bool saw_add = false;
    const uint16_t stored_reg = (uint16_t)(FRAME_REG_START + store_offset);
    const uint16_t moved_reg = (uint16_t)(FRAME_REG_START + move_offset);

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_MOVE_I64) {
            if (inst->operands.move.dst_reg == stored_reg &&
                inst->operands.move.src_reg == base_reg) {
                saw_store = true;
            } else if (inst->operands.move.dst_reg == moved_reg &&
                       inst->operands.move.src_reg == stored_reg) {
                saw_move = true;
            } else if (inst->operands.move.dst_reg == load_dst &&
                       inst->operands.move.src_reg == moved_reg) {
                saw_load = true;
            }
        } else if (inst->opcode == ORUS_JIT_IR_OP_ADD_I64) {
            saw_add = true;
        }
    }

    ASSERT_TRUE(saw_store, "expected move to frame slot when storing local");
    ASSERT_TRUE(saw_move, "expected move between frame slots");
    ASSERT_TRUE(saw_load, "expected load from frame slot into register");
    ASSERT_TRUE(saw_add, "expected i64 arithmetic after frame moves");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_iterator_bytecodes(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t range_reg = 0u;
    const uint16_t iter_reg = 1u;
    const uint16_t value_reg = 2u;
    const uint16_t has_reg = 3u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, range_reg,
                                         I32_VAL(3)),
                "expected range bound load");

    writeChunk(&chunk, OP_GET_ITER_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)range_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ITER_NEXT_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)has_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_JUMP_IF_NOT_SHORT, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)has_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, 0u, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure for iterator lowering: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_get_iter = false;
    bool saw_iter_next = false;
    bool saw_predicate_branch = false;

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_GET_ITER:
                if (inst->operands.get_iter.dst_reg == iter_reg &&
                    inst->operands.get_iter.iterable_reg == range_reg) {
                    saw_get_iter = true;
                }
                break;
            case ORUS_JIT_IR_OP_ITER_NEXT:
                if (inst->operands.iter_next.value_reg == value_reg &&
                    inst->operands.iter_next.iterator_reg == iter_reg &&
                    inst->operands.iter_next.has_value_reg == has_reg) {
                    saw_iter_next = true;
                }
                break;
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
                if (inst->operands.jump_if_not_short.predicate_reg == has_reg) {
                    saw_predicate_branch = true;
                }
                break;
            default:
                break;
        }
    }

    ASSERT_TRUE(saw_get_iter, "expected ORUS_JIT_IR_OP_GET_ITER in program");
    ASSERT_TRUE(saw_iter_next,
                "expected ORUS_JIT_IR_OP_ITER_NEXT in program");
    ASSERT_TRUE(saw_predicate_branch,
                "expected conditional branch to depend on iterator predicate");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_range_iterator_materialization(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t start_reg = 0u;
    const uint16_t end_reg = 1u;
    const uint16_t step_reg = 2u;
    const uint16_t range_reg = 3u;
    const uint16_t iter_reg = 4u;
    const uint16_t value_reg = 5u;
    const uint16_t has_reg = 6u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, start_reg,
                                         I32_VAL(1)),
                "expected range start constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, end_reg,
                                         I32_VAL(5)),
                "expected range end constant");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, step_reg,
                                         I32_VAL(1)),
                "expected range step constant");

    writeChunk(&chunk, OP_RANGE_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)range_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, 3u, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)start_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)end_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)step_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_GET_ITER_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)range_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ITER_NEXT_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)has_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure for range lowering: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_range = false;
    bool saw_get_iter = false;
    bool saw_iter_next = false;

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_RANGE:
                if (inst->operands.range.dst_reg == range_reg &&
                    inst->operands.range.arg_count == 3u &&
                    inst->operands.range.arg_regs[0] == start_reg &&
                    inst->operands.range.arg_regs[1] == end_reg &&
                    inst->operands.range.arg_regs[2] == step_reg) {
                    saw_range = true;
                }
                break;
            case ORUS_JIT_IR_OP_GET_ITER:
                if (inst->operands.get_iter.dst_reg == iter_reg &&
                    inst->operands.get_iter.iterable_reg == range_reg) {
                    saw_get_iter = true;
                }
                break;
            case ORUS_JIT_IR_OP_ITER_NEXT:
                if (inst->operands.iter_next.value_reg == value_reg &&
                    inst->operands.iter_next.iterator_reg == iter_reg &&
                    inst->operands.iter_next.has_value_reg == has_reg) {
                    saw_iter_next = true;
                }
                break;
            default:
                break;
        }
    }

    ASSERT_TRUE(saw_range, "expected ORUS_JIT_IR_OP_RANGE in program");
    ASSERT_TRUE(saw_get_iter, "expected iterator acquisition after range");
    ASSERT_TRUE(saw_iter_next, "expected iterator advance after range");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_range_iterator_frame_moves(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t bound_reg = 0u;
    const uint16_t iter_reg = 1u;
    const uint16_t value_reg = 2u;
    const uint16_t has_reg = 3u;
    const uint16_t loaded_reg = 4u;
    const uint16_t sum_reg = 5u;
    const uint16_t frame_slot = 0u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, bound_reg,
                                         I32_VAL(4)),
                "expected loop bound constant");

    writeChunk(&chunk, OP_GET_ITER_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)bound_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ITER_NEXT_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)has_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_STORE_FRAME, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)frame_slot, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_LOAD_FRAME, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)loaded_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)frame_slot, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_ADD_I64_TYPED, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)sum_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)value_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)loaded_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected translation failure for iterator frame moves: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_store_move = false;
    bool saw_load_move = false;
    bool saw_add = false;

    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_MOVE_I64) {
            if (inst->operands.move.dst_reg ==
                (uint16_t)(FRAME_REG_START + frame_slot)) {
                saw_store_move = true;
            } else if (inst->operands.move.dst_reg == loaded_reg) {
                saw_load_move = true;
            }
        } else if (inst->opcode == ORUS_JIT_IR_OP_ADD_I64 &&
                   inst->operands.arithmetic.dst_reg == sum_reg) {
            saw_add = true;
        }
    }

    ASSERT_TRUE(saw_store_move,
                "expected i64 move when storing iterator value to frame");
    ASSERT_TRUE(saw_load_move,
                "expected i64 move when reloading iterator value from frame");
    ASSERT_TRUE(saw_add, "expected i64 addition after frame moves");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_iterator_boxed_move(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t bound_reg = 0u;
    const uint16_t iter_reg = 1u;
    const uint16_t frame_slot = 2u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, bound_reg,
                                         I32_VAL(2)),
                "expected loop bound constant");

    writeChunk(&chunk, OP_GET_ITER_R, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)bound_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_STORE_FRAME, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)frame_slot, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)iter_reg, 1, 0, "jit_translation");

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected iterator store failure: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_boxed_move = false;
    for (size_t i = 0; i < program.count; ++i) {
        const OrusJitIRInstruction* inst = &program.instructions[i];
        if (inst->opcode == ORUS_JIT_IR_OP_MOVE_VALUE &&
            inst->operands.move.dst_reg ==
                (uint16_t)(FRAME_REG_START + frame_slot)) {
            saw_boxed_move = true;
            break;
        }
    }

    ASSERT_TRUE(saw_boxed_move,
                "expected boxed move when storing iterator object to frame");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_runtime_helpers(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const char* tag = "jit_translation";
    ObjString* enum_type = allocateString("BenchType", 9);
    ObjString* enum_variant = allocateString("Variant", 7);
    ASSERT_TRUE(enum_type != NULL && enum_variant != NULL,
                "expected enum name allocation");
    int enum_type_index = addConstant(&chunk, STRING_VAL(enum_type));
    int enum_variant_index = addConstant(&chunk, STRING_VAL(enum_variant));
    ASSERT_TRUE(enum_type_index >= 0 && enum_variant_index >= 0,
                "expected enum constant indices");

    writeChunk(&chunk, OP_TIME_STAMP, 1, 0, tag);
    writeChunk(&chunk, 0u, 1, 0, tag);

    writeChunk(&chunk, OP_MAKE_ARRAY_R, 1, 0, tag);
    writeChunk(&chunk, 1u, 1, 0, tag);
    writeChunk(&chunk, 2u, 1, 0, tag);
    writeChunk(&chunk, 2u, 1, 0, tag);

    writeChunk(&chunk, OP_ENUM_NEW_R, 1, 0, tag);
    writeChunk(&chunk, 3u, 1, 0, tag);
    writeChunk(&chunk, 1u, 1, 0, tag);
    writeChunk(&chunk, 2u, 1, 0, tag);
    writeChunk(&chunk, 4u, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)((enum_type_index >> 8) & 0xFF), 1, 0, tag);
    writeChunk(&chunk, (uint8_t)(enum_type_index & 0xFF), 1, 0, tag);
    writeChunk(&chunk, (uint8_t)((enum_variant_index >> 8) & 0xFF), 1, 0, tag);
    writeChunk(&chunk, (uint8_t)(enum_variant_index & 0xFF), 1, 0, tag);

    writeChunk(&chunk, OP_ARRAY_PUSH_R, 1, 0, tag);
    writeChunk(&chunk, 5u, 1, 0, tag);
    writeChunk(&chunk, 6u, 1, 0, tag);

    writeChunk(&chunk, OP_PRINT_R, 1, 0, tag);
    writeChunk(&chunk, 7u, 1, 0, tag);

    writeChunk(&chunk, OP_ASSERT_EQ_R, 1, 0, tag);
    writeChunk(&chunk, 8u, 1, 0, tag);
    writeChunk(&chunk, 9u, 1, 0, tag);
    writeChunk(&chunk, 10u, 1, 0, tag);
    writeChunk(&chunk, 11u, 1, 0, tag);

    writeChunk(&chunk, OP_CALL_NATIVE_R, 1, 0, tag);
    writeChunk(&chunk, 12u, 1, 0, tag);
    writeChunk(&chunk, 13u, 1, 0, tag);
    writeChunk(&chunk, 1u, 1, 0, tag);
    writeChunk(&chunk, 14u, 1, 0, tag);

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, tag);

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected helper translation failure: %s (opcode=%d, kind=%d, offset=%u)\n",
                orus_jit_translation_status_name(result.status),
                result.opcode, result.value_kind, result.bytecode_offset);
        success = false;
        goto cleanup;
    }

    bool saw_time_stamp = false;
    bool saw_make_array = false;
    bool saw_enum_new = false;
    bool saw_array_push = false;
    bool saw_print = false;
    bool saw_assert_eq = false;
    bool saw_call_native = false;

    for (size_t i = 0; i < program.count; ++i) {
        OrusJitIROpcode opcode = program.instructions[i].opcode;
        if (opcode == ORUS_JIT_IR_OP_TIME_STAMP) {
            saw_time_stamp = true;
        } else if (opcode == ORUS_JIT_IR_OP_MAKE_ARRAY) {
            saw_make_array = true;
        } else if (opcode == ORUS_JIT_IR_OP_ENUM_NEW) {
            saw_enum_new = true;
        } else if (opcode == ORUS_JIT_IR_OP_ARRAY_PUSH) {
            saw_array_push = true;
        } else if (opcode == ORUS_JIT_IR_OP_PRINT) {
            saw_print = true;
        } else if (opcode == ORUS_JIT_IR_OP_ASSERT_EQ) {
            saw_assert_eq = true;
        } else if (opcode == ORUS_JIT_IR_OP_CALL_NATIVE) {
            const OrusJitIRInstruction* call_inst = &program.instructions[i];
            ASSERT_TRUE(call_inst->operands.call_native.spill_base == 13u,
                        "runtime helper call should spill argument base");
            ASSERT_TRUE(call_inst->operands.call_native.spill_count == 2u,
                        "runtime helper call should spill dst and argument");
            saw_call_native = true;
        }
    }

    ASSERT_TRUE(saw_time_stamp, "expected ORUS_JIT_IR_OP_TIME_STAMP in program");
    ASSERT_TRUE(saw_make_array, "expected ORUS_JIT_IR_OP_MAKE_ARRAY in program");
    ASSERT_TRUE(saw_enum_new, "expected ORUS_JIT_IR_OP_ENUM_NEW in program");
    ASSERT_TRUE(saw_array_push, "expected ORUS_JIT_IR_OP_ARRAY_PUSH in program");
    ASSERT_TRUE(saw_print, "expected ORUS_JIT_IR_OP_PRINT in program");
    ASSERT_TRUE(saw_assert_eq, "expected ORUS_JIT_IR_OP_ASSERT_EQ in program");
    ASSERT_TRUE(saw_call_native, "expected ORUS_JIT_IR_OP_CALL_NATIVE in program");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_fused_increment_loop(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t counter_reg = FRAME_REG_START;
    const uint16_t limit_reg = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, counter_reg,
                                         I32_VAL(0)),
                "expected counter load");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, limit_reg,
                                         I32_VAL(4)),
                "expected limit load");

    const char* tag = "jit_translation";
    writeChunk(&chunk, OP_INC_CMP_JMP, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter_reg, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)limit_reg, 1, 0, tag);
    writeChunk(&chunk, 0xFF, 1, 0, tag);
    writeChunk(&chunk, 0xFB, 1, 0, tag);

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, tag);

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected fused loop translation failure: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_fused = false;
    for (size_t i = 0; i < program.count; ++i) {
        if (program.instructions[i].opcode == ORUS_JIT_IR_OP_INC_CMP_JUMP) {
            const OrusJitIRInstruction* inst = &program.instructions[i];
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_I32,
                        "expected i32 fused loop kind");
            ASSERT_TRUE(inst->operands.fused_loop.counter_reg == counter_reg,
                        "counter register mismatch");
            ASSERT_TRUE(inst->operands.fused_loop.limit_reg == limit_reg,
                        "limit register mismatch");
            ASSERT_TRUE(inst->operands.fused_loop.jump_offset == (int16_t)-5,
                        "jump offset mismatch");
            ASSERT_TRUE(
                inst->operands.fused_loop.step ==
                    (int8_t)ORUS_JIT_IR_LOOP_STEP_INCREMENT,
                "step kind mismatch");
            ASSERT_TRUE(
                inst->operands.fused_loop.compare_kind ==
                    (uint8_t)ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN,
                "compare kind mismatch");
            saw_fused = true;
            break;
        }
    }

    ASSERT_TRUE(saw_fused, "expected fused increment IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_mismatched_integer_fused_loop(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t counter_reg = FRAME_REG_START;
    const uint16_t limit_reg = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, counter_reg,
                                         I32_VAL(0)),
                "expected counter load");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_U32_CONST, limit_reg,
                                         U32_VAL(5)),
                "expected differently typed limit load");

    const char* tag = "jit_translation";
    writeChunk(&chunk, OP_INC_CMP_JMP, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter_reg, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)limit_reg, 1, 0, tag);
    writeChunk(&chunk, 0xFF, 1, 0, tag);
    writeChunk(&chunk, 0xFB, 1, 0, tag);

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, tag);

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr,
                "Unexpected fused loop translation failure for mismatched "
                "kinds: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_fused = false;
    for (size_t i = 0; i < program.count; ++i) {
        if (program.instructions[i].opcode == ORUS_JIT_IR_OP_INC_CMP_JUMP) {
            const OrusJitIRInstruction* inst = &program.instructions[i];
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_BOXED,
                        "expected boxed fused loop value kind");
            ASSERT_TRUE(inst->operands.fused_loop.counter_reg == counter_reg,
                        "counter register mismatch");
            ASSERT_TRUE(inst->operands.fused_loop.limit_reg == limit_reg,
                        "limit register mismatch");
            saw_fused = true;
            break;
        }
    }

    ASSERT_TRUE(saw_fused, "expected fused loop IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_mixed_boxed_counter_loop(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t boxed_src = FRAME_REG_START;
    const uint16_t counter_reg = FRAME_REG_START + 1u;
    const uint16_t limit_reg = FRAME_REG_START + 2u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, counter_reg,
                                         I32_VAL(0)),
                "expected counter load");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, limit_reg,
                                         I32_VAL(4)),
                "expected limit load");

    writeChunk(&chunk, OP_MOVE, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)counter_reg, 1, 0, "jit_translation");
    writeChunk(&chunk, (uint8_t)boxed_src, 1, 0, "jit_translation");

    const char* tag = "jit_translation";
    writeChunk(&chunk, OP_INC_CMP_JMP, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter_reg, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)limit_reg, 1, 0, tag);
    writeChunk(&chunk, 0xFF, 1, 0, tag);
    writeChunk(&chunk, 0xFB, 1, 0, tag);

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, tag);

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected mixed fused loop failure: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_fused = false;
    for (size_t i = 0; i < program.count; ++i) {
        if (program.instructions[i].opcode == ORUS_JIT_IR_OP_INC_CMP_JUMP) {
            const OrusJitIRInstruction* inst = &program.instructions[i];
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_BOXED,
                        "expected boxed fused loop kind");
            ASSERT_TRUE(inst->operands.fused_loop.counter_reg == counter_reg,
                        "counter register mismatch");
            ASSERT_TRUE(inst->operands.fused_loop.limit_reg == limit_reg,
                        "limit register mismatch");
            saw_fused = true;
            break;
        }
    }

    ASSERT_TRUE(saw_fused, "expected fused loop IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

static bool test_translates_fused_decrement_loop(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t counter_reg = FRAME_REG_START;
    const uint16_t limit_reg = FRAME_REG_START + 1u;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, counter_reg,
                                         I32_VAL(5)),
                "expected counter load");
    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, limit_reg,
                                         I32_VAL(0)),
                "expected limit load");

    const char* tag = "jit_translation";
    writeChunk(&chunk, OP_DEC_CMP_JMP, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)counter_reg, 1, 0, tag);
    writeChunk(&chunk, (uint8_t)limit_reg, 1, 0, tag);
    writeChunk(&chunk, 0xFF, 1, 0, tag);
    writeChunk(&chunk, 0xFB, 1, 0, tag);

    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, tag);

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;
    if (result.status != ORUS_JIT_TRANSLATE_STATUS_OK) {
        fprintf(stderr, "Unexpected fused loop translation failure: %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
        goto cleanup;
    }

    bool saw_fused = false;
    for (size_t i = 0; i < program.count; ++i) {
        if (program.instructions[i].opcode == ORUS_JIT_IR_OP_DEC_CMP_JUMP) {
            const OrusJitIRInstruction* inst = &program.instructions[i];
            ASSERT_TRUE(inst->value_kind == ORUS_JIT_VALUE_I32,
                        "expected i32 fused loop kind");
            ASSERT_TRUE(inst->operands.fused_loop.counter_reg == counter_reg,
                        "counter register mismatch");
            ASSERT_TRUE(inst->operands.fused_loop.limit_reg == limit_reg,
                        "limit register mismatch");
            ASSERT_TRUE(inst->operands.fused_loop.jump_offset == (int16_t)-5,
                        "jump offset mismatch");
            ASSERT_TRUE(
                inst->operands.fused_loop.step ==
                    (int8_t)ORUS_JIT_IR_LOOP_STEP_DECREMENT,
                "step kind mismatch");
            ASSERT_TRUE(
                inst->operands.fused_loop.compare_kind ==
                    (uint8_t)ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN,
                "compare kind mismatch");
            saw_fused = true;
            break;
        }
    }

    ASSERT_TRUE(saw_fused, "expected fused decrement IR opcode");

cleanup:
    orus_jit_ir_program_reset(&program);
    freeChunk(&chunk);
    freeVM();
    return success;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"translator emits i64 ops", test_translates_i64_linear_loop},
        {"translator promotes i32 inputs for i64 ops",
         test_translator_promotes_i32_constants_to_i64},
        {"translator emits f64 ops", test_translates_f64_stream},
        {"translator loads boxed bool constants", test_translates_boxed_bool_constant},
        {"translator emits string concat", test_translates_string_concat},
        {"translator emits typeof/istype helpers", test_translates_type_builtins},
        {"translator emits i32 to i64 conversion", test_translates_i32_to_i64_conversion},
        {"translator emits u32 to i32 conversion", test_translates_u32_to_i32_conversion},
        {"rollout blocks f64 before float stage",
         test_rollout_blocks_f64_before_stage},
        {"queue_tier_up skips stub install on unsupported",
         test_queue_tier_up_skips_stub_install_on_unsupported},
        {"queue_tier_up ignores helper-stub-only compilations",
         test_queue_tier_up_counts_only_native_entries},
        {"translator emits i32 comparison and branch",
         test_translates_i32_comparison_branch},
        {"translator lowers eq_r with typed inputs",
         test_translates_eq_r_with_typed_inputs},
        {"translator emits loop with forward exit",
         test_translates_loop_with_forward_exit},
        {"translator handles helper-rich loop exits",
         test_translates_loop_with_nested_branches_and_helper},
        {"translator emits if/else jump sequence",
         test_translates_if_else_jump_short},
        {"translator emits frame window moves",
         test_translates_frame_window_moves},
        {"translator emits iterator bytecodes",
         test_translates_iterator_bytecodes},
        {"translator emits range iterator materialization",
         test_translates_range_iterator_materialization},
        {"translator keeps iterator values typed across frame moves",
         test_translates_range_iterator_frame_moves},
        {"translator boxes iterator objects for frame stores",
         test_translates_iterator_boxed_move},
        {"translator emits runtime helper calls",
         test_translates_runtime_helpers},
        {"translator emits fused increment loop",
         test_translates_fused_increment_loop},
        {"translator boxes mismatched typed fused loop",
         test_translates_mismatched_integer_fused_loop},
        {"translator routes boxed fused loop counters",
         test_translates_mixed_boxed_counter_loop},
        {"translator emits fused decrement loop",
         test_translates_fused_decrement_loop},
    };

    int passed = 0;
    const int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; ++i) {
        printf("[ RUN      ] %s\n", tests[i].name);
        if (tests[i].fn()) {
            printf("[     OK  ] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[ FAILED ] %s\n", tests[i].name);
        }
    }

    printf("%d/%d baseline translator tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}

