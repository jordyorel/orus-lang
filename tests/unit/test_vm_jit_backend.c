#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/memory.h"
#include "vm/jit_backend.h"
#include "vm/jit_ir.h"
#include "vm/jit_translation.h"
#include "vm/jit_debug.h"
#include "vm/register_file.h"
#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_tiering.h"

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

static bool test_jit_debug_disassembly_capture(void) {
    initVM();

    struct OrusJitBackend* backend = orus_jit_backend_create();
    if (!backend) {
        freeVM();
        return false;
    }

    OrusJitDebugConfig config = ORUS_JIT_DEBUG_CONFIG_INIT;
    config.capture_disassembly = true;
    orus_jit_debug_set_config(&config);

    OrusJitIRInstruction instructions[1];
    memset(instructions, 0, sizeof(instructions));
    instructions[0].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 1);

    JITEntry entry;
    bool compiled = compile_program(backend, &program, &entry);
    if (!compiled) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    OrusJitDebugDisassembly disassembly;
    bool has_disassembly = orus_jit_debug_last_disassembly(&disassembly);
    bool contains_return = false;
    if (has_disassembly && disassembly.buffer) {
        contains_return = strstr(disassembly.buffer, "RETURN") != NULL;
    }

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();

    return has_disassembly && contains_return;
}

static bool test_jit_debug_guard_trace_and_loop_telemetry(void) {
    initVM();

    OrusJitDebugConfig config = ORUS_JIT_DEBUG_CONFIG_INIT;
    config.capture_guard_traces = true;
    config.loop_telemetry_enabled = true;
    orus_jit_debug_set_config(&config);
    orus_jit_debug_clear_loop_overrides();

    const uint16_t function_index = 3u;
    const uint16_t loop_index = 7u;
    orus_jit_debug_set_loop_enabled(loop_index, true);

    orus_jit_debug_record_loop_entry(&vm, function_index, loop_index);
    orus_jit_debug_record_loop_slow_path(&vm, function_index, loop_index);
    orus_jit_debug_record_guard_exit(&vm,
                                     function_index,
                                     loop_index,
                                     "unit-test",
                                     ORUS_JIT_DEBUG_INVALID_INSTRUCTION_INDEX);

    OrusJitGuardTraceEvent traces[4];
    size_t trace_count = orus_jit_debug_copy_guard_traces(traces, 4);
    bool guard_logged = trace_count > 0 &&
                        strcmp(traces[trace_count - 1].reason, "unit-test") == 0;

    OrusJitLoopTelemetry telemetry[4];
    size_t telemetry_count =
        orus_jit_debug_collect_loop_telemetry(telemetry, 4);
    bool loop_logged = false;
    if (telemetry_count > 0) {
        const OrusJitLoopTelemetry* entry = &telemetry[0];
        loop_logged = (entry->loop_index == loop_index) &&
                      (entry->entries == 1u) &&
                      (entry->guard_exits >= 1u) &&
                      (entry->slow_paths >= 1u);
    }

    orus_jit_debug_reset();
    freeVM();

    return guard_logged && loop_logged;
}

static bool run_gc_intensive_hotloop(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    if (!backend) {
        freeVM();
        return false;
    }

    const uint16_t acc_reg = FRAME_REG_START;
    const uint16_t inc_reg = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[6];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_I32;
    instructions[0].operands.load_const.dst_reg = acc_reg;
    instructions[0].operands.load_const.immediate_bits = 1u;

    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    instructions[1].value_kind = ORUS_JIT_VALUE_I32;
    instructions[1].operands.load_const.dst_reg = inc_reg;
    instructions[1].operands.load_const.immediate_bits = 2u;

    instructions[2].opcode = ORUS_JIT_IR_OP_ADD_I32;
    instructions[2].value_kind = ORUS_JIT_VALUE_I32;
    instructions[2].operands.arithmetic.dst_reg = acc_reg;
    instructions[2].operands.arithmetic.lhs_reg = acc_reg;
    instructions[2].operands.arithmetic.rhs_reg = inc_reg;

    instructions[3].opcode = ORUS_JIT_IR_OP_SAFEPOINT;
    instructions[3].value_kind = ORUS_JIT_VALUE_I32;

    instructions[4].opcode = ORUS_JIT_IR_OP_ADD_I32;
    instructions[4].value_kind = ORUS_JIT_VALUE_I32;
    instructions[4].operands.arithmetic.dst_reg = acc_reg;
    instructions[4].operands.arithmetic.lhs_reg = acc_reg;
    instructions[4].operands.arithmetic.rhs_reg = inc_reg;

    instructions[5].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 6);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    vm_store_i32_typed_hot(acc_reg, 0);
    vm_store_i32_typed_hot(inc_reg, 0);

    size_t previous_threshold = gcThreshold;
    size_t initial_gc = vm.gcCount;
    vm.gcPaused = false;
    gcThreshold = 64u;
    vm.bytesAllocated = gcThreshold + 1024u;

    orus_jit_helper_safepoint_reset();

    entry.entry_point(&vm);

    bool gc_triggered = vm.gcCount > initial_gc;
    size_t safepoint_count = orus_jit_helper_safepoint_count();
    int32_t acc_value = vm.typed_regs.i32_regs[acc_reg];
    int32_t inc_value = vm.typed_regs.i32_regs[inc_reg];
    bool registers_survived = (acc_value == 5) && (inc_value == 2);
    bool safepoint_seen = safepoint_count > 0u;

    if (!gc_triggered) {
        fprintf(stderr,
                "expected GC safepoint to trigger a collection during hotloop\n");
    }
    if (!safepoint_seen) {
        fprintf(stderr,
                "expected safepoint helper to increment counter during hotloop\n");
    }
    if (!registers_survived) {
        fprintf(stderr,
                "typed registers lost state across safepoint: acc=%d inc=%d\n",
                acc_value,
                inc_value);
    }

    gcThreshold = previous_threshold;

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();

    return gc_triggered && safepoint_seen && registers_survived;
}

