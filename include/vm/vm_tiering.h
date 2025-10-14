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

typedef struct {
    const uint8_t* start_ip;
    uint8_t length;
    uint8_t opcodes[VM_MAX_FUSION_WINDOW];
} VMHotWindowDescriptor;

void vm_tiering_request_window_fusion(const VMHotWindowDescriptor* window);
bool vm_tiering_try_execute_fused(const uint8_t* start_ip, uint8_t opcode);
void vm_tiering_instruction_tick(uint64_t instruction_index);
void vm_tiering_invalidate_all_fusions(void);

#endif // ORUS_VM_TIERING_H
