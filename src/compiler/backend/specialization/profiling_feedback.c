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

typedef enum {
    GUARD_KIND_NONE = 0,
    GUARD_KIND_I32,
    GUARD_KIND_I64,
    GUARD_KIND_F64,
} GuardKind;

typedef struct {
    int offset;
    uint8_t new_opcode;
    uint8_t operand_count;
    uint8_t operands[3];
    GuardKind guard_kind;
} InstructionTransform;

typedef struct {
    InstructionTransform* data;
    size_t count;
    size_t capacity;
} InstructionPlan;

typedef struct {
    uint8_t reg;
    GuardKind kind;
} GuardRequirement;

typedef struct {
    GuardRequirement* data;
    size_t count;
    size_t capacity;
} GuardPlan;

static void plan_reserve_transforms(InstructionPlan* plan, size_t additional) {
    if (!plan) {
        return;
    }
    size_t required = plan->count + additional;
    if (required <= plan->capacity) {
        return;
    }
    size_t new_capacity = plan->capacity ? plan->capacity : 8;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    InstructionTransform* resized = realloc(plan->data, new_capacity * sizeof(InstructionTransform));
    if (!resized) {
        return;
    }
    plan->data = resized;
    plan->capacity = new_capacity;
}

static void guard_plan_reserve(GuardPlan* plan, size_t additional) {
    if (!plan) {
        return;
    }
    size_t required = plan->count + additional;
    if (required <= plan->capacity) {
        return;
    }
    size_t new_capacity = plan->capacity ? plan->capacity : 4;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    GuardRequirement* resized = realloc(plan->data, new_capacity * sizeof(GuardRequirement));
    if (!resized) {
        return;
    }
    plan->data = resized;
    plan->capacity = new_capacity;
}

static bool guard_plan_add(GuardPlan* plan, uint8_t reg, GuardKind kind) {
    if (!plan || kind == GUARD_KIND_NONE) {
        return false;
    }

    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->data[i].reg == reg) {
            // Once a register is guarded we do not downgrade its guard kind.
            return true;
        }
    }

    guard_plan_reserve(plan, 1);
    if (plan->capacity == 0) {
        return false;
    }

    plan->data[plan->count++] = (GuardRequirement){.reg = reg, .kind = kind};
    return true;
}

static uint8_t guard_opcode_for_kind(GuardKind kind) {
    switch (kind) {
        case GUARD_KIND_I32:
            return OP_MOVE_I32;
        case GUARD_KIND_I64:
            return OP_MOVE_I64;
        case GUARD_KIND_F64:
            return OP_MOVE_F64;
        default:
            return OP_HALT;
    }
}

static GuardKind guard_kind_for_opcode(uint8_t opcode) {
    switch (opcode) {
        case OP_ADD_I32_R:
        case OP_SUB_I32_R:
        case OP_MUL_I32_R:
        case OP_DIV_I32_R:
        case OP_MOD_I32_R:
        case OP_LT_I32_R:
        case OP_LE_I32_R:
        case OP_GT_I32_R:
        case OP_GE_I32_R:
            return GUARD_KIND_I32;
        case OP_ADD_I64_R:
        case OP_SUB_I64_R:
        case OP_MUL_I64_R:
        case OP_DIV_I64_R:
        case OP_MOD_I64_R:
        case OP_LT_I64_R:
        case OP_LE_I64_R:
        case OP_GT_I64_R:
        case OP_GE_I64_R:
            return GUARD_KIND_I64;
        case OP_ADD_F64_R:
        case OP_SUB_F64_R:
        case OP_MUL_F64_R:
        case OP_DIV_F64_R:
        case OP_MOD_F64_R:
        case OP_LT_F64_R:
        case OP_LE_F64_R:
        case OP_GT_F64_R:
        case OP_GE_F64_R:
            return GUARD_KIND_F64;
        default:
            return GUARD_KIND_NONE;
    }
}