static bool test_backend_gc_safepoint_handles_heap_growth(void) {
    return run_gc_intensive_hotloop();
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

#if defined(__x86_64__) || defined(_M_X64)
static void force_dynasm_env_on(void) {
#if defined(_WIN32)
    _putenv("ORUS_JIT_FORCE_DYNASM=1");
#else
    setenv("ORUS_JIT_FORCE_DYNASM", "1", 1);
#endif
}

static void force_dynasm_env_off(void) {
#if defined(_WIN32)
    _putenv("ORUS_JIT_FORCE_DYNASM=");
#else
    unsetenv("ORUS_JIT_FORCE_DYNASM");
#endif
}
#else
static void force_dynasm_env_on(void) {
}

static void force_dynasm_env_off(void) {
}
#endif

static int native_stub_invocations = 0;

static Value native_allocating_stub(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    native_stub_invocations++;
    if (getenv("ORUS_JIT_BACKEND_TEST_DEBUG")) {
        fprintf(stderr, "[jit-backend-test] native stub invocation %d\n",
                native_stub_invocations);
    }
    return BOOL_VAL(true);
}

static bool test_backend_call_native_triggers_gc_safepoint(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    vm.nativeFunctionCount = 1;
    vm.nativeFunctions[0].function = native_allocating_stub;
    vm.nativeFunctions[0].arity = 0;
    vm.nativeFunctions[0].name = NULL;
    vm.nativeFunctions[0].returnType = NULL;

    const uint16_t dst = FRAME_REG_START;

    OrusJitIRInstruction instructions[2];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_CALL_NATIVE;
    instructions[0].value_kind = ORUS_JIT_VALUE_BOXED;
    instructions[0].operands.call_native.dst_reg = dst;
    instructions[0].operands.call_native.first_arg_reg = dst;
    instructions[0].operands.call_native.arg_count = 0;
    instructions[0].operands.call_native.native_index = 0;
    instructions[0].operands.call_native.spill_base = dst;
    instructions[0].operands.call_native.spill_count = 1u;

    instructions[1].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 2);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    orus_jit_helper_safepoint_reset();
    native_stub_invocations = 0;

    entry.entry_point(&vm);

    Value result = vm_get_register_safe(dst);
    bool invoked = (native_stub_invocations == 1);
    bool returned_true = IS_BOOL(result) && AS_BOOL(result);
    size_t safepoint_count = orus_jit_helper_safepoint_count();
    bool safepoint_hit = safepoint_count > 0u;

    if (!invoked) {
        fprintf(stderr, "native call helper stub was not invoked\n");
    }
    if (!returned_true) {
        fprintf(stderr, "native call helper did not propagate return value\n");
    }
    if (!safepoint_hit) {
        fprintf(stderr,
                "native call helper missed safepoint accounting after host call\n");
    }

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();

    return invoked && returned_true && safepoint_hit;
}

