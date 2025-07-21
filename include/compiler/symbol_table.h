#ifndef ORUS_SYMBOL_TABLE_H
#define ORUS_SYMBOL_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t hash;
    const char* name;
    int index;       // Local index
    int scope_depth; // Scope depth when variable was declared
    int scope_id;    // Unique ID of the declaring scope
    bool is_mutable; // Whether variable is mutable
    bool is_tombstone;
} SymbolEntry;

typedef struct {
    SymbolEntry* entries;
    size_t capacity;
    size_t count;
} SymbolTable;

void symbol_table_init(SymbolTable* table);
void symbol_table_free(SymbolTable* table);
bool symbol_table_set(SymbolTable* table, const char* name, int index, int scope_depth);
bool symbol_table_get(SymbolTable* table, const char* name, int* out_index);
void symbol_table_remove(SymbolTable* table, const char* name);

// Scope management functions
void symbol_table_begin_scope(SymbolTable* table, int scope_depth);
void symbol_table_end_scope(SymbolTable* table, int scope_depth);

// Additional scope-specific lookup
bool symbol_table_get_exact_scope(SymbolTable* table, const char* name, int exact_scope_depth, int* out_index);
bool symbol_table_get_in_scope(SymbolTable* table, const char* name, int scope_depth, int* out_index);

#endif // ORUS_SYMBOL_TABLE_H
