// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_layout.h
// Description: Shared layout metadata for VM structs consumed by the JIT
//              backend. This header encodes offsets and invariants so native
//              code can assume the same frame/register layout as the
//              interpreter.

#ifndef ORUS_VM_JIT_LAYOUT_H
#define ORUS_VM_JIT_LAYOUT_H

#include <stddef.h>

#include "vm/vm.h"

// --- VM level layout -------------------------------------------------------
#define ORUS_JIT_OFFSET_VM_REGISTER_FILE (offsetof(VM, register_file))
#define ORUS_JIT_OFFSET_VM_REGISTERS     (offsetof(VM, registers))
#define ORUS_JIT_OFFSET_VM_TYPED_REGS    (offsetof(VM, typed_regs))
#define ORUS_JIT_OFFSET_VM_FRAMES        (offsetof(VM, frames))

// --- Register file layout --------------------------------------------------
#define ORUS_JIT_OFFSET_RF_GLOBALS        (offsetof(RegisterFile, globals))
#define ORUS_JIT_OFFSET_RF_ROOT_TEMPS     (offsetof(RegisterFile, temps_root))
#define ORUS_JIT_OFFSET_RF_ACTIVE_TEMPS   (offsetof(RegisterFile, temps))
#define ORUS_JIT_OFFSET_RF_CURRENT_FRAME  (offsetof(RegisterFile, current_frame))
#define ORUS_JIT_OFFSET_RF_FRAME_STACK    (offsetof(RegisterFile, frame_stack))

// --- Call frame layout -----------------------------------------------------
#define ORUS_JIT_OFFSET_FRAME_REGISTERS       (offsetof(CallFrame, registers))
#define ORUS_JIT_OFFSET_FRAME_TEMPS           (offsetof(CallFrame, temps))
#define ORUS_JIT_OFFSET_FRAME_TYPED_WINDOW    (offsetof(CallFrame, typed_window))
#define ORUS_JIT_OFFSET_FRAME_PREV_TYPED      (offsetof(CallFrame, previous_typed_window))
#define ORUS_JIT_OFFSET_FRAME_PARENT          (offsetof(CallFrame, parent))
#define ORUS_JIT_OFFSET_FRAME_NEXT            (offsetof(CallFrame, next))
#define ORUS_JIT_OFFSET_FRAME_FRAME_BASE      (offsetof(CallFrame, frame_base))
#define ORUS_JIT_OFFSET_FRAME_TEMP_BASE       (offsetof(CallFrame, temp_base))
#define ORUS_JIT_OFFSET_FRAME_RESULT_REG      (offsetof(CallFrame, resultRegister))
#define ORUS_JIT_OFFSET_FRAME_PARAM_BASE      (offsetof(CallFrame, parameterBaseRegister))

#define ORUS_JIT_SIZEOF_VALUE        (sizeof(Value))
#define ORUS_JIT_SIZEOF_CALLFRAME    (sizeof(CallFrame))
#define ORUS_JIT_SIZEOF_REGISTERFILE (sizeof(RegisterFile))

// Static layout invariants --------------------------------------------------
_Static_assert(ORUS_JIT_OFFSET_VM_REGISTER_FILE == 0,
               "VM.register_file must stay the first field for JIT access");
_Static_assert(ORUS_JIT_OFFSET_RF_GLOBALS == 0,
               "RegisterFile.globals must be at offset 0");
_Static_assert(ORUS_JIT_OFFSET_FRAME_REGISTERS == 0,
               "CallFrame.registers must be at offset 0");
_Static_assert(ORUS_JIT_OFFSET_FRAME_TEMPS ==
                   (FRAME_REGISTERS * sizeof(Value)),
               "Frame temps must follow the register window contiguously");
_Static_assert(sizeof(((CallFrame*)0)->registers) / sizeof(Value) ==
                   FRAME_REGISTERS,
               "CallFrame.registers size mismatch with FRAME_REGISTERS");
_Static_assert(sizeof(((CallFrame*)0)->temps) / sizeof(Value) ==
                   TEMP_REGISTERS,
               "CallFrame.temps size mismatch with TEMP_REGISTERS");

#endif // ORUS_VM_JIT_LAYOUT_H