static bool test_backend_deopt_mid_gc_preserves_frame_alignment(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    ASSERT_TRUE(backend != NULL, "expected backend allocation to succeed");

    Chunk* baseline_chunk = (Chunk*)malloc(sizeof(Chunk));
    Chunk* specialized_chunk = (Chunk*)malloc(sizeof(Chunk));
    if (!baseline_chunk || !specialized_chunk) {
        free(baseline_chunk);
        free(specialized_chunk);
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    initChunk(baseline_chunk);
    initChunk(specialized_chunk);

    writeChunk(baseline_chunk, OP_RETURN_VOID, 1, 0, "jit_backend");
    writeChunk(specialized_chunk, OP_RETURN_VOID, 1, 0, "jit_backend");

    int bool_constant_index = addConstant(baseline_chunk, BOOL_VAL(true));
    if (bool_constant_index < 0) {
        freeChunk(baseline_chunk);
        freeChunk(specialized_chunk);
        free(baseline_chunk);
        free(specialized_chunk);
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    Function* function = &vm.functions[0];
    memset(function, 0, sizeof(*function));
    function->chunk = baseline_chunk;
    function->specialized_chunk = specialized_chunk;
    function->tier = FUNCTION_TIER_SPECIALIZED;
    function->start = 0;
    function->arity = 0;
    function->deopt_handler = vm_default_deopt_stub;
    vm.functionCount = 1;

    vm.chunk = specialized_chunk;
    vm.ip = specialized_chunk->code;

    CallFrame* frame = allocate_frame(&vm.register_file);
    if (!frame) {
        freeVM();
        orus_jit_backend_destroy(backend);
        return false;
    }
    frame->functionIndex = 0;
    frame->parameterBaseRegister = FRAME_REG_START;
    frame->resultRegister = FRAME_REG_START;
    frame->register_count = 2;
    frame->previousChunk = specialized_chunk;

    const uint16_t bool_reg = FRAME_REG_START;
    const uint16_t dst_reg = (uint16_t)(FRAME_REG_START + 1u);

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_VALUE_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_BOOL;
    instructions[0].operands.load_const.dst_reg = bool_reg;
    instructions[0].operands.load_const.constant_index = (uint16_t)bool_constant_index;

    instructions[1].opcode = ORUS_JIT_IR_OP_SAFEPOINT;
    instructions[1].value_kind = ORUS_JIT_VALUE_BOOL;

    instructions[2].opcode = ORUS_JIT_IR_OP_MOVE_STRING;
    instructions[2].value_kind = ORUS_JIT_VALUE_STRING;
    instructions[2].operands.move.dst_reg = dst_reg;
    instructions[2].operands.move.src_reg = bool_reg;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);
    program.source_chunk = baseline_chunk;

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        deallocate_frame(&vm.register_file);
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    size_t previous_threshold = gcThreshold;
    size_t initial_gc_count = vm.gcCount;
    size_t base_type_deopts = vm.jit_native_type_deopts;
    gcThreshold = 64u;
    vm.bytesAllocated = gcThreshold + 1024u;
    vm.jit_pending_invalidate = false;
    memset(&vm.jit_pending_trigger, 0, sizeof(vm.jit_pending_trigger));

    entry.entry_point(&vm);

    bool gc_triggered = vm.gcCount > initial_gc_count;
    Value reconciled = vm.registers[bool_reg];
    bool bool_mirrors_match = IS_BOOL(reconciled) && AS_BOOL(reconciled) &&
                              vm.typed_regs.bool_regs[bool_reg];
    bool downgraded_to_baseline =
        function->tier == FUNCTION_TIER_BASELINE && vm.chunk == function->chunk;
    bool invalidate_recorded =
        vm.jit_pending_invalidate && vm.jit_pending_trigger.function_index == 0;
    bool deopt_recorded = vm.jit_native_type_deopts > base_type_deopts;

    if (!gc_triggered) {
        fprintf(stderr, "expected GC to trigger during safepoint before deopt\n");
    }
    if (!bool_mirrors_match) {
        fprintf(stderr,
                "typed and boxed registers diverged after GC + deopt: boxed=%d typed=%d\n",
                IS_BOOL(reconciled) ? AS_BOOL(reconciled) : -1,
                vm.typed_regs.bool_regs[bool_reg]);
    }
    if (!downgraded_to_baseline) {
        fprintf(stderr, "function did not fall back to baseline after deopt\n");
    }
    if (!invalidate_recorded) {
        fprintf(stderr, "jit invalidate trigger was not recorded after deopt\n");
    }
    if (!deopt_recorded) {
        fprintf(stderr, "type deopt counter was not incremented\n");
    }

    gcThreshold = previous_threshold;
    vm.bytesAllocated = 0;

    deallocate_frame(&vm.register_file);
    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();

    return gc_triggered && bool_mirrors_match && downgraded_to_baseline &&
           invalidate_recorded && deopt_recorded;
}

static bool test_backend_typed_deopt_landing_pad_reuses_frame(void) {
    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    Chunk* baseline_chunk = (Chunk*)malloc(sizeof(Chunk));
    Chunk* specialized_chunk = (Chunk*)malloc(sizeof(Chunk));
    if (!baseline_chunk || !specialized_chunk) {
        free(baseline_chunk);
        free(specialized_chunk);
        freeVM();
        return false;
    }

    initChunk(baseline_chunk);
    initChunk(specialized_chunk);

    writeChunk(baseline_chunk, OP_RETURN_VOID, 1, 0, "jit_backend");
    writeChunk(specialized_chunk, OP_RETURN_VOID, 1, 0, "jit_backend");

    Function* function = &vm.functions[0];
    memset(function, 0, sizeof(*function));
    function->chunk = baseline_chunk;
    function->specialized_chunk = specialized_chunk;
    function->tier = FUNCTION_TIER_SPECIALIZED;
    function->start = 0;
    function->arity = 1;
    function->deopt_handler = vm_default_deopt_stub;
    vm.functionCount = 1;

    vm.chunk = specialized_chunk;
    vm.ip = specialized_chunk->code;

    CallFrame* frame = allocate_frame(&vm.register_file);
    if (!frame) {
        freeChunk(baseline_chunk);
        freeChunk(specialized_chunk);
        free(baseline_chunk);
        free(specialized_chunk);
        freeVM();
        return false;
    }

    frame->functionIndex = 0;
    frame->register_count = 2;
    frame->parameterBaseRegister = (uint16_t)(frame->frame_base + frame->register_count - function->arity);
    frame->resultRegister = frame->frame_base;
    frame->previousChunk = specialized_chunk;
    frame->temp_count = 1;

    TypedRegisterWindow* window_before = frame->typed_window;
    ASSERT_TRUE(window_before != NULL, "expected typed register window");

    uint16_t param_reg = frame->parameterBaseRegister;
    uint16_t local_reg = frame->frame_base;
    uint16_t temp_reg = frame->temp_base;

    vm_store_i32_typed_hot(param_reg, 13);
    vm_store_i32_typed_hot(local_reg, 7);
    vm_store_i32_typed_hot(temp_reg, 99);

    ASSERT_TRUE(typed_window_slot_live(window_before, param_reg),
                "parameter register not marked live before deopt");
    ASSERT_TRUE(typed_window_slot_live(window_before, local_reg),
                "local register not marked live before deopt");
    ASSERT_TRUE(typed_window_slot_live(window_before, temp_reg),
                "temp register not marked live before deopt");

    vm_handle_type_error_deopt();

    bool same_window = frame->typed_window == window_before;
    bool params_cleared = !typed_window_slot_live(window_before, param_reg);
    bool locals_cleared = !typed_window_slot_live(window_before, local_reg);
    bool temps_cleared = !typed_window_slot_live(window_before, temp_reg);
    bool downgraded = function->tier == FUNCTION_TIER_BASELINE;
    bool ip_swapped = vm.chunk == function->chunk;

    if (!same_window) {
        fprintf(stderr, "typed window was replaced during deopt landing pad\n");
    }
    if (!params_cleared) {
        fprintf(stderr, "parameter register remained live after landing pad\n");
    }
    if (!locals_cleared) {
        fprintf(stderr, "local register remained live after landing pad\n");
    }
    if (!temps_cleared) {
        fprintf(stderr, "temp register remained live after landing pad\n");
    }
    if (!downgraded) {
        fprintf(stderr, "function did not downgrade after landing pad\n");
    }
    if (!ip_swapped) {
        fprintf(stderr, "VM instruction pointer did not swap to baseline chunk\n");
    }

    deallocate_frame(&vm.register_file);
    freeChunk(baseline_chunk);
    freeChunk(specialized_chunk);
    free(baseline_chunk);
    free(specialized_chunk);
    freeVM();

    return same_window && params_cleared && locals_cleared && temps_cleared && downgraded && ip_swapped;
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

static uint64_t bits_from_i32(int32_t value) {
    return (uint64_t)(uint32_t)value;
}

static uint64_t bits_from_i64(int64_t value) {
    return (uint64_t)value;
}

static uint64_t bits_from_u32(uint32_t value) {
    return (uint64_t)value;
}

static uint64_t bits_from_u64(uint64_t value) {
    return value;
}

static uint64_t bits_from_f64(double value) {
    uint64_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int32_t decode_i32_bits(uint64_t bits) {
    return (int32_t)(uint32_t)bits;
}

static int64_t decode_i64_bits(uint64_t bits) {
    return (int64_t)bits;
}

static uint32_t decode_u32_bits(uint64_t bits) {
    return (uint32_t)bits;
}

static uint64_t decode_u64_bits(uint64_t bits) {
    return bits;
}

static double decode_f64_bits(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

#if defined(__x86_64__) || defined(_M_X64)
typedef struct {
    uint64_t bits;
    const char* emitter_name;
} DynasmEmitterResult;

static uint64_t read_result_bits(OrusJitValueKind kind, uint16_t reg) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            return bits_from_i32(vm.typed_regs.i32_regs[reg]);
        case ORUS_JIT_VALUE_I64:
            return bits_from_i64(vm.typed_regs.i64_regs[reg]);
        case ORUS_JIT_VALUE_U32:
            return bits_from_u32(vm.typed_regs.u32_regs[reg]);
        case ORUS_JIT_VALUE_U64:
            return bits_from_u64(vm.typed_regs.u64_regs[reg]);
        case ORUS_JIT_VALUE_F64:
            return bits_from_f64(vm.typed_regs.f64_regs[reg]);
        default:
            return 0u;
    }
}

static bool execute_dynasm_parity_case(OrusJitValueKind kind,
                                       uint64_t lhs_bits,
                                       uint64_t rhs_bits,
                                       bool use_dynasm,
                                       DynasmEmitterResult* out_result) {
    if (!out_result) {
        return false;
    }

    force_helper_stub_env_off();
    if (use_dynasm) {
        force_dynasm_env_on();
    } else {
        force_dynasm_env_off();
    }

    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    if (!backend) {
        freeVM();
        force_dynasm_env_off();
        return false;
    }

    OrusJitIROpcode load_opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    OrusJitIROpcode add_opcode = ORUS_JIT_IR_OP_ADD_I32;

    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            break;
        case ORUS_JIT_VALUE_I64:
            load_opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_I64;
            break;
        case ORUS_JIT_VALUE_U32:
            load_opcode = ORUS_JIT_IR_OP_LOAD_U32_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_U32;
            break;
        case ORUS_JIT_VALUE_U64:
            load_opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_U64;
            break;
        case ORUS_JIT_VALUE_F64:
            load_opcode = ORUS_JIT_IR_OP_LOAD_F64_CONST;
            add_opcode = ORUS_JIT_IR_OP_ADD_F64;
            break;
        default:
            orus_jit_backend_destroy(backend);
            freeVM();
            force_dynasm_env_off();
            return false;
    }

    const uint16_t dst0 = FRAME_REG_START;
    const uint16_t dst1 = FRAME_REG_START + 1u;

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = load_opcode;
    instructions[0].value_kind = kind;
    instructions[0].operands.load_const.dst_reg = dst0;
    instructions[0].operands.load_const.immediate_bits = lhs_bits;

    instructions[1].opcode = load_opcode;
    instructions[1].value_kind = kind;
    instructions[1].operands.load_const.dst_reg = dst1;
    instructions[1].operands.load_const.immediate_bits = rhs_bits;

    instructions[2].opcode = add_opcode;
    instructions[2].value_kind = kind;
    instructions[2].operands.arithmetic.dst_reg = dst0;
    instructions[2].operands.arithmetic.lhs_reg = dst0;
    instructions[2].operands.arithmetic.rhs_reg = dst1;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    bool compiled = compile_program(backend, &program, &entry);
    bool success = compiled;
    if (!compiled) {
        orus_jit_backend_destroy(backend);
        freeVM();
        force_dynasm_env_off();
        return false;
    }

    const char* debug_name = entry.debug_name ? entry.debug_name : "";

    if (use_dynasm) {
        if (strcmp(debug_name, "orus_jit_ir_stub") != 0) {
            fprintf(stderr, "expected DynASM emitter, got %s\n", debug_name);
            success = false;
        }
    } else {
        if (strstr(debug_name, "linear") == NULL) {
            fprintf(stderr, "expected linear emitter, got %s\n", debug_name);
            success = false;
        }
    }

    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            vm_store_i32_typed_hot(dst0, 0);
            vm_store_i32_typed_hot(dst1, 0);
            break;
        case ORUS_JIT_VALUE_I64:
            vm_store_i64_typed_hot(dst0, 0);
            vm_store_i64_typed_hot(dst1, 0);
            break;
        case ORUS_JIT_VALUE_U32:
            vm_store_u32_typed_hot(dst0, 0u);
            vm_store_u32_typed_hot(dst1, 0u);
            break;
        case ORUS_JIT_VALUE_U64:
            vm_store_u64_typed_hot(dst0, 0u);
            vm_store_u64_typed_hot(dst1, 0u);
            break;
        case ORUS_JIT_VALUE_F64:
            vm_store_f64_typed_hot(dst0, 0.0);
            vm_store_f64_typed_hot(dst1, 0.0);
            break;
        default:
            break;
    }

    entry.entry_point(&vm);

    out_result->bits = read_result_bits(kind, dst0);
    out_result->emitter_name = debug_name;

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();
    force_dynasm_env_off();

    return success;
}

