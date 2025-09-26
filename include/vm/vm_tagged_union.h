/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/vm_tagged_union.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares the tagged union representation for dynamically typed values.
 */

#ifndef ORUS_VM_TAGGED_UNION_H
#define ORUS_VM_TAGGED_UNION_H

#include "vm/vm.h"
#include <stdbool.h>

typedef struct {
    const char* type_name;
    const char* variant_name;
    int variant_index;
    const Value* payload;
    int payload_count;
} TaggedUnionSpec;

bool vm_make_tagged_union(const TaggedUnionSpec* spec, Value* out_value);
bool vm_result_ok(Value inner, Value* out_value);
bool vm_result_err(Value error_value, Value* out_value);

#endif // ORUS_VM_TAGGED_UNION_H
