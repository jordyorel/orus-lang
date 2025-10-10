// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/compiler/backend/specialization/profiling_feedback.c
// Description: Captures profiling feedback from the VM and prepares
//              specialization plans for the compiler backend.

#include "compiler/specialization_feedback.h"
#include "compiler/compiler.h"
#include "internal/strutil.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char* duplicate_function_name(Function* function, int index) {
    if (function && function->debug_name) {
        return orus_strdup(function->debug_name);
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "<fn_%d>", index);
    return orus_strdup(buffer);
}

static void collect_feedback_snapshot(CompilerProfilingFeedback* feedback) {
    feedback->functions = NULL;
    feedback->function_count = 0;

    if (!(g_profiling.enabledFlags & PROFILE_FUNCTION_CALLS) || !g_profiling.isActive) {
        return;
    }

    extern VM vm;
    if (vm.functionCount <= 0) {
        return;
    }

    feedback->functions = calloc((size_t)vm.functionCount, sizeof(FunctionSpecializationHint));
    if (!feedback->functions) {
        return;
    }

    for (int i = 0; i < vm.functionCount; ++i) {
        Function* function = &vm.functions[i];
        if (!function || !function->chunk) {
            continue;
        }

        char* name_copy = duplicate_function_name(function, i);
        uint64_t hits = getFunctionHitCount(function, false);
        bool eligible = hits >= FUNCTION_SPECIALIZATION_THRESHOLD;

        FunctionSpecializationHint* hint = &feedback->functions[feedback->function_count++];
        hint->name = name_copy;
        hint->hit_count = hits;
        hint->function_index = i;
        hint->arity = function->arity;
        hint->eligible = eligible;
    }
}

void compiler_refresh_feedback(struct CompilerContext* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->profiling_feedback) {
        compiler_free_profiling_feedback(ctx->profiling_feedback);
        free(ctx->profiling_feedback);
        ctx->profiling_feedback = NULL;
    }

    CompilerProfilingFeedback* feedback = malloc(sizeof(CompilerProfilingFeedback));
    if (!feedback) {
        return;
    }

    collect_feedback_snapshot(feedback);
    ctx->profiling_feedback = feedback;
}

void compiler_free_profiling_feedback(CompilerProfilingFeedback* feedback) {
    if (!feedback) {
        return;
    }

    if (feedback->functions) {
        for (size_t i = 0; i < feedback->function_count; ++i) {
            free(feedback->functions[i].name);
        }
        free(feedback->functions);
    }

    feedback->functions = NULL;
    feedback->function_count = 0;
}

const FunctionSpecializationHint*
compiler_find_specialization_hint(const CompilerProfilingFeedback* feedback,
                                  const char* name) {
    if (!feedback || !feedback->functions || feedback->function_count == 0 || !name) {
        return NULL;
    }

    for (size_t i = 0; i < feedback->function_count; ++i) {
        const FunctionSpecializationHint* hint = &feedback->functions[i];
        if (!hint->name) {
            continue;
        }
        if (strcmp(hint->name, name) == 0) {
            return hint;
        }
    }

    return NULL;
}

static BytecodeBuffer* clone_bytecode_buffer(const BytecodeBuffer* source) {
    if (!source) {
        return NULL;
    }

    BytecodeBuffer* clone = init_bytecode_buffer();
    if (!clone) {
        return NULL;
    }

    if (source->count > 0) {
        free(clone->instructions);
        clone->instructions = malloc(source->count);
        if (!clone->instructions) {
            free_bytecode_buffer(clone);
            return NULL;
        }
        memcpy(clone->instructions, source->instructions, source->count);
        clone->capacity = source->count;
    }

    clone->count = source->count;

    if (source->source_lines && source->count > 0) {
        free(clone->source_lines);
        free(clone->source_columns);
        free(clone->source_files);

        clone->source_lines = malloc(sizeof(int) * source->count);
        clone->source_columns = malloc(sizeof(int) * source->count);
        clone->source_files = malloc(sizeof(const char*) * source->count);
        if (!clone->source_lines || !clone->source_columns || !clone->source_files) {
            free_bytecode_buffer(clone);
            return NULL;
        }
        memcpy(clone->source_lines, source->source_lines, sizeof(int) * source->count);
        memcpy(clone->source_columns, source->source_columns, sizeof(int) * source->count);
        memcpy(clone->source_files, source->source_files, sizeof(const char*) * source->count);
    }

    clone->current_location = source->current_location;
    clone->has_current_location = source->has_current_location;
    clone->patch_count = 0;
    clone->patch_capacity = 0;
    clone->patches = NULL;

    return clone;
}

void compiler_prepare_specialized_variants(struct CompilerContext* ctx) {
    if (!ctx || ctx->function_count == 0 || !ctx->function_chunks || !ctx->function_hot_counts) {
        return;
    }

    for (int i = 0; i < ctx->function_count; ++i) {
        uint64_t hits = ctx->function_hot_counts ? ctx->function_hot_counts[i] : 0;
        if (hits < FUNCTION_SPECIALIZATION_THRESHOLD) {
            if (ctx->function_specialized_chunks) {
                if (ctx->function_specialized_chunks[i]) {
                    free_bytecode_buffer(ctx->function_specialized_chunks[i]);
                    ctx->function_specialized_chunks[i] = NULL;
                }
            }
            if (ctx->function_deopt_stubs) {
                if (ctx->function_deopt_stubs[i]) {
                    free_bytecode_buffer(ctx->function_deopt_stubs[i]);
                    ctx->function_deopt_stubs[i] = NULL;
                }
            }
            continue;
        }

        BytecodeBuffer* baseline = ctx->function_chunks[i];
        if (!baseline) {
            continue;
        }

        BytecodeBuffer* specialized = clone_bytecode_buffer(baseline);
        if (!specialized) {
            continue;
        }

        BytecodeBuffer* stub = init_bytecode_buffer();
        if (!stub) {
            free_bytecode_buffer(specialized);
            continue;
        }

        // Encode the function arity as metadata so the runtime stub can
        // invalidate parameter registers before handing control back to the
        // baseline chunk on deoptimization.
        emit_byte_to_buffer(stub, (uint8_t)ctx->function_arities[i]);

        if (ctx->function_specialized_chunks) {
            if (ctx->function_specialized_chunks[i]) {
                free_bytecode_buffer(ctx->function_specialized_chunks[i]);
            }
            ctx->function_specialized_chunks[i] = specialized;
        }

        if (ctx->function_deopt_stubs) {
            if (ctx->function_deopt_stubs[i]) {
                free_bytecode_buffer(ctx->function_deopt_stubs[i]);
            }
            ctx->function_deopt_stubs[i] = stub;
        } else {
            free_bytecode_buffer(stub);
        }
    }
}