typedef struct {
    const char* label;
    OrusJitValueKind kind;
    uint64_t lhs_bits;
    uint64_t rhs_bits;
    uint64_t expected_bits;
} DynasmParityCase;

static void log_parity_mismatch(const char* emitter,
                                const DynasmParityCase* test_case,
                                uint64_t actual_bits,
                                uint64_t expected_bits) {
    switch (test_case->kind) {
        case ORUS_JIT_VALUE_I32:
            fprintf(stderr,
                    "%s emitter parity mismatch for %s: got %d expected %d\n",
                    emitter,
                    test_case->label,
                    decode_i32_bits(actual_bits),
                    decode_i32_bits(expected_bits));
            break;
        case ORUS_JIT_VALUE_I64:
            fprintf(stderr,
                    "%s emitter parity mismatch for %s: got %lld expected %lld\n",
                    emitter,
                    test_case->label,
                    (long long)decode_i64_bits(actual_bits),
                    (long long)decode_i64_bits(expected_bits));
            break;
        case ORUS_JIT_VALUE_U32:
            fprintf(stderr,
                    "%s emitter parity mismatch for %s: got %u expected %u\n",
                    emitter,
                    test_case->label,
                    decode_u32_bits(actual_bits),
                    decode_u32_bits(expected_bits));
            break;
        case ORUS_JIT_VALUE_U64:
            fprintf(stderr,
                    "%s emitter parity mismatch for %s: got %llu expected %llu\n",
                    emitter,
                    test_case->label,
                    (unsigned long long)decode_u64_bits(actual_bits),
                    (unsigned long long)decode_u64_bits(expected_bits));
            break;
        case ORUS_JIT_VALUE_F64:
            fprintf(stderr,
                    "%s emitter parity mismatch for %s: got %.17g expected %.17g\n",
                    emitter,
                    test_case->label,
                    decode_f64_bits(actual_bits),
                    decode_f64_bits(expected_bits));
            break;
        default:
            fprintf(stderr,
                    "%s emitter parity mismatch for %s: unsupported kind %d\n",
                    emitter,
                    test_case->label,
                    (int)test_case->kind);
            break;
    }
}

