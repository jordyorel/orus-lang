//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/symbol_table.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2022 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Implements symbol table management for declarations, scopes, and resolution.
//  

#include "compiler/symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/strutil.h"

#define SYMBOL_TABLE_INITIAL_CAPACITY 16

// Simple hash function for strings
unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

SymbolTable* create_symbol_table(SymbolTable* parent) {
    SymbolTable* table = malloc(sizeof(SymbolTable));
    if (!table) return NULL;
    
    table->capacity = SYMBOL_TABLE_INITIAL_CAPACITY;
    table->symbols = calloc(table->capacity, sizeof(Symbol*));
    if (!table->symbols) {
        free(table);
        return NULL;
    }
    
    table->symbol_count = 0;
    table->parent = parent;
    table->scope_depth = parent ? parent->scope_depth + 1 : 0;
    
    // printf("[SYMBOL_TABLE] Created symbol table (depth=%d, capacity=%d)\n", 
    //        table->scope_depth, table->capacity);
    return table;
}

void free_symbol_table(SymbolTable* table) {
    if (!table) return;
    
    // Free all symbols and their names
    for (int i = 0; i < table->capacity; i++) {
        Symbol* symbol = table->symbols[i];
        while (symbol) {
            Symbol* next = symbol->next;
            free(symbol->name);
            // Note: We don't free reg_allocation here because it's managed by DualRegisterAllocator
            // The allocator owns the RegisterAllocation structs
            free(symbol);
            symbol = next;
        }
    }
    
    free(table->symbols);
    free(table);
    
    // printf("[SYMBOL_TABLE] Freed symbol table\n");
}

Symbol* declare_symbol(SymbolTable* table, const char* name, Type* type,
                      bool is_mutable, int register_id,
                      SrcLocation location, bool is_initialized) {
    if (!table || !name) return NULL;

    // Check if symbol already exists in local scope
    if (resolve_symbol_local_only(table, name)) {
        // printf("[SYMBOL_TABLE] Error: Symbol '%s' already declared in current scope\n", name);
        return NULL;
    }
    
    // Calculate hash and find bucket
    unsigned int hash = hash_string(name);
    int index = hash % table->capacity;
    
    // Create new symbol
    Symbol* symbol = malloc(sizeof(Symbol));
    if (!symbol) return NULL;
    
    symbol->name = orus_strdup(name);
    symbol->legacy_register_id = register_id;  // Legacy compatibility
    symbol->reg_allocation = NULL;             // Will be set by dual register system
    symbol->type = type;
    symbol->is_mutable = is_mutable;
    symbol->declared_mutable = is_mutable;
    symbol->is_initialized = is_initialized;
    symbol->is_arithmetic_heavy = false;       // Default to false
    symbol->usage_count = 0;                   // Track usage
    symbol->is_loop_variable = false;          // Default to false
    symbol->declaration_location = location;
    symbol->last_assignment_location = is_initialized ? location : (SrcLocation){NULL, 0, 0};
    symbol->has_been_read = false;

    // Insert at head of chain
    symbol->next = table->symbols[index];
    table->symbols[index] = symbol;
    table->symbol_count++;
    
    // printf("[SYMBOL_TABLE] Declared symbol '%s' -> R%d (legacy) (%s, %s)\n", 
    //        name, register_id, 
    //        is_mutable ? "mutable" : "immutable",
    //        type ? "typed" : "untyped");
    
    return symbol;
}

Symbol* resolve_symbol_local_only(SymbolTable* table, const char* name) {
    if (!table || !name) return NULL;
    
    unsigned int hash = hash_string(name);
    int index = hash % table->capacity;
    
    Symbol* symbol = table->symbols[index];
    while (symbol) {
        if (strcmp(symbol->name, name) == 0) {
            return symbol;
        }
        symbol = symbol->next;
    }
    
    return NULL;
}

Symbol* resolve_symbol(SymbolTable* table, const char* name) {
    if (!table || !name) return NULL;
    
    // Try current scope first
    Symbol* symbol = resolve_symbol_local_only(table, name);
    if (symbol) {
        // Increment usage count for optimization hints
        increment_symbol_usage(symbol);
        
        if (symbol->reg_allocation) {
            // printf("[SYMBOL_TABLE] Resolved symbol '%s' -> %s R%d (physical R%d) (local scope)\n", 
            //        name, 
            //        symbol->reg_allocation->strategy == REG_STRATEGY_TYPED ? "TYPED" : "STANDARD",
            //        symbol->reg_allocation->logical_id, symbol->reg_allocation->physical_id);
        } else {
            // printf("[SYMBOL_TABLE] Resolved symbol '%s' -> R%d (legacy) (local scope)\n", 
            //        name, symbol->legacy_register_id);
        }
        return symbol;
    }
    
    // Try parent scopes
    if (table->parent) {
        symbol = resolve_symbol(table->parent, name);
        if (symbol) {
            // Increment usage count for optimization hints
            increment_symbol_usage(symbol);
            
            if (symbol->reg_allocation) {
                // printf("[SYMBOL_TABLE] Resolved symbol '%s' -> %s R%d (physical R%d) (parent scope)\n", 
                //        name, 
                //        symbol->reg_allocation->strategy == REG_STRATEGY_TYPED ? "TYPED" : "STANDARD",
                //        symbol->reg_allocation->logical_id, symbol->reg_allocation->physical_id);
            } else {
                // printf("[SYMBOL_TABLE] Resolved symbol '%s' -> R%d (legacy) (parent scope)\n", 
                //        name, symbol->legacy_register_id);
            }
            return symbol;
        }
    }
    
    // printf("[SYMBOL_TABLE] Symbol '%s' not found in any scope\n", name);
    return NULL;
}