static bool map_typed_opcode(uint8_t opcode, uint8_t* typed_opcode) {
    if (!typed_opcode) {
        return false;
    }

    switch (opcode) {
        case OP_ADD_I32_R: *typed_opcode = OP_ADD_I32_TYPED; return true;
        case OP_SUB_I32_R: *typed_opcode = OP_SUB_I32_TYPED; return true;
        case OP_MUL_I32_R: *typed_opcode = OP_MUL_I32_TYPED; return true;
        case OP_DIV_I32_R: *typed_opcode = OP_DIV_I32_TYPED; return true;
        case OP_MOD_I32_R: *typed_opcode = OP_MOD_I32_TYPED; return true;
        case OP_LT_I32_R: *typed_opcode = OP_LT_I32_TYPED; return true;
        case OP_LE_I32_R: *typed_opcode = OP_LE_I32_TYPED; return true;
        case OP_GT_I32_R: *typed_opcode = OP_GT_I32_TYPED; return true;
        case OP_GE_I32_R: *typed_opcode = OP_GE_I32_TYPED; return true;
        case OP_ADD_I64_R: *typed_opcode = OP_ADD_I64_TYPED; return true;
        case OP_SUB_I64_R: *typed_opcode = OP_SUB_I64_TYPED; return true;
        case OP_MUL_I64_R: *typed_opcode = OP_MUL_I64_TYPED; return true;
        case OP_DIV_I64_R: *typed_opcode = OP_DIV_I64_TYPED; return true;
        case OP_MOD_I64_R: *typed_opcode = OP_MOD_I64_TYPED; return true;
        case OP_LT_I64_R: *typed_opcode = OP_LT_I64_TYPED; return true;
        case OP_LE_I64_R: *typed_opcode = OP_LE_I64_TYPED; return true;
        case OP_GT_I64_R: *typed_opcode = OP_GT_I64_TYPED; return true;
        case OP_GE_I64_R: *typed_opcode = OP_GE_I64_TYPED; return true;
        case OP_ADD_F64_R: *typed_opcode = OP_ADD_F64_TYPED; return true;
        case OP_SUB_F64_R: *typed_opcode = OP_SUB_F64_TYPED; return true;
        case OP_MUL_F64_R: *typed_opcode = OP_MUL_F64_TYPED; return true;
        case OP_DIV_F64_R: *typed_opcode = OP_DIV_F64_TYPED; return true;
        case OP_MOD_F64_R: *typed_opcode = OP_MOD_F64_TYPED; return true;
        case OP_LT_F64_R: *typed_opcode = OP_LT_F64_TYPED; return true;
        case OP_LE_F64_R: *typed_opcode = OP_LE_F64_TYPED; return true;
        case OP_GT_F64_R: *typed_opcode = OP_GT_F64_TYPED; return true;
        case OP_GE_F64_R: *typed_opcode = OP_GE_F64_TYPED; return true;
        default:
            return false;
    }
}

static int bytecode_instruction_width(uint8_t opcode) {
    switch (opcode) {
        case OP_HALT:
        case OP_RETURN_VOID:
            return 1;
        case OP_LOAD_TRUE:
        case OP_LOAD_FALSE:
        case OP_INC_I32_R:
        case OP_INC_I32_CHECKED:
        case OP_INC_I64_R:
        case OP_INC_I64_CHECKED:
        case OP_INC_U32_R:
        case OP_INC_U32_CHECKED:
        case OP_INC_U64_R:
        case OP_INC_U64_CHECKED:
        case OP_DEC_I32_R:
        case OP_TRY_BEGIN:
        case OP_TRY_END:
        case OP_RETURN_R:
            return 2;
        case OP_MOVE:
        case OP_MOVE_I32:
        case OP_MOVE_I64:
        case OP_MOVE_F64:
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL:
        case OP_JUMP_IF_R:
        case OP_JUMP_IF_NOT_R:
        case OP_GET_ITER_R:
        case OP_ITER_NEXT_R:
        case OP_ARRAY_LEN_R:
        case OP_PRINT_R:
        case OP_ASSERT_EQ_R:
            return 3;
        case OP_LOAD_CONST:
        case OP_JUMP:
        case OP_LOOP:
        case OP_JUMP_IF_NOT_I32_TYPED:
        case OP_CALL_R:
        case OP_CALL_NATIVE_R:
        case OP_CALL_FOREIGN:
        case OP_TAIL_CALL_R:
            return 4;
        default:
            return 1;
    }
}