static bool run_dynasm_parity_case(const DynasmParityCase* test_case) {
    if (!test_case) {
        return false;
    }

    DynasmEmitterResult linear = {0};
    DynasmEmitterResult dynasm = {0};

    bool linear_ok = execute_dynasm_parity_case(test_case->kind,
                                                test_case->lhs_bits,
                                                test_case->rhs_bits,
                                                false,
                                                &linear);
    bool dynasm_ok = execute_dynasm_parity_case(test_case->kind,
                                                test_case->lhs_bits,
                                                test_case->rhs_bits,
                                                true,
                                                &dynasm);

    bool success = linear_ok && dynasm_ok;
    if (!linear_ok) {
        fprintf(stderr,
                "linear emitter parity case '%s' did not execute successfully\n",
                test_case->label);
    }
    if (!dynasm_ok) {
        fprintf(stderr,
                "DynASM emitter parity case '%s' did not execute successfully\n",
                test_case->label);
    }
    if (!success) {
        return false;
    }

    if (linear.bits != test_case->expected_bits) {
        log_parity_mismatch("linear", test_case, linear.bits, test_case->expected_bits);
        success = false;
    }
    if (dynasm.bits != test_case->expected_bits) {
        log_parity_mismatch("DynASM", test_case, dynasm.bits, test_case->expected_bits);
        success = false;
    }
    if (linear.bits != dynasm.bits) {
        fprintf(stderr,
                "linear and DynASM emitters diverged for %s (linear=0x%016llx DynASM=0x%016llx)\n",
                test_case->label,
                (unsigned long long)linear.bits,
                (unsigned long long)dynasm.bits);
        success = false;
    }

    return success;
}

