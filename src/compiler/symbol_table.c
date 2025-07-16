#include "compiler/symbol_table.h"
#include <stdlib.h>
#include <string.h>
#include "runtime/memory.h"

#define INITIAL_CAPACITY 16
#define MAX_LOAD_FACTOR 0.75

static uint64_t fnv1a_hash(const char* key) {
    uint64_t hash = FNV_OFFSET_BASIS;
    while (*key) {
        hash ^= (unsigned char)(*key++);
        hash *= FNV_PRIME;
    }
    return hash;
}

static SymbolEntry* find_entry(SymbolEntry* entries, size_t capacity, uint64_t hash, const char* name) {
    size_t index = hash & (capacity - 1);
    SymbolEntry* tombstone = NULL;
    for (;;) {
        SymbolEntry* entry = &entries[index];
        if (!entry->name) {
            if (!entry->is_tombstone) {
                return tombstone ? tombstone : entry;
            } else if (!tombstone) {
                tombstone = entry;
            }
        } else if (entry->hash == hash && strcmp(entry->name, name) == 0) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void adjust_capacity(SymbolTable* table, size_t new_capacity) {
    SymbolEntry* new_entries = calloc(new_capacity, sizeof(SymbolEntry));
    for (size_t i = 0; i < table->capacity; i++) {
        SymbolEntry* entry = &table->entries[i];
        if (entry->name && !entry->is_tombstone) {
            SymbolEntry* dest = find_entry(new_entries, new_capacity, entry->hash, entry->name);
            *dest = *entry;
        }
    }
    free(table->entries);
    table->entries = new_entries;
    table->capacity = new_capacity;
}

void symbol_table_init(SymbolTable* table) {
    table->capacity = INITIAL_CAPACITY;
    table->count = 0;
    table->entries = calloc(table->capacity, sizeof(SymbolEntry));
}

void symbol_table_free(SymbolTable* table) {
    if (table->entries) {
        for (size_t i = 0; i < table->capacity; i++) {
            if (table->entries[i].name && !table->entries[i].is_tombstone) {
                free((char*)table->entries[i].name);
            }
        }
        free(table->entries);
    }
    table->entries = NULL;
    table->capacity = 0;
    table->count = 0;
}

bool symbol_table_set(SymbolTable* table, const char* name, int index) {
    if (!name) return false;
    if ((double)(table->count + 1) / (double)table->capacity > MAX_LOAD_FACTOR) {
        adjust_capacity(table, table->capacity * 2);
    }
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry(table->entries, table->capacity, hash, name);
    bool is_new = entry->name == NULL;
    if (is_new && !entry->is_tombstone) {
        table->count++;
    } else if (is_new && entry->is_tombstone && entry->name) {
        free((char*)entry->name);
    }
    if (is_new || entry->is_tombstone) {
        entry->name = copyString(name, (int)strlen(name));
    }
    entry->hash = hash;
    entry->index = index;
    entry->is_tombstone = false;
    return is_new;
}

bool symbol_table_get(SymbolTable* table, const char* name, int* out_index) {
    if (!table->entries || table->capacity == 0) return false;
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry(table->entries, table->capacity, hash, name);
    if (!entry->name) return false;
    if (out_index) *out_index = entry->index;
    return true;
}

void symbol_table_remove(SymbolTable* table, const char* name) {
    if (!table->entries || table->capacity == 0) return;
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry(table->entries, table->capacity, hash, name);
    if (!entry->name) return;
    free((char*)entry->name);
    entry->name = NULL;
    entry->is_tombstone = true;
    table->count--;
}