static void apply_transformation_plan(BytecodeBuffer* specialized,
                                      const InstructionPlan* plan,
                                      const GuardPlan* guards) {
    if (!specialized || !plan) {
        return;
    }

    for (size_t i = 0; i < plan->count; ++i) {
        const InstructionTransform* transform = &plan->data[i];
        if (transform->offset < 0 || transform->offset >= specialized->count) {
            continue;
        }
        specialized->instructions[transform->offset] = transform->new_opcode;
    }

    if (!guards || guards->count == 0) {
        return;
    }

    BytecodeBuffer* prologue = init_bytecode_buffer();
    if (!prologue) {
        return;
    }

    for (size_t i = 0; i < guards->count; ++i) {
        uint8_t opcode = guard_opcode_for_kind(guards->data[i].kind);
        if (opcode == OP_HALT) {
            continue;
        }
        emit_byte_to_buffer(prologue, opcode);
        emit_byte_to_buffer(prologue, guards->data[i].reg);
        emit_byte_to_buffer(prologue, guards->data[i].reg);
    }

    if (prologue->count == 0) {
        free_bytecode_buffer(prologue);
        return;
    }

    int new_count = prologue->count + specialized->count;
    uint8_t* merged = malloc((size_t)new_count);
    if (!merged) {
        free_bytecode_buffer(prologue);
        return;
    }

    memcpy(merged, prologue->instructions, (size_t)prologue->count);
    memcpy(merged + prologue->count, specialized->instructions, (size_t)specialized->count);

    free(specialized->instructions);
    specialized->instructions = merged;
    specialized->capacity = new_count;

    int* merged_lines = NULL;
    int* merged_columns = NULL;
    const char** merged_files = NULL;

    if (specialized->source_lines || prologue->count > 0) {
        merged_lines = malloc(sizeof(int) * (size_t)new_count);
        merged_columns = malloc(sizeof(int) * (size_t)new_count);
        merged_files = malloc(sizeof(const char*) * (size_t)new_count);
        if (merged_lines && merged_columns && merged_files) {
            for (int i = 0; i < prologue->count; ++i) {
                merged_lines[i] = -1;
                merged_columns[i] = -1;
                merged_files[i] = NULL;
            }
            if (specialized->source_lines) {
                memcpy(&merged_lines[prologue->count], specialized->source_lines,
                       sizeof(int) * (size_t)specialized->count);
            } else {
                for (int i = prologue->count; i < new_count; ++i) {
                    merged_lines[i] = -1;
                }
            }

            if (specialized->source_columns) {
                memcpy(&merged_columns[prologue->count], specialized->source_columns,
                       sizeof(int) * (size_t)specialized->count);
            } else {
                for (int i = prologue->count; i < new_count; ++i) {
                    merged_columns[i] = -1;
                }
            }

            if (specialized->source_files) {
                memcpy(&merged_files[prologue->count], specialized->source_files,
                       sizeof(const char*) * (size_t)specialized->count);
            } else {
                for (int i = prologue->count; i < new_count; ++i) {
                    merged_files[i] = NULL;
                }
            }
        }

        free(specialized->source_lines);
        free(specialized->source_columns);
        free(specialized->source_files);

        specialized->source_lines = merged_lines;
        specialized->source_columns = merged_columns;
        specialized->source_files = merged_files;
    }

    specialized->count = new_count;
    free_bytecode_buffer(prologue);
}