static bool test_backend_dynasm_matches_linear_across_value_kinds(void) {
    const DynasmParityCase cases[] = {
        {.label = "i32_add",
         .kind = ORUS_JIT_VALUE_I32,
         .lhs_bits = bits_from_i32(21),
         .rhs_bits = bits_from_i32(29),
         .expected_bits = bits_from_i32(50)},
        {.label = "i64_add",
         .kind = ORUS_JIT_VALUE_I64,
         .lhs_bits = bits_from_i64(1024LL),
         .rhs_bits = bits_from_i64(256LL),
         .expected_bits = bits_from_i64(1280LL)},
        {.label = "u32_add",
         .kind = ORUS_JIT_VALUE_U32,
         .lhs_bits = bits_from_u32(100u),
         .rhs_bits = bits_from_u32(200u),
         .expected_bits = bits_from_u32(300u)},
        {.label = "u64_add",
         .kind = ORUS_JIT_VALUE_U64,
         .lhs_bits = bits_from_u64(5000000000ULL),
         .rhs_bits = bits_from_u64(42ULL),
         .expected_bits = bits_from_u64(5000000042ULL)},
        {.label = "f64_add",
         .kind = ORUS_JIT_VALUE_F64,
         .lhs_bits = bits_from_f64(3.125),
         .rhs_bits = bits_from_f64(6.875),
         .expected_bits = bits_from_f64(10.0)},
    };

    bool success = true;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (!run_dynasm_parity_case(&cases[i])) {
            success = false;
        }
    }

    if (!success) {
        fprintf(stderr,
                "DynASM vs linear parity test failed for at least one value kind\n");
    }

    return success;
}
#else
static bool test_backend_dynasm_matches_linear_across_value_kinds(void) {
    return true;
}
#endif

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

    vm.safe_register_reads = 0;
    entry.entry_point(&vm);

    bool success = true;
    if (vm.safe_register_reads != 0) {
        fprintf(stderr,
                "typed JIT loop touched boxed registers: observed %llu safe reads\n",
                (unsigned long long)vm.safe_register_reads);
        success = false;
    }
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

