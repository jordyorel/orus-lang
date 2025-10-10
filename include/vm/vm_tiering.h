// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/vm_tiering.h
// Description: Tiering helpers that mediate specialized function dispatch and
//              deoptimization control flow inside the VM.

#ifndef ORUS_VM_TIERING_H
#define ORUS_VM_TIERING_H

#include "vm/vm.h"

Chunk* vm_select_function_chunk(Function* function);
void vm_default_deopt_stub(Function* function);
void vm_handle_type_error_deopt(void);
JITEntry* vm_jit_lookup_entry(FunctionId function, LoopId loop);
uint64_t vm_jit_install_entry(FunctionId function, LoopId loop, JITEntry* entry);
void vm_jit_invalidate_entry(const JITDeoptTrigger* trigger);
void vm_jit_flush_entries(void);

#endif // ORUS_VM_TIERING_H
