//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/main.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2022 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Defines the Orus CLI entry point, bootstrapping the VM and the interactive REPL.

// Orus Language Main Interpreter
// This is the main entry point for the Orus programming language

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/vm.h"
#include "vm/jit_backend.h"
#include "vm/jit_translation.h"
#include "vm/jit_debug.h"
#include "vm/jit_benchmark.h"
#include "public/common.h"
#include "internal/error_reporting.h"
#include "internal/logging.h"
#include "errors/error_interface.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "tools/repl.h"
#include "public/version.h"
#include "config/config.h"
#include "vm/vm_profiling.h"
#include "debug/debug_config.h"

// Bytecode debugging function
void dumpBytecode(Chunk* chunk);


static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        free_string_table(&globalStringTable);
        return NULL;
    }
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        free_string_table(&globalStringTable);
        return NULL;
    }
    
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        free(buffer);
        fclose(file);
        free_string_table(&globalStringTable);
        return NULL;
    }
    
    buffer[bytesRead] = '\0';
    
    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    if (source == NULL) {
        // readFile already prints an error message when it fails
        free_string_table(&globalStringTable);
        exit(65);
    }
    
    // Set the file path for better error reporting
    vm.filePath = path;
    
    // Initialize error reporting with arena allocation
    ErrorReportResult error_init = init_error_reporting();
    if (error_init != ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "Failed to initialize error reporting\n");
        free(source);
        free_string_table(&globalStringTable);
        exit(70);
    }
    
    // Set source text for error reporting with bounds checking
    size_t source_len = strlen(source);
    ErrorReportResult source_result = set_source_text(source, source_len);
    if (source_result != ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "Failed to set source text for error reporting\n");
        cleanup_error_reporting();
        free(source);
        free_string_table(&globalStringTable);
        exit(70);
    }
    
    InterpretResult result = interpret(source);
    
    // Clean up error reporting before freeing source
    cleanup_error_reporting();
    free(source);
    vm.filePath = NULL;
    
    if (result == INTERPRET_COMPILE_ERROR) {
        fprintf(stderr, "Compilation failed for \"%s\".\n", path);
        if (vm.devMode) {
            fprintf(stderr, "Debug: Check if the syntax is supported and tokens are properly recognized.\n");
            fprintf(stderr, "Debug: Try running with simpler syntax to isolate the issue.\n");
        }
        free_string_table(&globalStringTable);
        exit(65);
    }
    if (result == INTERPRET_RUNTIME_ERROR) {
        // Enhanced error reporting is now handled in runtimeError() function
        free_string_table(&globalStringTable);
        exit(70);
    }
}

static const char*
jit_backend_status_name(JITBackendStatus status) {
    switch (status) {
        case JIT_BACKEND_OK:
            return "ok";
        case JIT_BACKEND_UNSUPPORTED:
            return "unsupported";
        case JIT_BACKEND_OUT_OF_MEMORY:
            return "out_of_memory";
        case JIT_BACKEND_ASSEMBLY_ERROR:
            return "assembly_error";
        default:
            return "unknown";
    }
}

