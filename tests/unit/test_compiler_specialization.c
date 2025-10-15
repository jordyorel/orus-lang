#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/specialization_feedback.h"
#include "internal/strutil.h"
#include "vm/vm.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_tiering.h"
#include "vm/vm_string_ops.h"

static BytecodeBuffer* build_baseline_hot_add_chunk(void) {
    BytecodeBuffer* buffer = init_bytecode_buffer();
    if (!buffer) {
        return NULL;
    }

    emit_byte_to_buffer(buffer, OP_ADD_I32_R);
    emit_byte_to_buffer(buffer, 2);  // dst
    emit_byte_to_buffer(buffer, 0);  // src1
    emit_byte_to_buffer(buffer, 1);  // src2

    emit_byte_to_buffer(buffer, OP_HALT);
    return buffer;
}

static Chunk* materialize_test_chunk(const CompilerContext* ctx, BytecodeBuffer* buffer) {
    if (!buffer) {
        return NULL;
    }

    Chunk* chunk = malloc(sizeof(Chunk));
    if (!chunk) {
        return NULL;
    }

    memset(chunk, 0, sizeof(Chunk));
    chunk->count = buffer->count;
    chunk->capacity = buffer->count;

    if (buffer->count > 0) {
        chunk->code = malloc((size_t)buffer->count);
        if (!chunk->code) {
            free(chunk);
            return NULL;
        }
        memcpy(chunk->code, buffer->instructions, (size_t)buffer->count);
    }

    if (ctx && ctx->constants && ctx->constants->count > 0) {
        chunk->constants.count = ctx->constants->count;
        chunk->constants.capacity = ctx->constants->capacity;
        chunk->constants.values = malloc(sizeof(Value) * chunk->constants.capacity);
        if (chunk->constants.values) {
            memcpy(chunk->constants.values, ctx->constants->values,
                   sizeof(Value) * (size_t)ctx->constants->count);
        }
    }

    return chunk;
}

static void destroy_chunk(Chunk* chunk) {
    if (!chunk) {
        return;
    }
    free(chunk->code);
    free(chunk->lines);
    free(chunk->columns);
    free(chunk->files);
    free(chunk->constants.values);
    free(chunk);
}

static void destroy_context(CompilerContext* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->function_chunks) {
        for (int i = 0; i < ctx->function_count; ++i) {
            if (ctx->function_chunks[i]) {
                free_bytecode_buffer(ctx->function_chunks[i]);
            }
            if (ctx->function_specialized_chunks && ctx->function_specialized_chunks[i]) {
                free_bytecode_buffer(ctx->function_specialized_chunks[i]);
            }
            if (ctx->function_deopt_stubs && ctx->function_deopt_stubs[i]) {
                free_bytecode_buffer(ctx->function_deopt_stubs[i]);
            }
            if (ctx->function_names && ctx->function_names[i]) {
                free(ctx->function_names[i]);
            }
        }
    }

    free(ctx->function_chunks);
    free(ctx->function_arities);
    free(ctx->function_names);
    free(ctx->function_specialized_chunks);
    free(ctx->function_deopt_stubs);
    free(ctx->function_hot_counts);

    if (ctx->constants) {
        free_constant_pool(ctx->constants);
        ctx->constants = NULL;
    }

    if (ctx->profiling_feedback) {
        compiler_free_profiling_feedback(ctx->profiling_feedback);
        free(ctx->profiling_feedback);
        ctx->profiling_feedback = NULL;
    }
}

static bool setup_context(CompilerContext* ctx, BytecodeBuffer* baseline) {
    memset(ctx, 0, sizeof(*ctx));

    ctx->function_count = 1;
    ctx->function_capacity = 1;

    ctx->function_chunks = malloc(sizeof(BytecodeBuffer*));
    ctx->function_arities = malloc(sizeof(int));
    ctx->function_names = malloc(sizeof(char*));
    ctx->function_specialized_chunks = calloc(1, sizeof(BytecodeBuffer*));
    ctx->function_deopt_stubs = calloc(1, sizeof(BytecodeBuffer*));
    ctx->function_hot_counts = malloc(sizeof(uint64_t));
    ctx->constants = init_constant_pool();

    if (!ctx->function_chunks || !ctx->function_arities || !ctx->function_names ||
        !ctx->function_specialized_chunks || !ctx->function_deopt_stubs ||
        !ctx->function_hot_counts || !ctx->constants) {
        return false;
    }

    ctx->function_chunks[0] = baseline;
    ctx->function_arities[0] = 2;
    ctx->function_names[0] = orus_strdup("hot_add");
    ctx->function_hot_counts[0] = FUNCTION_SPECIALIZATION_THRESHOLD + 128;

    CompilerProfilingFeedback* feedback = malloc(sizeof(CompilerProfilingFeedback));
    if (!feedback) {
        return false;
    }
    feedback->function_count = 1;
    feedback->functions = calloc(1, sizeof(FunctionSpecializationHint));
    if (!feedback->functions) {
        free(feedback);
        return false;
    }

    feedback->functions[0].name = orus_strdup("hot_add");
    feedback->functions[0].hit_count = ctx->function_hot_counts[0];
    feedback->functions[0].function_index = 0;
    feedback->functions[0].arity = 2;
    feedback->functions[0].eligible = true;

    ctx->profiling_feedback = feedback;
    return true;
}