typedef union UnsupportedOpcodeValues {
    struct {
        int64_t lhs;
        int64_t rhs;
    } s64;
    struct {
        uint64_t lhs;
        uint64_t rhs;
    } u64;
    struct {
        double lhs;
        double rhs;
    } f64;
} UnsupportedOpcodeValues;

typedef struct UnsupportedOpcodeCase {
    const char* label;
    OrusJitIROpcode opcode;
    OrusJitValueKind kind;
    UnsupportedOpcodeValues values;
} UnsupportedOpcodeCase;

static bool run_unsupported_opcode_case(const UnsupportedOpcodeCase* test_case) {
    if (!test_case) {
        return false;
    }

    initVM();
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);

    struct OrusJitBackend* backend = orus_jit_backend_create();
    if (!backend) {
        freeVM();
        return false;
    }

    const uint16_t dst = FRAME_REG_START;
    const uint16_t lhs = FRAME_REG_START + 1u;
    const uint16_t rhs = FRAME_REG_START + 2u;

    OrusJitIRInstruction instructions[4];
    memset(instructions, 0, sizeof(instructions));

    instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
    instructions[0].value_kind = ORUS_JIT_VALUE_I32;
    instructions[1].value_kind = ORUS_JIT_VALUE_I32;

    uint64_t lhs_bits = 0u;
    uint64_t rhs_bits = 0u;

    switch (test_case->kind) {
        case ORUS_JIT_VALUE_I64:
            lhs_bits = (uint64_t)test_case->values.s64.lhs;
            rhs_bits = (uint64_t)test_case->values.s64.rhs;
            instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
            instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
            instructions[0].value_kind = ORUS_JIT_VALUE_I64;
            instructions[1].value_kind = ORUS_JIT_VALUE_I64;
            break;
        case ORUS_JIT_VALUE_U64:
            lhs_bits = test_case->values.u64.lhs;
            rhs_bits = test_case->values.u64.rhs;
            instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
            instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
            instructions[0].value_kind = ORUS_JIT_VALUE_U64;
            instructions[1].value_kind = ORUS_JIT_VALUE_U64;
            break;
        case ORUS_JIT_VALUE_F64: {
            double lhs_value = test_case->values.f64.lhs;
            double rhs_value = test_case->values.f64.rhs;
            memcpy(&lhs_bits, &lhs_value, sizeof(lhs_bits));
            memcpy(&rhs_bits, &rhs_value, sizeof(rhs_bits));
            instructions[0].opcode = ORUS_JIT_IR_OP_LOAD_F64_CONST;
            instructions[1].opcode = ORUS_JIT_IR_OP_LOAD_F64_CONST;
            instructions[0].value_kind = ORUS_JIT_VALUE_F64;
            instructions[1].value_kind = ORUS_JIT_VALUE_F64;
            break;
        }
        default:
            orus_jit_backend_destroy(backend);
            freeVM();
            return false;
    }

    instructions[0].operands.load_const.dst_reg = lhs;
    instructions[0].operands.load_const.immediate_bits = lhs_bits;
    instructions[1].operands.load_const.dst_reg = rhs;
    instructions[1].operands.load_const.immediate_bits = rhs_bits;

    instructions[2].opcode = test_case->opcode;
    instructions[2].value_kind = test_case->kind;
    instructions[2].operands.arithmetic.dst_reg = dst;
    instructions[2].operands.arithmetic.lhs_reg = lhs;
    instructions[2].operands.arithmetic.rhs_reg = rhs;

    instructions[3].opcode = ORUS_JIT_IR_OP_RETURN;

    OrusJitIRProgram program;
    init_ir_program(&program, instructions, 4);

    JITEntry entry;
    if (!compile_program(backend, &program, &entry)) {
        orus_jit_backend_destroy(backend);
        freeVM();
        return false;
    }

    const char* debug_name = entry.debug_name ? entry.debug_name : "";
    bool used_helper_stub = (strcmp(debug_name, "orus_jit_helper_stub") == 0);

    uint64_t initial_type_deopts = vm.jit_native_type_deopts;

    entry.entry_point(&vm);

    bool recorded_type_deopt = vm.jit_native_type_deopts > initial_type_deopts;

    orus_jit_backend_release_entry(backend, &entry);
    orus_jit_backend_destroy(backend);
    freeVM();

    if (!used_helper_stub) {
        fprintf(stderr,
                "unsupported opcode fixture '%s' expected helper stub fallback\n",
                test_case->label);
        return false;
    }

    if (!recorded_type_deopt) {
        fprintf(stderr,
                "unsupported opcode fixture '%s' did not trigger bailout counters\n",
                test_case->label);
        return false;
    }

    return true;
}