static void print_guard_trace_summary(const OrusJitRunStats* stats) {
    if (!stats || !stats->guard_trace.events || stats->guard_trace.count == 0u) {
        printf("[JIT Benchmark] guard trace: no guard exits were recorded.\n");
        return;
    }

    const OrusJitGuardTraceEvent* events = stats->guard_trace.events;
    size_t guard_count = stats->guard_trace.count;

    typedef struct {
        uint16_t function_index;
        uint16_t loop_index;
        uint32_t instruction_index;
        char reason[sizeof(events->reason)];
        uint64_t hits;
        uint64_t first_timestamp;
        uint64_t last_timestamp;
    } OrusGuardTraceSummary;

    OrusGuardTraceSummary* summaries = (OrusGuardTraceSummary*)calloc(
        guard_count, sizeof(*summaries));
    if (!summaries) {
        printf("[JIT Benchmark] guard trace: insufficient memory for summaries; printing raw events.\n");
        for (size_t i = 0; i < guard_count; ++i) {
            const OrusJitGuardTraceEvent* event = &events[i];
            const char* reason =
                (event->reason[0] != '\0') ? event->reason : "(no reason)";
            printf("    * guard event #%zu: func=%u loop=%u ir_index=%u hits=1 reason=%s timestamp=%" PRIu64 "\n",
                   i,
                   (unsigned)event->function_index,
                   (unsigned)event->loop_index,
                   event->instruction_index,
                   reason,
                   (uint64_t)event->timestamp);
        }
        return;
    }

    size_t summary_count = 0u;
    for (size_t i = 0; i < guard_count; ++i) {
        const OrusJitGuardTraceEvent* event = &events[i];
        size_t j = 0u;
        for (; j < summary_count; ++j) {
            OrusGuardTraceSummary* summary = &summaries[j];
            if (summary->function_index == event->function_index &&
                summary->loop_index == event->loop_index &&
                summary->instruction_index == event->instruction_index &&
                strncmp(summary->reason, event->reason, sizeof(summary->reason)) ==
                    0) {
                summary->hits++;
                if (event->timestamp < summary->first_timestamp) {
                    summary->first_timestamp = event->timestamp;
                }
                if (event->timestamp > summary->last_timestamp) {
                    summary->last_timestamp = event->timestamp;
                }
                break;
            }
        }
        if (j == summary_count) {
            OrusGuardTraceSummary* summary = &summaries[summary_count++];
            summary->function_index = event->function_index;
            summary->loop_index = event->loop_index;
            summary->instruction_index = event->instruction_index;
            strncpy(summary->reason,
                    event->reason,
                    sizeof(summary->reason) - 1u);
            summary->reason[sizeof(summary->reason) - 1u] = '\0';
            summary->hits = 1u;
            summary->first_timestamp = event->timestamp;
            summary->last_timestamp = event->timestamp;
        }
    }

    size_t dominant_index = 0u;
    uint64_t dominant_hits = 0u;
    for (size_t i = 0; i < summary_count; ++i) {
        if (summaries[i].hits > dominant_hits) {
            dominant_hits = summaries[i].hits;
            dominant_index = i;
        }
    }

    printf("[JIT Benchmark] guard trace events captured: %zu\n", guard_count);
    if (summary_count > 0u) {
        OrusGuardTraceSummary* dominant = &summaries[dominant_index];
        const char* reason =
            (dominant->reason[0] != '\0') ? dominant->reason : "(no reason)";
        printf("[JIT Benchmark] dominant bailout: func=%u loop=%u ir_index=%u hits=%" PRIu64
               " first_ts=%" PRIu64 " last_ts=%" PRIu64 " reason=%s\n",
               (unsigned)dominant->function_index,
               (unsigned)dominant->loop_index,
               dominant->instruction_index,
               dominant->hits,
               dominant->first_timestamp,
               dominant->last_timestamp,
               reason);

        printf("[JIT Benchmark] guard bailout breakdown:\n");
        for (size_t i = 0; i < summary_count; ++i) {
            OrusGuardTraceSummary* summary = &summaries[i];
            const char* summary_reason =
                (summary->reason[0] != '\0') ? summary->reason : "(no reason)";
            printf("    - func=%u loop=%u ir_index=%u hits=%" PRIu64
                   " first_ts=%" PRIu64 " last_ts=%" PRIu64 " reason=%s\n",
                   (unsigned)summary->function_index,
                   (unsigned)summary->loop_index,
                   summary->instruction_index,
                   summary->hits,
                   summary->first_timestamp,
                   summary->last_timestamp,
                   summary_reason);
        }
    }

    free(summaries);
}

// Note: showUsage and showVersion functions are now handled by the configuration system
// See config_print_help() and config_print_version() in src/config/config.c