static bool test_specialization_plan_injects_guards(void) {
    BytecodeBuffer* baseline = build_baseline_hot_add_chunk();
    if (!baseline) {
        fprintf(stderr, "Failed to build baseline chunk\n");
        return false;
    }

    CompilerContext ctx;
    if (!setup_context(&ctx, baseline)) {
        destroy_context(&ctx);
        fprintf(stderr, "Failed to setup compiler context\n");
        return false;
    }

    compiler_prepare_specialized_variants(&ctx);

    bool success = true;
    BytecodeBuffer* specialized = ctx.function_specialized_chunks[0];
    if (!specialized) {
        fprintf(stderr, "Specialized chunk was not generated\n");
        success = false;
        goto cleanup;
    }

    if (specialized->count <= baseline->count) {
        fprintf(stderr, "Specialized chunk did not grow after guard injection\n");
        success = false;
        goto cleanup;
    }

    if (specialized->instructions[0] != OP_MOVE_I32 || specialized->instructions[1] != 0 ||
        specialized->instructions[2] != 0) {
        fprintf(stderr, "First guard did not materialize as OP_MOVE_I32 on R0\n");
        success = false;
        goto cleanup;
    }

    if (specialized->instructions[3] != OP_MOVE_I32 || specialized->instructions[4] != 1 ||
        specialized->instructions[5] != 1) {
        fprintf(stderr, "Second guard did not materialize as OP_MOVE_I32 on R1\n");
        success = false;
        goto cleanup;
    }

    if (specialized->instructions[6] != OP_ADD_I32_TYPED) {
        fprintf(stderr, "Arithmetic opcode was not rewritten to typed variant\n");
        success = false;
        goto cleanup;
    }

    if (!ctx.function_deopt_stubs[0] || ctx.function_deopt_stubs[0]->count == 0 ||
        ctx.function_deopt_stubs[0]->instructions[0] != 2) {
        fprintf(stderr, "Deoptimization stub metadata missing arity\n");
        success = false;
    }

cleanup:
    destroy_context(&ctx);
    return success;
}

