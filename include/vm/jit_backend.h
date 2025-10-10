// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_backend.h
// Description: DynASM-backed JIT integration layer exposing a minimal
//              interface for native tier execution.

#ifndef ORUS_VM_JIT_BACKEND_H
#define ORUS_VM_JIT_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm/jit_ir.h"

struct VM;
struct OrusJitBackend;

typedef struct JITDeoptTrigger {
    uint16_t function_index;
    uint16_t loop_index;
    uint64_t generation;
} JITDeoptTrigger;

typedef void (*JITEntryPoint)(struct VM* vm);

typedef enum {
    JIT_BACKEND_OK = 0,
    JIT_BACKEND_UNSUPPORTED,
    JIT_BACKEND_OUT_OF_MEMORY,
    JIT_BACKEND_ASSEMBLY_ERROR
} JITBackendStatus;

typedef struct JITEntry {
    JITEntryPoint entry_point;
    void* code_ptr;
    size_t code_size;
    size_t code_capacity;
    const char* debug_name;
} JITEntry;

typedef struct {
    void (*enter)(struct VM* vm, const JITEntry* entry);
    void (*invalidate)(struct VM* vm, const JITDeoptTrigger* trigger);
    void (*flush)(struct VM* vm);
} JITBackendVTable;

struct OrusJitBackend* orus_jit_backend_create(void);
void orus_jit_backend_destroy(struct OrusJitBackend* backend);

bool orus_jit_backend_is_available(void);

JITBackendStatus orus_jit_backend_compile_noop(struct OrusJitBackend* backend,
                                               JITEntry* out_entry);
JITBackendStatus orus_jit_backend_compile_ir(struct OrusJitBackend* backend,
                                             const OrusJitIRProgram* program,
                                             JITEntry* out_entry);
void orus_jit_backend_release_entry(struct OrusJitBackend* backend,
                                    JITEntry* entry);

const JITBackendVTable* orus_jit_backend_vtable(void);

#endif // ORUS_VM_JIT_BACKEND_H
