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

#endif // ORUS_VM_TIERING_H