static bool run_specialized_execution_scenarios(void) {
    BytecodeBuffer* baseline = build_baseline_hot_add_chunk();
    if (!baseline) {
        fprintf(stderr, "Failed to build baseline chunk\n");
        return false;
    }

    CompilerContext ctx;
    if (!setup_context(&ctx, baseline)) {
        destroy_context(&ctx);
        fprintf(stderr, "Failed to setup compiler context\n");
        return false;
    }

    compiler_prepare_specialized_variants(&ctx);
    BytecodeBuffer* specialized_buffer = ctx.function_specialized_chunks[0];
    BytecodeBuffer* stub_buffer = ctx.function_deopt_stubs[0];

    if (!specialized_buffer || !stub_buffer) {
        fprintf(stderr, "Specialization artifacts missing\n");
        destroy_context(&ctx);
        return false;
    }

    Chunk* baseline_chunk = materialize_test_chunk(&ctx, baseline);
    Chunk* specialized_chunk = materialize_test_chunk(&ctx, specialized_buffer);
    Chunk* stub_chunk = materialize_test_chunk(&ctx, stub_buffer);

    bool success = true;

    initVM();

    Function* function = &vm.functions[0];
    memset(function, 0, sizeof(Function));
    function->chunk = baseline_chunk;
    function->specialized_chunk = specialized_chunk;
    function->deopt_stub_chunk = stub_chunk;
    function->arity = 2;
    function->tier = FUNCTION_TIER_SPECIALIZED;
    function->deopt_handler = vm_default_deopt_stub;
    function->debug_name = orus_strdup("hot_add");
    vm.functionCount = 1;

    CallFrame frame;
    memset(&frame, 0, sizeof(CallFrame));
    frame.functionIndex = 0;
    frame.parameterBaseRegister = FRAME_REG_START;
    vm.register_file.current_frame = &frame;

    vm.chunk = specialized_chunk;
    vm.ip = specialized_chunk->code;

    vm_set_register_safe(0, I32_VAL(5));
    vm_set_register_safe(1, I32_VAL(7));

    InterpretResult result = vm_run_dispatch();
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Specialized execution with typed inputs failed (%d)\n", result);
        success = false;
        goto teardown;
    }

    Value acc = vm_get_register_safe(2);
    if (!IS_I32(acc) || AS_I32(acc) != 12) {
        fprintf(stderr, "Typed specialized execution produced unexpected result\n");
        success = false;
        goto teardown;
    }

    if (function->debug_name) {
        free(function->debug_name);
        function->debug_name = NULL;
    }

    const char* skip_guard = getenv("ORUS_SKIP_SPECIALIZATION_GUARD_TEST");
    if (skip_guard && skip_guard[0] != '\0') {
        baseline_chunk = NULL;
        specialized_chunk = NULL;
        stub_chunk = NULL;
        goto teardown;
    }

    freeVM();
    initVM();

    function = &vm.functions[0];
    memset(function, 0, sizeof(Function));
    function->chunk = baseline_chunk;
    function->specialized_chunk = specialized_chunk;
    function->deopt_stub_chunk = stub_chunk;
    function->arity = 2;
    function->tier = FUNCTION_TIER_SPECIALIZED;
    function->deopt_handler = vm_default_deopt_stub;
    function->debug_name = orus_strdup("hot_add");
    vm.functionCount = 1;

    memset(&frame, 0, sizeof(CallFrame));
    frame.functionIndex = 0;
    frame.parameterBaseRegister = FRAME_REG_START;
    vm.register_file.current_frame = &frame;

    vm.chunk = specialized_chunk;
    vm.ip = specialized_chunk->code;

    ObjString* left = allocateString("hello", 5);
    ObjString* right = allocateString("world", 5);
    vm_set_register_safe(0, STRING_VAL(left));
    vm_set_register_safe(1, STRING_VAL(right));

    result = vm_run_dispatch();
    if (result != INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "Guard failure did not raise runtime error to trigger deopt\n");
        success = false;
        goto teardown;
    }

    if (!function || function->tier != FUNCTION_TIER_BASELINE) {
        fprintf(stderr, "Function tier did not downgrade after guard failure\n");
        success = false;
        goto teardown;
    }

    if (vm.chunk != baseline_chunk) {
        fprintf(stderr, "VM did not swap back to baseline chunk after deopt\n");
        success = false;
        goto teardown;
    }

    vm.lastError = BOOL_VAL(false);
    vm.ip = vm.chunk->code;

    result = vm_run_dispatch();
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Baseline execution after deopt failed (%d)\n", result);
        success = false;
        goto teardown;
    }

    Value concat = vm_get_register_safe(2);
    if (!IS_STRING(concat)) {
        fprintf(stderr, "Baseline fallback result is not a string\n");
        success = false;
    } else {
        const char* chars = string_get_chars(AS_STRING(concat));
        if (!chars || strcmp(chars, "helloworld") != 0) {
            fprintf(stderr, "Baseline fallback did not produce concatenated string\n");
            success = false;
        }
    }

teardown:
    if (function && function->debug_name) {
        free(function->debug_name);
        function->debug_name = NULL;
    }
    freeVM();
    destroy_chunk(baseline_chunk);
    destroy_chunk(specialized_chunk);
    destroy_chunk(stub_chunk);
    destroy_context(&ctx);
    return success;
}

int main(void) {
    int passed = 0;
    int total = 2;

    if (test_specialization_plan_injects_guards()) {
        printf("[1/2] Specialization plan generation... ok\n");
        passed++;
    } else {
        printf("[1/2] Specialization plan generation... failed\n");
    }

    if (run_specialized_execution_scenarios()) {
        printf("[2/2] Guarded execution and deopt fallback... ok\n");
        passed++;
    } else {
        printf("[2/2] Guarded execution and deopt fallback... failed\n");
    }

    printf("%d/%d compiler specialization tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