int main(int argc, const char* argv[]) {
    // Initialize logging system first (can be configured via environment variables)
    initLogger(LOG_INFO);

    // Create and initialize configuration
    OrusConfig* config = config_create();
    if (!config) {
        fprintf(stderr, "Error: Failed to create configuration\n");
        shutdownLogger();
        return EXIT_USAGE_ERROR;
    }

    // Load configuration from environment variables
    config_load_from_env(config);

    // Load configuration file if specified in environment
    const char* config_file = getenv(ORUS_CONFIG_FILE);
    if (config_file) {
        config_load_from_file(config, config_file);
    }
    
    // Parse command line arguments (highest precedence)
    if (!config_parse_args(config, argc, argv)) {
        // config_parse_args returns false for help/version or errors
        config_destroy(config);
        shutdownLogger();
        return 0; // Help/version shown, normal exit
    }

    // Validate configuration
    if (!config_validate(config)) {
        config_print_errors(config);
        config_destroy(config);
        shutdownLogger();
        return EXIT_USAGE_ERROR;
    }

    if (config->jit_benchmark_mode) {
        if (!config->input_file) {
            fprintf(stderr,
                    "Error: --jit-benchmark requires an input program.\n");
            config_destroy(config);
            shutdownLogger();
            return EXIT_USAGE_ERROR;
        }

        OrusJitRunStats interpreter_stats = {0};
        OrusJitRunStats jit_stats = {0};
        if (!vm_jit_benchmark_file(config->input_file, &interpreter_stats,
                                   &jit_stats)) {
            fprintf(stderr,
                    "Failed to execute JIT benchmark for \"%s\".\n",
                    config->input_file);
            config_destroy(config);
            shutdownLogger();
            return EXIT_RUNTIME_ERROR;
        }

        double interpreter_ms = interpreter_stats.duration_ns / 1e6;
        double jit_ms = jit_stats.duration_ns / 1e6;
        double speedup = (jit_ms > 0.0) ? (interpreter_ms / jit_ms) : 0.0;

        printf("[JIT Benchmark] interpreter runtime: %.2f ms\n",
               interpreter_ms);
        printf("[JIT Benchmark] jit runtime: %.2f ms\n", jit_ms);
        printf("[JIT Benchmark] speedup: %.2fx\n", speedup);
        printf("[JIT Benchmark] translations: %" PRIu64
               " succeeded, %" PRIu64 " failed\n",
               jit_stats.translation_success,
               jit_stats.translation_failure);
        if (jit_stats.enter_cycle_samples > 0) {
            printf("[JIT Benchmark] native steady-state latency: %.0f ns "
                   "(samples=%" PRIu64 ", total=%" PRIu64 ")\n",
                   jit_stats.enter_cycle_average,
                   jit_stats.enter_cycle_samples,
                   jit_stats.enter_cycle_total);
        }
        if (jit_stats.enter_cycle_warmup_samples > 0) {
            printf("[JIT Benchmark] native warmup latency: %.0f ns (samples=%" PRIu64
                   ", total=%" PRIu64 ")\n",
                   jit_stats.enter_cycle_warmup_average,
                   jit_stats.enter_cycle_warmup_samples,
                   jit_stats.enter_cycle_warmup_total);
        }
        printf("[JIT Benchmark] rollout stage: %s (mask=0x%X)\n",
               orus_jit_rollout_stage_name(jit_stats.rollout_stage),
               jit_stats.rollout_mask);
        uint64_t rollout_blocked =
            (jit_stats.rollout_stage < ORUS_JIT_ROLLOUT_STAGE_STRINGS)
                ? jit_stats.failure_log
                      .reason_counts[ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED]
                : 0u;
        if (jit_stats.failure_log.total_failures > 0) {
            printf("[JIT Benchmark] failure breakdown:\n");
            for (size_t i = 0; i < ORUS_JIT_TRANSLATE_STATUS_COUNT; ++i) {
                uint64_t count = jit_stats.failure_log.reason_counts[i];
                if (count == 0) {
                    continue;
                }
                printf("    - %s: %" PRIu64 "\n",
                       orus_jit_translation_status_name(
                           (OrusJitTranslationStatus)i),
                       count);
            }
            printf("    - categorized failures:\n");
            for (size_t category = 0;
                 category < ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_COUNT;
                 ++category) {
                uint64_t count =
                    jit_stats.failure_log.category_counts[category];
                if (count == 0) {
                    continue;
                }
                printf("        * %s: %" PRIu64 "\n",
                       orus_jit_translation_failure_category_name(
                           (OrusJitTranslationFailureCategory)category),
                       count);
            }
            printf("    - failure by value kind:\n");
            double total_failures =
                (double)jit_stats.failure_log.total_failures;
            for (size_t kind = 0; kind < ORUS_JIT_VALUE_KIND_COUNT; ++kind) {
                uint64_t count =
                    jit_stats.failure_log.value_kind_counts[kind];
                if (count == 0) {
                    continue;
                }
                double share =
                    (total_failures > 0.0)
                        ? (100.0 * (double)count / total_failures)
                        : 0.0;
                printf("        * %s: %" PRIu64 " (%.1f%%)\n",
                       orus_jit_value_kind_name((OrusJitValueKind)kind),
                       count,
                       share);
            }
            if (jit_stats.failure_log.count > 0) {
                size_t history_size = ORUS_JIT_TRANSLATION_FAILURE_HISTORY;
                size_t last_index =
                    (jit_stats.failure_log.next_index + history_size - 1u) %
                    history_size;
                const OrusJitTranslationFailureRecord* last_failure =
                    &jit_stats.failure_log.records[last_index];
                printf("    - last failure: reason=%s opcode=%u kind=%u "
                       "func=%u loop=%u bytecode=%u\n",
                       orus_jit_translation_status_name(last_failure->status),
                       (unsigned)last_failure->opcode,
                       (unsigned)last_failure->value_kind,
                       (unsigned)last_failure->function_index,
                       (unsigned)last_failure->loop_index,
                       last_failure->bytecode_offset);
            }
        }
        uint64_t tier_skip_total =
            orus_jit_tier_skip_total(&jit_stats.tier_skips);
        if (tier_skip_total > 0) {
            printf("[JIT Benchmark] tier-up skips (%" PRIu64 " total):\n",
                   tier_skip_total);
            for (size_t reason = 0; reason < ORUS_JIT_TIER_SKIP_REASON_COUNT;
                 ++reason) {
                uint64_t count =
                    jit_stats.tier_skips.reason_counts[reason];
                if (count == 0) {
                    continue;
                }
                printf("    - %s: %" PRIu64 "\n",
                       orus_jit_tier_skip_reason_name(
                           (OrusJitTierSkipReason)reason),
                       count);
            }
            printf("[JIT Benchmark] last skip: reason=%s func=%u loop=%u "
                   "translation=%s backend=%d bytecode=%u\n",
                   orus_jit_tier_skip_reason_name(
                       jit_stats.tier_skips.last_reason),
                   (unsigned)jit_stats.tier_skips.last_function,
                   (unsigned)jit_stats.tier_skips.last_loop,
                   orus_jit_translation_status_name(
                       jit_stats.tier_skips.last_translation_status),
                   (int)jit_stats.tier_skips.last_backend_status,
                   jit_stats.tier_skips.last_bytecode_offset);
        }
        if (rollout_blocked > 0) {
            printf("[JIT Benchmark] notice: %" PRIu64
                   " translations blocked by rollout stage %s\n",
                   rollout_blocked,
                   orus_jit_rollout_stage_name(jit_stats.rollout_stage));
        }
        printf("[JIT Benchmark] native compilations recorded: %" PRIu64 "\n",
               jit_stats.compilation_count);
        printf("[JIT Benchmark] native invocations recorded: %" PRIu64
               ", type guard bailouts: %" PRIu64 "\n",
               jit_stats.invocations,
               jit_stats.native_type_deopts);
        printf("[JIT Benchmark] native dispatches: %" PRIu64
               ", cache hits: %" PRIu64 ", cache misses: %" PRIu64
               ", deopts: %" PRIu64 "\n",
               jit_stats.native_dispatches,
               jit_stats.cache_hits,
               jit_stats.cache_misses,
               jit_stats.deopts);

        double coverage =
            (jit_stats.invocations > 0)
                ? (100.0 * (double)jit_stats.native_dispatches /
                   (double)jit_stats.invocations)
                : 0.0;
        printf("[JIT Benchmark] native coverage: %.1f%% (%" PRIu64 "/%" PRIu64 ")\n",
               coverage,
               jit_stats.native_dispatches,
               jit_stats.invocations);

        const char* backend_message =
            (jit_stats.backend_message && jit_stats.backend_message[0] != '\0')
                ? jit_stats.backend_message
                : "(no message)";
        if (!jit_stats.jit_backend_enabled) {
            printf("[JIT Benchmark] backend disabled before execution: status=%s"
                   " message=%s\n",
                   jit_backend_status_name(jit_stats.backend_status),
                   backend_message);
        } else if (jit_stats.backend_status != JIT_BACKEND_OK) {
            printf("[JIT Benchmark] backend status: status=%s message=%s\n",
                   jit_backend_status_name(jit_stats.backend_status),
                   backend_message);
        }

        if (jit_stats.translation_success == 0 ||
            jit_stats.native_dispatches == 0) {
            printf("[JIT Benchmark] warning: baseline tier did not translate this "
                   "program; execution remained in the interpreter.\n");
        }

        print_guard_trace_summary(&jit_stats);

        vm_jit_run_stats_release(&jit_stats);
        vm_jit_run_stats_release(&interpreter_stats);

        config_destroy(config);
        shutdownLogger();
        return 0;
    }

    // Initialize debug system
    debug_init();

    // Apply debug settings from configuration to debug system
    config_apply_debug_settings(config);

    // Set global configuration for access by other modules
    config_set_global(config);

    // Load configuration file if specified via command line
    if (config->config_file && config->config_file != config_file) {
        config_load_from_file(config, config->config_file);
        // Re-validate after loading config file
        if (!config_validate(config)) {
            config_print_errors(config);
            config_destroy(config);
            shutdownLogger();
            return EXIT_USAGE_ERROR;
        }
    }

    // Strict leak-free: initialize global string table before running programs
    init_string_table(&globalStringTable);

    // Initialize feature-based error system
    init_feature_errors();
    init_type_errors();
    init_variable_errors();

    // Initialize VM profiling system
    initVMProfiling();

    // Initialize VM with configuration
    initVM();

    // Apply configuration to VM
    vm.trace = config->trace_execution;
    vm.devMode = config->debug_mode;
    if (config->jit_rollout_stage >= 0 &&
        config->jit_rollout_stage < ORUS_JIT_ROLLOUT_STAGE_COUNT) {
        orus_jit_rollout_set_stage(&vm,
                                   (OrusJitRolloutStage)config->jit_rollout_stage);
    }

    bool jit_requested = config->enable_jit;
    const char* force_helper_stub = getenv("ORUS_JIT_FORCE_HELPER_STUB");
    bool helper_stub_forced = force_helper_stub && force_helper_stub[0] != '\0';
    if (jit_requested && vm.jit_backend != NULL && !helper_stub_forced) {
        orus_jit_backend_set_linear_emitter_enabled(true);
    } else {
        orus_jit_backend_clear_linear_emitter_override();
    }
    vm.jit_enabled = jit_requested && vm.jit_backend != NULL;
    if (!jit_requested) {
        vm.jit_backend_message = "Baseline JIT disabled by configuration.";
    } else if (jit_requested && vm.jit_backend == NULL &&
               vm.jit_backend_message == NULL) {
        vm.jit_backend_message = "Baseline JIT unavailable on this platform.";
    }

    // Configure VM profiling based on command line options
    if (config->vm_profiling_enabled) {
        ProfilingFlags flags = PROFILE_NONE;
        
        if (config->profile_instructions) flags |= PROFILE_INSTRUCTIONS;
        if (config->profile_hot_paths) flags |= PROFILE_HOT_PATHS;
        if (config->profile_functions) flags |= PROFILE_FUNCTION_CALLS;
        if (config->profile_registers) flags |= PROFILE_REGISTER_USAGE;
        if (config->profile_memory_access) flags |= PROFILE_MEMORY_ACCESS;
        if (config->profile_branches) flags |= PROFILE_BRANCH_PREDICTION;
        
        enableProfiling(flags);
        
        if (config->verbose && !config->quiet) {
            printf("VM Profiling enabled with flags: 0x%X\n", flags);
        }
    }
    
    // Apply VM configuration settings
    // Note: In a real implementation, we'd need to modify the VM to accept these parameters
    // For now, we'll just set the trace and debug flags
    
    if (config->verbose && !config->quiet) {
        // printf("Orus Language Interpreter starting...\n");
        if (config->show_ast || config->show_bytecode || config->show_tokens || config->show_optimization_stats) {
            printf("Development tools enabled: ");
            if (config->show_ast) printf("AST ");
            if (config->show_bytecode) printf("Bytecode ");
            if (config->show_tokens) printf("Tokens ");
            if (config->show_optimization_stats) printf("OptStats ");
            printf("\n");
        }
    }
    
    // Handle special development modes
    if (config->benchmark_mode && !config->quiet) {
        printf("Benchmark mode enabled\n");
    }
    
    // Run file or REPL based on configuration
    if (config->repl_mode) {
        if (!config->quiet) {
            printf("Starting REPL mode...\n");
        }
        repl();
    } else {
        runFile(config->input_file);
    }
    
    // Show optimization statistics if requested
    if (config->show_optimization_stats && !config->quiet) {
        // printGlobalOptimizationStats(); // TODO: Implementation missing
        printf("Optimization statistics: Feature not yet implemented\n");
    }
    
    // Cleanup and profiling export
    if (config->vm_profiling_enabled) {
        if (config->profile_output) {
            exportProfilingData(config->profile_output);
        } else if (config->verbose && !config->quiet) {
            dumpProfilingStats();
        }
        shutdownVMProfiling();
    }

    freeVM();
    free_string_table(&globalStringTable);
    config_destroy(config);

    // LOG_INFO("Orus Language Interpreter shutting down");
    shutdownLogger();
    return 0;
}