bool symbol_exists(SymbolTable* table, const char* name) {
    return resolve_symbol(table, name) != NULL;
}

bool can_assign_to_symbol(Symbol* symbol) {
    return symbol && symbol->is_mutable;
}

void print_symbol_table(SymbolTable* table, int indent) {
    if (!table) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    printf("SymbolTable (depth=%d, count=%d):\n", table->scope_depth, table->symbol_count);
    
    for (int i = 0; i < table->capacity; i++) {
        Symbol* symbol = table->symbols[i];
        while (symbol) {
            for (int j = 0; j < indent + 1; j++) printf("  ");
            
            if (symbol->reg_allocation) {
                printf("- %s -> %s R%d (physical R%d) (%s, usage=%d)%s%s\n", 
                       symbol->name, 
                       symbol->reg_allocation->strategy == REG_STRATEGY_TYPED ? "TYPED" : "STANDARD",
                       symbol->reg_allocation->logical_id, 
                       symbol->reg_allocation->physical_id,
                       symbol->is_mutable ? "mut" : "immut",
                       symbol->usage_count,
                       symbol->is_arithmetic_heavy ? " [ARITH]" : "",
                       symbol->is_loop_variable ? " [LOOP]" : "");
            } else {
                printf("- %s -> R%d (legacy) (%s, usage=%d)%s%s\n", 
                       symbol->name, 
                       symbol->legacy_register_id,
                       symbol->is_mutable ? "mut" : "immut",
                       symbol->usage_count,
                       symbol->is_arithmetic_heavy ? " [ARITH]" : "",
                       symbol->is_loop_variable ? " [LOOP]" : "");
            }
            
            symbol = symbol->next;
        }
    }
}

int symbol_table_size(SymbolTable* table) {
    return table ? table->symbol_count : 0;
}

// ===== DUAL REGISTER SYSTEM INTEGRATION =====

Symbol* declare_symbol_with_allocation(SymbolTable* table, const char* name, Type* type,
                                      bool is_mutable, struct RegisterAllocation* reg_alloc,
                                      SrcLocation location, bool is_initialized) {
    if (!table || !name || !reg_alloc) return NULL;

    // Check if symbol already exists in local scope
    if (resolve_symbol_local_only(table, name)) {
        // printf("[SYMBOL_TABLE] Error: Symbol '%s' already declared in current scope\n", name);
        return NULL;
    }
    
    // Calculate hash and find bucket
    unsigned int hash = hash_string(name);
    int index = hash % table->capacity;
    
    // Create new symbol
    Symbol* symbol = malloc(sizeof(Symbol));
    if (!symbol) return NULL;
    
    symbol->name = orus_strdup(name);
    symbol->reg_allocation = reg_alloc;        // Dual register system allocation
    symbol->legacy_register_id = reg_alloc->logical_id;  // Compatibility
    symbol->type = type;
    symbol->is_mutable = is_mutable;
    symbol->declared_mutable = is_mutable;
    symbol->is_initialized = is_initialized;
    symbol->is_arithmetic_heavy = false;       // Default to false
    symbol->usage_count = 0;                   // Track usage
    symbol->is_loop_variable = false;          // Default to false
    symbol->declaration_location = location;
    symbol->last_assignment_location = is_initialized ? location : (SrcLocation){NULL, 0, 0};
    symbol->has_been_read = false;

    // Insert at head of chain
    symbol->next = table->symbols[index];
    table->symbols[index] = symbol;
    table->symbol_count++;
    
    // printf("[SYMBOL_TABLE] Declared symbol '%s' -> %s R%d (physical R%d) (%s, %s)\n", 
    //        name, 
    //        reg_alloc->strategy == REG_STRATEGY_TYPED ? "TYPED" : "STANDARD",
    //        reg_alloc->logical_id, reg_alloc->physical_id,
    //        is_mutable ? "mutable" : "immutable",
    //        type ? "typed" : "untyped");
    
    return symbol;
}

Symbol* declare_symbol_legacy(SymbolTable* table, const char* name, Type* type,
                             bool is_mutable, int register_id,
                             SrcLocation location, bool is_initialized) {
    // Legacy compatibility wrapper
    return declare_symbol(table, name, type, is_mutable, register_id, location, is_initialized);
}

void mark_symbol_arithmetic_heavy(Symbol* symbol) {
    if (!symbol) return;
    
    symbol->is_arithmetic_heavy = true;
    // printf("[SYMBOL_TABLE] Marked symbol '%s' as arithmetic-heavy\n", symbol->name);
}

void increment_symbol_usage(Symbol* symbol) {
    if (!symbol) return;
    
    symbol->usage_count++;
    // printf("[SYMBOL_TABLE] Symbol '%s' usage count: %d\n", symbol->name, symbol->usage_count);
}

void mark_symbol_as_loop_variable(Symbol* symbol) {
    if (!symbol) return;
    
    symbol->is_loop_variable = true;
    // printf("[SYMBOL_TABLE] Marked symbol '%s' as loop variable\n", symbol->name);
}
