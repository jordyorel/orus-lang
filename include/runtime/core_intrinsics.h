// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/runtime/core_intrinsics.h
// Author: Orus Contributors
// Description: Shared declarations for core intrinsic signature and binding
// tables across runtime compilation units.

#ifndef ORUS_RUNTIME_CORE_INTRINSICS_H
#define ORUS_RUNTIME_CORE_INTRINSICS_H

#include <stddef.h>

#include "vm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* symbol;
    NativeFn function;
} IntrinsicBinding;

extern const IntrinsicSignatureInfo core_math_intrinsic_signature_table[];
extern const size_t core_math_intrinsic_signature_table_count;
extern const IntrinsicBinding core_math_intrinsic_bindings[];
extern const size_t core_math_intrinsic_bindings_count;

extern const IntrinsicSignatureInfo core_fs_intrinsic_signature_table[];
extern const size_t core_fs_intrinsic_signature_table_count;
extern const IntrinsicBinding core_fs_intrinsic_bindings[];
extern const size_t core_fs_intrinsic_bindings_count;

#ifdef __cplusplus
}
#endif

#endif /* ORUS_RUNTIME_CORE_INTRINSICS_H */
