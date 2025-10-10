// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_ir.h
// Description: Minimal OrusJit intermediate representation describing
//              architecture-neutral operations for DynASM-backed codegen.

#ifndef ORUS_VM_JIT_IR_H
#define ORUS_VM_JIT_IR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OrusJitIROpcode {
    ORUS_JIT_IR_OP_RETURN = 0,
} OrusJitIROpcode;

typedef struct OrusJitIRInstruction {
    OrusJitIROpcode opcode;
} OrusJitIRInstruction;

typedef struct OrusJitIRProgram {
    const OrusJitIRInstruction* instructions;
    size_t count;
} OrusJitIRProgram;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ORUS_VM_JIT_IR_H
