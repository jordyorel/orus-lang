// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/runtime/core_bytes.h
// Author: Orus Contributors
// Description: Helper utilities and intrinsic entry points for the runtime
//              byte buffer type exposed to Orus programs.

#ifndef ORUS_RUNTIME_CORE_BYTES_H
#define ORUS_RUNTIME_CORE_BYTES_H

#include <stddef.h>
#include <stdint.h>

#include "vm/vm.h"

ObjByteBuffer* vm_bytes_from_string_object(ObjString* string);
ObjString* vm_bytes_to_string_object(const ObjByteBuffer* buffer);

Value vm_core_bytes_alloc(int argCount, Value* args);
Value vm_core_bytes_alloc_fill(int argCount, Value* args);
Value vm_core_bytes_slice(int argCount, Value* args);
Value vm_core_bytes_from_string(int argCount, Value* args);
Value vm_core_bytes_to_string(int argCount, Value* args);

#endif  // ORUS_RUNTIME_CORE_BYTES_H
