// Orus Language Project

#ifndef ORUS_COMPILER_SPECIALIZATION_FEEDBACK_H
#define ORUS_COMPILER_SPECIALIZATION_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct CompilerContext;

// Threshold for promoting a function to specialized tier by default.
#define FUNCTION_SPECIALIZATION_THRESHOLD 512ULL

typedef struct FunctionSpecializationHint {
    char* name;              // Debug name of the function (heap-allocated)
    uint64_t hit_count;      // Sampled invocation count from profiling
    int function_index;      // VM function table index associated with the hit data
    int arity;               // Function arity captured from the VM metadata
    bool eligible;           // Whether the function crosses the specialization threshold
} FunctionSpecializationHint;

typedef struct CompilerProfilingFeedback {
    FunctionSpecializationHint* functions;
    size_t function_count;
} CompilerProfilingFeedback;

void compiler_refresh_feedback(struct CompilerContext* ctx);
void compiler_prepare_specialized_variants(struct CompilerContext* ctx);
void compiler_free_profiling_feedback(CompilerProfilingFeedback* feedback);
const FunctionSpecializationHint*
compiler_find_specialization_hint(const CompilerProfilingFeedback* feedback,
                                  const char* name);

#endif // ORUS_COMPILER_SPECIALIZATION_FEEDBACK_H
