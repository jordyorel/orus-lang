//  Orus Language Project
 

// vm_core.c - VM initialization and core management
#include "vm_internal.h"
#include "runtime/builtins.h"
#include "runtime/memory.h"
#include "vm/vm_string_ops.h"
#include "vm/register_file.h"
#include "vm/jit_translation.h"
#include "vm/jit_debug.h"
#include "internal/logging.h"
#include "type/type.h"
#include <string.h>
#include <stdlib.h>

static const char*
jit_backend_entry_stub_failure_message(JITBackendStatus status) {
    switch (status) {
        case JIT_BACKEND_UNSUPPORTED:
            return "Baseline entry stub unsupported: native emitter unavailable or executable memory protections blocked code generation.";
        case JIT_BACKEND_OUT_OF_MEMORY:
            return "Baseline entry stub allocation failed: exhausted executable memory while emitting native code.";
        case JIT_BACKEND_ASSEMBLY_ERROR:
            return "Baseline entry stub assembly failed: native assembler rejected the generated code.";
        case JIT_BACKEND_OK:
            return "Baseline entry stub ready.";
        default:
            return "Failed to materialize baseline JIT entry stub.";
    }
}

VM vm; // Global VM instance

void initVM(void) {
    initTypeSystem();

    initMemory();

    if (globalStringTable.interned == NULL) {
        init_string_table(&globalStringTable);
    } else if (globalStringTable.threshold == 0) {
        // The table may have been pre-initialized by the caller (e.g. main.c)
        // to guarantee cleanup on early exits. Avoid reinitializing it here to
        // prevent leaking the previously allocated hashmap backing store.
        globalStringTable.threshold = 32;
    }

    init_register_file(&vm.register_file);

    // Legacy register initialization (for backward compatibility)
    for (int i = 0; i < REGISTER_COUNT; i++) {
        vm.registers[i] = BOOL_VAL(false); // Default value instead of NIL_VAL
    }

    memset(&vm.typed_regs, 0, sizeof(TypedRegisters));
    memset(&vm.typed_regs.root_window, 0, sizeof(TypedRegisterWindow));
    vm.typed_regs.root_window.generation = 0;
    typed_window_reset_live_mask(&vm.typed_regs.root_window);
    for (int i = 0; i < TYPED_REGISTER_WINDOW_SIZE; i++) {
        vm.typed_regs.root_window.reg_types[i] = REG_TYPE_NONE;
        vm.typed_regs.root_window.dirty[i] = false;
    }
    vm.typed_regs.root_window.next = NULL;
    vm.typed_regs.active_window = &vm.typed_regs.root_window;
    vm.typed_regs.free_windows = NULL;
    vm.typed_regs.window_version = 0;
    vm.typed_regs.active_depth = 0;
    vm.typed_regs.i32_regs = vm.typed_regs.root_window.i32_regs;
    vm.typed_regs.i64_regs = vm.typed_regs.root_window.i64_regs;
    vm.typed_regs.u32_regs = vm.typed_regs.root_window.u32_regs;
    vm.typed_regs.u64_regs = vm.typed_regs.root_window.u64_regs;
    vm.typed_regs.f64_regs = vm.typed_regs.root_window.f64_regs;
    vm.typed_regs.bool_regs = vm.typed_regs.root_window.bool_regs;
    vm.typed_regs.heap_regs = vm.typed_regs.root_window.heap_regs;
    vm.typed_regs.dirty = vm.typed_regs.root_window.dirty;
    vm.typed_regs.dirty_mask = vm.typed_regs.root_window.dirty_mask;
    vm.typed_regs.reg_types = vm.typed_regs.root_window.reg_types;
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.globals[i] = BOOL_VAL(false); // Default value instead of NIL_VAL
        vm.globalTypes[i] = NULL;
        vm.publicGlobals[i] = false;
        vm.mutableGlobals[i] = false;
        vm.variableNames[i].name = NULL;
        vm.variableNames[i].length = 0;
        vm.functions[i].start = 0;
        vm.functions[i].arity = 0;
        vm.functions[i].chunk = NULL;
        vm.functions[i].specialized_chunk = NULL;
        vm.functions[i].deopt_stub_chunk = NULL;
        vm.functions[i].tier = FUNCTION_TIER_BASELINE;
        vm.functions[i].deopt_handler = NULL;
        vm.functions[i].specialization_hits = 0;
        if (vm.functions[i].debug_name) {
            free(vm.functions[i].debug_name);
            vm.functions[i].debug_name = NULL;
        }
    }

    vm.variableCount = 0;
    vm.functionCount = 0;
    vm.frameCount = 0;
    vm.tryFrameCount = 0;
    vm.lastError = BOOL_VAL(false); // Default value instead of NIL_VAL
    vm_set_error_report_pending(false);
    vm.instruction_count = 0;
    vm.ticks = 0;
    vm.astRoot = NULL;
    vm.filePath = NULL;
    vm.currentLine = 0;
    vm.currentColumn = 1;
    vm.safe_register_reads = 0;
    vm.moduleCount = 0;
    vm.loadingModuleCount = 0;
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.loadingModules[i] = NULL;
    }
    vm.nativeFunctionCount = 0;
    for (int i = 0; i < MAX_NATIVES; ++i) {
        vm.nativeFunctions[i].name = NULL;
        vm.nativeFunctions[i].function = NULL;
        vm.nativeFunctions[i].arity = 0;
        vm.nativeFunctions[i].returnType = NULL;
    }
    vm.gcCount = 0;
    vm.lastExecutionTime = 0.0;

    memset(vm.profile, 0, sizeof(vm.profile));

    vm.openUpvalues = NULL;

    const char* envTrace = getenv("ORUS_TRACE");
    vm.trace = envTrace && envTrace[0] != '\0';

    vm.chunk = NULL;
    vm.ip = NULL;
    vm.isShuttingDown = false;  // Initialize shutdown flag

    // Initialize register file frame pointers
    vm.register_file.current_frame = NULL;
    vm.register_file.frame_stack = NULL;

    vm.jit_backend_status = JIT_BACKEND_UNSUPPORTED;
    vm.jit_backend_target = ORUS_JIT_BACKEND_TARGET_NATIVE;
    vm.jit_backend_message = NULL;
    vm.jit_backend = orus_jit_backend_create();
    vm.jit_enabled = false;
    memset(&vm.jit_entry_stub, 0, sizeof(vm.jit_entry_stub));
    vm.jit_cache.slots = NULL;
    vm.jit_cache.capacity = 0;
    vm.jit_cache.count = 0;
    vm.jit_cache.next_generation = 0;
    vm.jit_compilation_count = 0;
    vm.jit_invocation_count = 0;
    vm.jit_cache_hit_count = 0;
    vm.jit_cache_miss_count = 0;
    vm.jit_deopt_count = 0;
    vm.jit_translation_success_count = 0;
    orus_jit_translation_failure_log_init(&vm.jit_translation_failures);
    memset(&vm.jit_tier_skips, 0, sizeof(vm.jit_tier_skips));
    vm.jit_tier_skips.last_reason = ORUS_JIT_TIER_SKIP_REASON_NONE;
    vm.jit_tier_skips.last_translation_status = ORUS_JIT_TRANSLATE_STATUS_OK;
    vm.jit_tier_skips.last_backend_status = vm.jit_backend_status;
    vm.jit_tier_skips.last_function = UINT16_MAX;
    vm.jit_tier_skips.last_loop = UINT16_MAX;
    vm.jit_tier_skips.last_bytecode_offset = 0u;
    vm.jit_native_dispatch_count = 0;
    vm.jit_native_type_deopts = 0;
    vm.jit_native_frame_top = NULL;
    vm.jit_native_slow_path_pending = false;
    vm.jit_enter_cycle_total = 0;
    vm.jit_enter_cycle_samples = 0;
    vm.jit_enter_cycle_warmup_total = 0;
    vm.jit_enter_cycle_warmup_samples = 0;
    orus_jit_debug_reset();
    // Default to the full baseline rollout so production workloads gain
    // immediate access to floating-point and string helpers without requiring
    // a command-line override.
    orus_jit_rollout_set_stage(&vm, ORUS_JIT_ROLLOUT_STAGE_STRINGS);
    memset(vm.jit_loop_blocklist, 0, sizeof(vm.jit_loop_blocklist));
    vm.jit_pending_invalidate = false;
    memset(&vm.jit_pending_trigger, 0, sizeof(vm.jit_pending_trigger));
    if (vm.jit_backend) {
        const char* availability_message = NULL;
        vm.jit_backend_status = orus_jit_backend_availability(
            vm.jit_backend, &vm.jit_backend_target, &availability_message);
        vm.jit_backend_message = availability_message;
        if (vm.jit_backend_status != JIT_BACKEND_OK) {
            if (vm.jit_backend_message) {
                LOG_INFO("Disabling JIT backend: %s", vm.jit_backend_message);
            } else {
                LOG_INFO("Disabling JIT backend: unsupported host platform.");
            }
            orus_jit_backend_destroy(vm.jit_backend);
            vm.jit_backend = NULL;
        } else {
            JITEntry stub_entry;
            memset(&stub_entry, 0, sizeof(stub_entry));
            JITBackendStatus status =
                orus_jit_backend_compile_noop(vm.jit_backend, &stub_entry);
            vm.jit_backend_status = status;
            if (status == JIT_BACKEND_OK) {
                vm.jit_entry_stub = stub_entry;
                vm.jit_enabled = true;
            } else {
                if (stub_entry.code_ptr) {
                    orus_jit_backend_release_entry(vm.jit_backend, &stub_entry);
                }
                vm.jit_backend_message =
                    jit_backend_entry_stub_failure_message(status);
                LOG_WARN("Disabling JIT backend (status=%d): %s",
                         (int)status, vm.jit_backend_message);
                orus_jit_backend_destroy(vm.jit_backend);
                vm.jit_backend = NULL;
            }
        }
    } else {
        vm.jit_backend_status = JIT_BACKEND_OUT_OF_MEMORY;
        vm.jit_backend_message =
            "Failed to allocate Orus JIT backend instance.";
        LOG_ERROR("Failed to allocate Orus JIT backend; native tier disabled.");
    }
}

