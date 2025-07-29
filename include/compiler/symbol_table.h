#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "compiler/typed_ast.h"
#include "public/common.h"
#include <stdbool.h>

// Simple symbol for variable storage
typedef struct Symbol {
    char* name;                     // Variable name (owned by symbol table)
    int register_id;                // Assigned VM register
    Type* type;                     // Variable type
    bool is_mutable;                // Can be reassigned (mut vs immutable)
    bool is_initialized;            // Has been assigned a value
    struct Symbol* next;            // Hash table collision chain
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

// Symbol management
Symbol* declare_symbol(SymbolTable* table, const char* name, Type* type, 
                      bool is_mutable, int register_id);
Symbol* resolve_symbol(SymbolTable* table, const char* name);
Symbol* resolve_symbol_local_only(SymbolTable* table, const char* name);

// Symbol operations
bool symbol_exists(SymbolTable* table, const char* name);
bool can_assign_to_symbol(Symbol* symbol);

// Debugging and utilities
void print_symbol_table(SymbolTable* table, int indent);
int symbol_table_size(SymbolTable* table);

// Hash table utilities (internal)
unsigned int hash_string(const char* str);

#endif