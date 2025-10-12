// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_ir_debug.h
// Description: Debugging helpers for inspecting Orus JIT IR programs.

#ifndef ORUS_VM_JIT_IR_DEBUG_H
#define ORUS_VM_JIT_IR_DEBUG_H

#include <stddef.h>
#include <stdio.h>

#include "vm/jit_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* orus_jit_ir_opcode_name(OrusJitIROpcode opcode);

const char* orus_jit_ir_loop_compare_name(OrusJitIRLoopCompareKind kind);

size_t orus_jit_ir_format_instruction(const OrusJitIRInstruction* inst,
                                      char* buffer,
                                      size_t buffer_size);

void orus_jit_ir_dump_program(const OrusJitIRProgram* program, FILE* file);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ORUS_VM_JIT_IR_DEBUG_H
