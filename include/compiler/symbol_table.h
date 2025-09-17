#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "compiler/typed_ast.h"
#include "compiler/register_allocator.h"  // For dual register system integration
#include "public/common.h"
#include <stdbool.h>

// Enhanced symbol for dual register system
typedef struct Symbol {
    char* name;                     // Variable name (owned by symbol table)
    
    // DUAL REGISTER SYSTEM INTEGRATION
    struct RegisterAllocation* reg_allocation;  // Register allocation info
    int legacy_register_id;         // Legacy register ID (compatibility)
    
    // Variable metadata
    Type* type;                     // Variable type
    bool is_mutable;                // Can be reassigned (mut vs immutable)
    bool is_initialized;            // Has been assigned a value
    bool is_arithmetic_heavy;       // Used in arithmetic operations frequently

    // Symbol table management
    struct Symbol* next;            // Hash table collision chain

    // Optimization hints
    int usage_count;                // Number of times symbol is accessed
    bool is_loop_variable;          // Used as loop induction variable

    // Lifecycle tracking
    SrcLocation declaration_location;   // Where the symbol was declared
    SrcLocation last_assignment_location;// Most recent assignment location
    bool has_been_read;                 // Has the value been read from
} Symbol;

// Simple hash table symbol table
typedef struct SymbolTable {
    Symbol** symbols;               // Hash table of symbols (array of Symbol*)
    int capacity;                   // Hash table capacity (power of 2)
    int symbol_count;               // Number of symbols in this scope
    struct SymbolTable* parent;     // Parent scope (NULL for global scope)
    int scope_depth;                // Nesting level (0 = global, 1+ = nested)
} SymbolTable;

// Core symbol table operations
SymbolTable* create_symbol_table(SymbolTable* parent);
void free_symbol_table(SymbolTable* table);

// Symbol management - DUAL REGISTER SYSTEM
Symbol* declare_symbol_with_allocation(SymbolTable* table, const char* name, Type* type,
                                      bool is_mutable, struct RegisterAllocation* reg_alloc,
                                      SrcLocation location, bool is_initialized);
Symbol* declare_symbol_legacy(SymbolTable* table, const char* name, Type* type,
                             bool is_mutable, int register_id,
                             SrcLocation location, bool is_initialized);  // Compatibility
Symbol* resolve_symbol(SymbolTable* table, const char* name);
Symbol* resolve_symbol_local_only(SymbolTable* table, const char* name);

// Symbol optimization hints
void mark_symbol_arithmetic_heavy(Symbol* symbol);
void increment_symbol_usage(Symbol* symbol);
void mark_symbol_as_loop_variable(Symbol* symbol);

// Symbol operations
bool symbol_exists(SymbolTable* table, const char* name);
bool can_assign_to_symbol(Symbol* symbol);

// Debugging and utilities
void print_symbol_table(SymbolTable* table, int indent);
int symbol_table_size(SymbolTable* table);

// Hash table utilities (internal)
unsigned int hash_string(const char* str);

#endif