static bool test_backend_documents_unhandled_arithmetic_opcodes(void) {
    static const UnsupportedOpcodeCase cases[] = {
        {.label = "div_i64",
         .opcode = ORUS_JIT_IR_OP_DIV_I64,
         .kind = ORUS_JIT_VALUE_I64,
         .values.s64 = {.lhs = 96, .rhs = 7}},
        {.label = "mod_i64",
         .opcode = ORUS_JIT_IR_OP_MOD_I64,
         .kind = ORUS_JIT_VALUE_I64,
         .values.s64 = {.lhs = 96, .rhs = 7}},
        {.label = "div_u64",
         .opcode = ORUS_JIT_IR_OP_DIV_U64,
         .kind = ORUS_JIT_VALUE_U64,
         .values.u64 = {.lhs = 128u, .rhs = 5u}},
        {.label = "mod_u64",
         .opcode = ORUS_JIT_IR_OP_MOD_U64,
         .kind = ORUS_JIT_VALUE_U64,
         .values.u64 = {.lhs = 128u, .rhs = 5u}},
        {.label = "div_f64",
         .opcode = ORUS_JIT_IR_OP_DIV_F64,
         .kind = ORUS_JIT_VALUE_F64,
         .values.f64 = {.lhs = 81.0, .rhs = 4.5}},
        {.label = "mod_f64",
         .opcode = ORUS_JIT_IR_OP_MOD_F64,
         .kind = ORUS_JIT_VALUE_F64,
         .values.f64 = {.lhs = 81.0, .rhs = 4.5}},
    };

    bool success = true;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (!run_unsupported_opcode_case(&cases[i])) {
            success = false;
        }
    }
    return success;
}

int main(void) {
    if (!orus_jit_backend_is_available()) {
        printf("Baseline JIT backend unavailable; skipping backend tests.\n");
        return 0;
    }

    const char* filter = getenv("ORUS_JIT_BACKEND_TEST_FILTER");
    if (filter) {
        if (strcmp(filter, "call_native_gc") == 0) {
            return test_backend_call_native_triggers_gc_safepoint() ? 0 : 1;
        }
    }

    bool success = true;

    if (!test_jit_debug_disassembly_capture()) {
        fprintf(stderr, "jit debug disassembly capture test failed\n");
        success = false;
    }
    if (!test_jit_debug_guard_trace_and_loop_telemetry()) {
        fprintf(stderr,
                "jit debug guard trace and loop telemetry test failed\n");
        success = false;
    }
    if (!test_backend_helper_stub_executes()) {
        fprintf(stderr, "backend helper stub test failed\n");
        success = false;
    }
    if (!test_backend_dynasm_matches_linear_across_value_kinds()) {
        fprintf(stderr, "backend DynASM parity test failed\n");
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
    if (!test_backend_documents_unhandled_arithmetic_opcodes()) {
        fprintf(stderr,
                "backend unsupported arithmetic opcode fixtures failed\n");
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
    if (!test_backend_gc_safepoint_handles_heap_growth()) {
        fprintf(stderr, "backend GC safepoint stress test failed\n");
        success = false;
    }
    if (!test_backend_deopt_mid_gc_preserves_frame_alignment()) {
        fprintf(stderr,
                "backend GC + deopt frame reconciliation test failed\n");
        success = false;
    }
    if (!test_backend_typed_deopt_landing_pad_reuses_frame()) {
        fprintf(stderr, "backend typed deopt landing pad test failed\n");
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