static void collect_transforms(const BytecodeBuffer* baseline,
                               InstructionPlan* plan,
                               GuardPlan* guards) {
    if (!baseline || !plan) {
        return;
    }

    int offset = 0;
    while (offset < baseline->count) {
        uint8_t opcode = baseline->instructions[offset];
        uint8_t typed_opcode = 0;
        GuardKind guard_kind = guard_kind_for_opcode(opcode);
        bool can_specialize = map_typed_opcode(opcode, &typed_opcode);

        if (can_specialize && offset + 3 < baseline->count) {
            plan_reserve_transforms(plan, 1);
            if (plan->capacity == 0) {
                return;
            }

            InstructionTransform* transform = &plan->data[plan->count++];
            transform->offset = offset;
            transform->new_opcode = typed_opcode;
            transform->operand_count = 3;
            transform->guard_kind = guard_kind;
            transform->operands[0] = baseline->instructions[offset + 1];
            transform->operands[1] = baseline->instructions[offset + 2];
            transform->operands[2] = baseline->instructions[offset + 3];

            if (guards && guard_kind != GUARD_KIND_NONE) {
                guard_plan_add(guards, transform->operands[1], guard_kind);
                guard_plan_add(guards, transform->operands[2], guard_kind);
            }

            offset += 4;
            continue;
        }

        int width = bytecode_instruction_width(opcode);
        if (width <= 0) {
            width = 1;
        }
        offset += width;
    }
}

static void free_instruction_plan(InstructionPlan* plan) {
    if (!plan) {
        return;
    }
    free(plan->data);
    plan->data = NULL;
    plan->count = 0;
    plan->capacity = 0;
}

static void free_guard_plan(GuardPlan* plan) {
    if (!plan) {
        return;
    }
    free(plan->data);
    plan->data = NULL;
    plan->count = 0;
    plan->capacity = 0;
}

void compiler_prepare_specialized_variants(struct CompilerContext* ctx) {
    if (!ctx || ctx->function_count == 0 || !ctx->function_chunks || !ctx->function_hot_counts) {
        return;
    }

    for (int i = 0; i < ctx->function_count; ++i) {
        uint64_t hits = ctx->function_hot_counts ? ctx->function_hot_counts[i] : 0;
        const FunctionSpecializationHint* hint = NULL;
        if (ctx->profiling_feedback && ctx->function_names && ctx->function_names[i]) {
            hint = compiler_find_specialization_hint(ctx->profiling_feedback,
                                                     ctx->function_names[i]);
        }

        if (!hint || !hint->eligible || hits < FUNCTION_SPECIALIZATION_THRESHOLD) {
            if (ctx->function_specialized_chunks && ctx->function_specialized_chunks[i]) {
                free_bytecode_buffer(ctx->function_specialized_chunks[i]);
                ctx->function_specialized_chunks[i] = NULL;
            }
            if (ctx->function_deopt_stubs && ctx->function_deopt_stubs[i]) {
                free_bytecode_buffer(ctx->function_deopt_stubs[i]);
                ctx->function_deopt_stubs[i] = NULL;
            }
            continue;
        }

        BytecodeBuffer* baseline = ctx->function_chunks[i];
        if (!baseline) {
            continue;
        }

        InstructionPlan transform_plan = {0};
        GuardPlan guard_plan = {0};
        collect_transforms(baseline, &transform_plan, &guard_plan);

        if (transform_plan.count == 0) {
            free_instruction_plan(&transform_plan);
            free_guard_plan(&guard_plan);
            continue;
        }

        BytecodeBuffer* specialized = clone_bytecode_buffer(baseline);
        if (!specialized) {
            free_instruction_plan(&transform_plan);
            free_guard_plan(&guard_plan);
            continue;
        }

        apply_transformation_plan(specialized, &transform_plan, &guard_plan);
        free_instruction_plan(&transform_plan);
        free_guard_plan(&guard_plan);

        BytecodeBuffer* stub = init_bytecode_buffer();
        if (!stub) {
            free_bytecode_buffer(specialized);
            continue;
        }

        emit_byte_to_buffer(stub, (uint8_t)ctx->function_arities[i]);

        if (ctx->function_specialized_chunks) {
            if (ctx->function_specialized_chunks[i]) {
                free_bytecode_buffer(ctx->function_specialized_chunks[i]);
            }
            ctx->function_specialized_chunks[i] = specialized;
        } else {
            free_bytecode_buffer(specialized);
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
