// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/scope_stack.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Defines the compiler scope stack for tracking lexical scopes and symbol visibility.


#ifndef ORUS_COMPILER_SCOPE_STACK_H
#define ORUS_COMPILER_SCOPE_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct SymbolTable;

typedef enum {
    SCOPE_KIND_LEXICAL,
    SCOPE_KIND_LOOP
} ScopeKind;

typedef struct ScopeFrame {
    ScopeKind kind;
    struct SymbolTable* symbols;
    int lexical_depth;
    int start_offset;
    int end_offset;
    int continue_offset;

    uint16_t loop_id;
    uint16_t prev_loop_id;

    int prev_loop_start;
    int prev_loop_end;
    int prev_loop_continue;

    int* saved_break_statements;
    int saved_break_count;
    int saved_break_capacity;

    int* saved_continue_statements;
    int saved_continue_count;
    int saved_continue_capacity;

    int* loop_break_statements;
    int loop_break_count;
    int loop_break_capacity;

    int* loop_continue_statements;
    int loop_continue_count;
    int loop_continue_capacity;

    const char* label;
} ScopeFrame;

typedef struct ScopeStack {
    ScopeFrame* frames;
    int count;
    int capacity;
    int loop_depth;
} ScopeStack;

ScopeStack* scope_stack_create(void);
void scope_stack_destroy(ScopeStack* stack);
ScopeFrame* scope_stack_push(ScopeStack* stack, ScopeKind kind);
void scope_stack_pop(ScopeStack* stack);
ScopeFrame* scope_stack_current(ScopeStack* stack);
ScopeFrame* scope_stack_current_loop(ScopeStack* stack);
int scope_stack_depth(const ScopeStack* stack);
int scope_stack_loop_depth(const ScopeStack* stack);
bool scope_stack_is_in_loop(const ScopeStack* stack);
ScopeFrame* scope_stack_get_frame(ScopeStack* stack, int index);
ScopeFrame* scope_stack_find_loop_by_label(ScopeStack* stack, const char* label);

#endif // ORUS_COMPILER_SCOPE_STACK_H
