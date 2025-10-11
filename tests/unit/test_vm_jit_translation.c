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
        fprintf(stderr, "Unexpected translation failure: %s\n",
                orus_jit_translation_status_name(result.status));
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
        fprintf(stderr, "Unexpected translation failure: %s\n",
                orus_jit_translation_status_name(result.status));
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

static bool test_reports_constant_kind_failure(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk chunk;
    initChunk(&chunk);

    Function function;
    init_function(&function, &chunk);

    const uint16_t dst0 = FRAME_REG_START;

    ASSERT_TRUE(write_load_numeric_const(&chunk, OP_LOAD_I32_CONST, dst0,
                                         BOOL_VAL(true)),
                "expected constant emission");
    writeChunk(&chunk, OP_RETURN_VOID, 1, 0, "jit_translation");

    HotPathSample sample = {0};
    sample.func = 0;
    sample.loop = (uint16_t)function.start;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    OrusJitTranslationResult result =
        orus_jit_translate_linear_block(&vm, &function, &sample, &program);

    bool success = true;

    if (result.status != ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND) {
        fprintf(stderr, "Expected unsupported constant kind failure, got %s\n",
                orus_jit_translation_status_name(result.status));
        success = false;
    }

    ASSERT_TRUE(orus_jit_translation_status_is_unsupported(result.status),
                "failure should flag unsupported status");

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

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"translator emits i64 ops", test_translates_i64_linear_loop},
        {"translator emits f64 ops", test_translates_f64_stream},
        {"translator reports unsupported constant", test_reports_constant_kind_failure},
        {"translator emits string concat", test_translates_string_concat},
        {"translator emits i32 to i64 conversion", test_translates_i32_to_i64_conversion},
        {"rollout blocks f64 before float stage",
         test_rollout_blocks_f64_before_stage},
        {"queue_tier_up skips stub install on unsupported",
         test_queue_tier_up_skips_stub_install_on_unsupported},
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

