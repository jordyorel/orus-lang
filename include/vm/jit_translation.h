// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_translation.h
// Description: Shared definitions for the baseline JIT bytecode translator.

#ifndef ORUS_VM_JIT_TRANSLATION_H
#define ORUS_VM_JIT_TRANSLATION_H

#include <stdbool.h>
#include <stdint.h>

#include "vm/jit_ir.h"
#include "vm/vm.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrusJitTranslationResult {
    OrusJitTranslationStatus status;
    OrusJitIROpcode opcode;
    OrusJitValueKind value_kind;
    uint32_t bytecode_offset;
} OrusJitTranslationResult;

OrusJitTranslationResult orus_jit_translate_linear_block(
    VMState* vm_state,
    Function* function,
    const struct Chunk* chunk,
    const HotPathSample* sample,
    OrusJitIRProgram* program);

const char* orus_jit_translation_status_name(OrusJitTranslationStatus status);

bool orus_jit_translation_status_is_unsupported(
    OrusJitTranslationStatus status);

const char* orus_jit_value_kind_name(OrusJitValueKind kind);

const char* orus_jit_translation_failure_category_name(
    OrusJitTranslationFailureCategory category);

const char* orus_jit_rollout_stage_name(OrusJitRolloutStage stage);

bool orus_jit_rollout_stage_parse(const char* text,
                                  OrusJitRolloutStage* out_stage);

void orus_jit_rollout_set_stage(VMState* vm_state, OrusJitRolloutStage stage);

bool orus_jit_rollout_is_kind_enabled(const VMState* vm_state,
                                      OrusJitValueKind kind);

void orus_jit_translation_failure_log_init(
    OrusJitTranslationFailureLog* log);

void orus_jit_translation_failure_log_record(
    OrusJitTranslationFailureLog* log,
    const OrusJitTranslationFailureRecord* record);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ORUS_VM_JIT_TRANSLATION_H

