#ifndef ORUS_SYMBOL_TABLE_H
#define ORUS_SYMBOL_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t hash;
    const char* name;
    int index;       // Local index
    bool is_tombstone;
} SymbolEntry;

typedef struct {
    SymbolEntry* entries;
    size_t capacity;
    size_t count;
} SymbolTable;

void symbol_table_init(SymbolTable* table);
void symbol_table_free(SymbolTable* table);
bool symbol_table_set(SymbolTable* table, const char* name, int index);
bool symbol_table_get(SymbolTable* table, const char* name, int* out_index);
void symbol_table_remove(SymbolTable* table, const char* name);

#endif // ORUS_SYMBOL_TABLE_H
