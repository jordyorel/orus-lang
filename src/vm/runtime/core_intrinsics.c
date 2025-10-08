// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/core_intrinsics.c
// Author: Orus Contributors
// License: MIT License (see LICENSE file in the project root)
// Description: Core intrinsic bindings exposed to the runtime (math functions, etc.).

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vm/vm.h"
#include "runtime/core_bytes.h"

static const IntrinsicSignatureInfo intrinsic_signature_table[] = {
    {"__c_sin", 1, {TYPE_F64}, TYPE_F64},
    {"__c_cos", 1, {TYPE_F64}, TYPE_F64},
    {"__c_pow", 2, {TYPE_F64, TYPE_F64}, TYPE_F64},
    {"__c_sqrt", 1, {TYPE_F64}, TYPE_F64},
    {"__bytes_alloc", 1, {TYPE_I64}, TYPE_BYTES},
    {"__bytes_alloc_fill", 2, {TYPE_I64, TYPE_I64}, TYPE_BYTES},
    {"__bytes_slice", 3, {TYPE_BYTES, TYPE_I64, TYPE_I64}, TYPE_BYTES},
    {"__bytes_from_string", 1, {TYPE_STRING}, TYPE_BYTES},
    {"__bytes_to_string", 1, {TYPE_BYTES}, TYPE_STRING},
};

static const size_t intrinsic_signature_table_count =
    sizeof(intrinsic_signature_table) / sizeof(intrinsic_signature_table[0]);

const IntrinsicSignatureInfo* vm_get_intrinsic_signature(const char* symbol) {
    if (!symbol) {
        return NULL;
    }

    for (size_t i = 0; i < intrinsic_signature_table_count; i++) {
        const IntrinsicSignatureInfo* entry = &intrinsic_signature_table[i];
        if (entry->symbol && strcmp(entry->symbol, symbol) == 0) {
            return entry;
        }
    }

    return NULL;
}

static Value intrinsic_native_sin(int argCount, Value* args) {
    (void)argCount;
    if (!args) {
        return F64_VAL(0.0);
    }
    double operand = IS_F64(args[0]) ? AS_F64(args[0]) : 0.0;
    return F64_VAL(sin(operand));
}

static Value intrinsic_native_cos(int argCount, Value* args) {
    (void)argCount;
    if (!args) {
        return F64_VAL(0.0);
    }
    double operand = IS_F64(args[0]) ? AS_F64(args[0]) : 0.0;
    return F64_VAL(cos(operand));
}

static Value intrinsic_native_pow(int argCount, Value* args) {
    (void)argCount;
    if (!args) {
        return F64_VAL(1.0);
    }
    double base = IS_F64(args[0]) ? AS_F64(args[0]) : 0.0;
    double exp = (argCount > 1 && IS_F64(args[1])) ? AS_F64(args[1]) : 0.0;
    return F64_VAL(pow(base, exp));
}

static Value intrinsic_native_sqrt(int argCount, Value* args) {
    (void)argCount;
    if (!args) {
        return F64_VAL(0.0);
    }
    double operand = IS_F64(args[0]) ? AS_F64(args[0]) : 0.0;
    return F64_VAL(sqrt(operand));
}

typedef struct {
    const char* symbol;
    NativeFn function;
} IntrinsicBinding;

static const IntrinsicBinding core_intrinsic_bindings[] = {
    {"__c_sin", intrinsic_native_sin},
    {"__c_cos", intrinsic_native_cos},
    {"__c_pow", intrinsic_native_pow},
    {"__c_sqrt", intrinsic_native_sqrt},
    {"__bytes_alloc", vm_core_bytes_alloc},
    {"__bytes_alloc_fill", vm_core_bytes_alloc_fill},
    {"__bytes_slice", vm_core_bytes_slice},
    {"__bytes_from_string", vm_core_bytes_from_string},
    {"__bytes_to_string", vm_core_bytes_to_string},
};

NativeFn vm_lookup_core_intrinsic(const char* symbol) {
    if (!symbol) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(core_intrinsic_bindings) / sizeof(core_intrinsic_bindings[0]); ++i) {
        if (strcmp(core_intrinsic_bindings[i].symbol, symbol) == 0) {
            return core_intrinsic_bindings[i].function;
        }
    }

    return NULL;
}
