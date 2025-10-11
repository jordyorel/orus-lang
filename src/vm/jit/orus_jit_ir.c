// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/jit/orus_jit_ir.c
// Description: Utility helpers for constructing Orus JIT IR programs.

#include "vm/jit_ir.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void
orus_jit_ir_program_init(OrusJitIRProgram* program) {
    if (!program) {
        return;
    }
    memset(program, 0, sizeof(*program));
}

void
orus_jit_ir_program_reset(OrusJitIRProgram* program) {
    if (!program) {
        return;
    }
    free(program->instructions);
    program->instructions = NULL;
    program->count = 0;
    program->capacity = 0;
    program->source_chunk = NULL;
    program->function_index = 0;
    program->loop_index = 0;
    program->loop_start_offset = 0;
    program->loop_end_offset = 0;
}

bool
orus_jit_ir_program_reserve(OrusJitIRProgram* program, size_t additional) {
    if (!program) {
        return false;
    }
    if (additional == 0) {
        return true;
    }
    if (program->count > SIZE_MAX - additional) {
        return false;
    }
    size_t required = program->count + additional;
    if (required <= program->capacity) {
        return true;
    }
    size_t new_capacity = program->capacity ? program->capacity : 8u;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }
    OrusJitIRInstruction* instructions =
        (OrusJitIRInstruction*)realloc(program->instructions,
                                       new_capacity * sizeof(OrusJitIRInstruction));
    if (!instructions) {
        return false;
    }
    program->instructions = instructions;
    program->capacity = new_capacity;
    return true;
}

OrusJitIRInstruction*
orus_jit_ir_program_append(OrusJitIRProgram* program) {
    if (!program) {
        return NULL;
    }
    if (!orus_jit_ir_program_reserve(program, 1u)) {
        return NULL;
    }
    OrusJitIRInstruction* inst = &program->instructions[program->count++];
    memset(inst, 0, sizeof(*inst));
    return inst;
}

