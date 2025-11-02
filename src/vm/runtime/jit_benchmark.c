// Orus Language Project

#define _POSIX_C_SOURCE 199309L

#include "vm/jit_benchmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "errors/error_interface.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "vm/jit_backend.h"
#include "vm/jit_debug.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"

static double
vm_jit_timespec_diff_ns(const struct timespec* start,
                        const struct timespec* end) {
    if (!start || !end) {
        return 0.0;
    }

    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;

    if (nsec < 0) {
        --sec;
        nsec += 1000000000L;
    }

    return (double)sec * 1e9 + (double)nsec;
}

static char*
vm_jit_read_file(const char* path, size_t* out_length) {
    if (!path) {
        return NULL;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char* buffer = (char*)malloc((size_t)size + 1u);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(buffer, 1u, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_length) {
        *out_length = (size_t)size;
    }
    return buffer;
}

bool
vm_jit_run_source_benchmark(const char* source,
                            size_t source_len,
                            const char* path,
                            bool enable_jit,
                            OrusJitRunStats* stats) {
    if (!source || source_len == 0) {
        return false;
    }

    if (stats) {
        stats->guard_trace.events = NULL;
        stats->guard_trace.count = 0u;
    }

    OrusJitDebugConfig previous_debug_config = {0};
    OrusJitDebugConfig benchmark_debug_config = {0};
    bool restore_debug_config = false;
    bool restore_linear_emitter = false;

    if (enable_jit) {
        previous_debug_config = orus_jit_debug_get_config();
        benchmark_debug_config = previous_debug_config;
        benchmark_debug_config.capture_guard_traces = true;
        restore_debug_config = true;
    }

    bool string_table_ready = false;
    bool error_system_ready = false;
    bool profiling_ready = false;
    bool vm_ready = false;

    init_string_table(&globalStringTable);
    string_table_ready = true;

    if (init_error_reporting() != ERROR_REPORT_SUCCESS) {
        goto cleanup;
    }
    error_system_ready = true;

    if (set_source_text(source, source_len) != ERROR_REPORT_SUCCESS) {
        goto cleanup;
    }

    init_feature_errors();
    init_type_errors();
    init_variable_errors();

    initVMProfiling();
    profiling_ready = true;

    initVM();
    vm_ready = true;

    if (vm.jit_backend != NULL) {
        orus_jit_backend_linear_stats_reset();
    }

    if (enable_jit) {
        orus_jit_debug_set_config(&benchmark_debug_config);
    }

    if (enable_jit && vm.jit_backend != NULL) {
        orus_jit_backend_set_linear_emitter_enabled(true);
        restore_linear_emitter = true;
    }

    vm.jit_enabled = enable_jit && vm.jit_backend != NULL;
    vm.filePath = path;

    struct timespec start = {0};
    struct timespec end = {0};

    clock_gettime(CLOCK_MONOTONIC, &start);
    InterpretResult result = interpret(source);
    clock_gettime(CLOCK_MONOTONIC, &end);

    vm.filePath = NULL;

    if (result != INTERPRET_OK) {
        goto cleanup;
    }

    if (stats) {
        memset(stats, 0, sizeof(*stats));
        stats->duration_ns = vm_jit_timespec_diff_ns(&start, &end);
        stats->compilation_count = vm.jit_compilation_count;
        stats->translation_success = vm.jit_translation_success_count;
        stats->translation_failure = vm.jit_translation_failures.total_failures;
        stats->cache_hits = vm.jit_cache_hit_count;
        stats->cache_misses = vm.jit_cache_miss_count;
        stats->invocations = vm.jit_invocation_count;
        stats->native_dispatches = vm.jit_native_dispatch_count;
        stats->native_type_deopts = vm.jit_native_type_deopts;
        stats->deopts = vm.jit_deopt_count;
        stats->enter_cycle_total = vm.jit_enter_cycle_total;
        stats->enter_cycle_samples = vm.jit_enter_cycle_samples;
        stats->enter_cycle_warmup_total = vm.jit_enter_cycle_warmup_total;
        stats->enter_cycle_warmup_samples = vm.jit_enter_cycle_warmup_samples;
        stats->enter_cycle_average =
            (stats->enter_cycle_samples > 0)
                ? (double)stats->enter_cycle_total /
                      (double)stats->enter_cycle_samples
                : 0.0;
        stats->enter_cycle_warmup_average =
            (stats->enter_cycle_warmup_samples > 0)
                ? (double)stats->enter_cycle_warmup_total /
                      (double)stats->enter_cycle_warmup_samples
                : 0.0;
        stats->failure_log = vm.jit_translation_failures;
        stats->rollout_stage = vm.jit_rollout.stage;
        stats->rollout_mask = vm.jit_rollout.enabled_kind_mask;
        stats->jit_backend_enabled = vm.jit_enabled;
        stats->backend_status = vm.jit_backend_status;
        stats->backend_message = vm.jit_backend_message;
        stats->tier_skips = vm.jit_tier_skips;
        if (vm.jit_backend != NULL) {
            OrusJitLinearEmitterStats linear_stats = {0};
            if (orus_jit_backend_linear_stats(&linear_stats)) {
                stats->linear_attempts = linear_stats.attempts;
                stats->linear_successes = linear_stats.successes;
                stats->linear_failures = linear_stats.failures;
                stats->linear_last_status = linear_stats.last_status;
                stats->linear_last_function = linear_stats.last_function_index;
                stats->linear_last_loop = linear_stats.last_loop_index;
                stats->linear_last_instruction_count =
                    linear_stats.last_instruction_count;
                stats->linear_last_code_size = linear_stats.last_code_size;
            }
        }
        size_t guard_count = orus_jit_debug_guard_trace_count();
        if (guard_count > 0u) {
            OrusJitGuardTraceEvent* guard_buffer =
                (OrusJitGuardTraceEvent*)calloc(guard_count,
                                                sizeof(*guard_buffer));
            if (guard_buffer) {
                size_t copied = orus_jit_debug_copy_guard_traces(
                    guard_buffer, guard_count);
                if (copied == 0u) {
                    free(guard_buffer);
                } else {
                    if (copied < guard_count) {
                        OrusJitGuardTraceEvent* resized =
                            (OrusJitGuardTraceEvent*)realloc(
                                guard_buffer,
                                copied * sizeof(*guard_buffer));
                        if (resized) {
                            guard_buffer = resized;
                        }
                    }
                    stats->guard_trace.events = guard_buffer;
                    stats->guard_trace.count = copied;
                }
            }
        }
    }

    cleanup_error_reporting();
    error_system_ready = false;

    freeVM();
    vm_ready = false;

    shutdownVMProfiling();
    profiling_ready = false;

    free_string_table(&globalStringTable);
    string_table_ready = false;

    if (restore_debug_config) {
        orus_jit_debug_set_config(&previous_debug_config);
    }

    if (restore_linear_emitter) {
        orus_jit_backend_clear_linear_emitter_override();
    }
    return true;

cleanup:
    if (error_system_ready) {
        cleanup_error_reporting();
        error_system_ready = false;
    }
    if (vm_ready) {
        freeVM();
        vm_ready = false;
    }
    if (profiling_ready) {
        shutdownVMProfiling();
        profiling_ready = false;
    }
    if (string_table_ready) {
        free_string_table(&globalStringTable);
        string_table_ready = false;
    }
    if (restore_debug_config) {
        orus_jit_debug_set_config(&previous_debug_config);
    }
    if (restore_linear_emitter) {
        orus_jit_backend_clear_linear_emitter_override();
    }
    return false;
}

bool
vm_jit_benchmark_file(const char* path,
                      OrusJitRunStats* interpreter_stats,
                      OrusJitRunStats* jit_stats) {
    size_t source_len = 0u;
    char* source = vm_jit_read_file(path, &source_len);
    if (!source) {
        return false;
    }

    bool ok_interpreter = vm_jit_run_source_benchmark(
        source, source_len, path, /*enable_jit=*/false, interpreter_stats);
    bool ok_jit = false;

    if (ok_interpreter) {
        ok_jit = vm_jit_run_source_benchmark(
            source, source_len, path, /*enable_jit=*/true, jit_stats);
    }

    free(source);
    return ok_interpreter && ok_jit;
}

void
vm_jit_run_stats_release(OrusJitRunStats* stats) {
    if (!stats) {
        return;
    }
    free(stats->guard_trace.events);
    stats->guard_trace.events = NULL;
    stats->guard_trace.count = 0u;
}