void freeVM(void) {
    // Set shutdown flag to indicate VM is in cleanup phase
    vm.isShuttingDown = true;
    
    // Phase 1: Free register file resources
    free_register_file(&vm.register_file);

    for (int i = 0; i < vm.functionCount; ++i) {
        Function* function = &vm.functions[i];
        if (function->specialized_chunk) {
            freeChunk(function->specialized_chunk);
            free(function->specialized_chunk);
            function->specialized_chunk = NULL;
        }
        if (function->deopt_stub_chunk) {
            freeChunk(function->deopt_stub_chunk);
            free(function->deopt_stub_chunk);
            function->deopt_stub_chunk = NULL;
        }
        if (function->chunk) {
            freeChunk(function->chunk);
            free(function->chunk);
            function->chunk = NULL;
        }
        if (function->debug_name) {
            free(function->debug_name);
            function->debug_name = NULL;
        }
        function->tier = FUNCTION_TIER_BASELINE;
        function->deopt_handler = NULL;
        function->specialization_hits = 0;
    }

    vm_jit_flush_entries();
    if (vm.jit_cache.slots) {
        for (size_t i = 0; i < vm.jit_cache.capacity; ++i) {
            vm.jit_cache.slots[i].function_index = UINT16_MAX;
            vm.jit_cache.slots[i].loop_index = UINT16_MAX;
        }
        free(vm.jit_cache.slots);
        vm.jit_cache.slots = NULL;
    }
    vm.jit_cache.capacity = 0;
    vm.jit_cache.count = 0;
    vm.jit_cache.next_generation = 0;

    if (vm.jit_backend) {
        if (vm.jit_entry_stub.code_ptr) {
            orus_jit_backend_release_entry(vm.jit_backend, &vm.jit_entry_stub);
            memset(&vm.jit_entry_stub, 0, sizeof(vm.jit_entry_stub));
        }
        orus_jit_backend_destroy(vm.jit_backend);
        vm.jit_backend = NULL;
    }
    orus_jit_debug_reset();
    vm.jit_enabled = false;
    vm.jit_compilation_count = 0;
    vm.jit_invocation_count = 0;
    vm.jit_cache_hit_count = 0;
    vm.jit_cache_miss_count = 0;
    vm.jit_deopt_count = 0;
    vm.functionCount = 0;

    // Free global string table
    free_string_table(&globalStringTable);
    
    freeObjects();
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.variableNames[i].name = NULL;
        vm.globalTypes[i] = NULL;
        vm.publicGlobals[i] = false;
        vm.mutableGlobals[i] = false;
    }
    vm.astRoot = NULL;
    vm.chunk = NULL;
    vm.ip = NULL;
}

