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

// Find entry in current scope or outer scopes (scope-aware lookup)
static SymbolEntry* find_entry_with_scope(SymbolEntry* entries, size_t capacity, uint64_t hash, const char* name, int max_scope_depth) {
    size_t index = hash & (capacity - 1);
    SymbolEntry* best_match = NULL;
    int best_scope = -1;
    
    for (size_t attempts = 0; attempts < capacity; attempts++) {
        SymbolEntry* entry = &entries[index];
        if (!entry->name && !entry->is_tombstone) {
            break; // Empty slot, no more entries
        }
        if (entry->name && entry->hash == hash && strcmp(entry->name, name) == 0) {
            // Found matching name, check if it's in an accessible scope
            if (entry->scope_depth <= max_scope_depth && entry->scope_depth > best_scope) {
                best_match = entry;
                best_scope = entry->scope_depth;
            }
        }
        index = (index + 1) & (capacity - 1);
    }
    return best_match;
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

bool symbol_table_set(SymbolTable* table, const char* name, int index, int scope_depth, int scope_id, bool is_mutable) {
    if (!name) return false;
    if ((double)(table->count + 1) / (double)table->capacity > MAX_LOAD_FACTOR) {
        adjust_capacity(table, table->capacity * 2);
    }
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry(table->entries, table->capacity, hash, name);
    bool is_new = entry->name == NULL;

    // Check for illegal shadowing in same scope hierarchy
    if (!is_new && entry->scope_id == scope_id && strcmp(entry->name, name) == 0) {
        return false; // Reject redeclaration in same scope
    }

    // Check for assignment to immutable variable
    if (!is_new && !entry->is_mutable && entry->scope_depth <= scope_depth) {
        return false; // Cannot assign to immutable variable
    }

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
    entry->scope_depth = scope_depth;
    entry->scope_id = scope_id;
    entry->is_mutable = is_mutable;
    entry->is_tombstone = false;
    
    return true;
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

// Scope management functions
void symbol_table_begin_scope(SymbolTable* table, int scope_depth, int scope_id) {
    // Track scope hierarchy in compiler state
    // The actual scope management is handled during variable declaration/assignment
    (void)table;
    (void)scope_depth; 
    (void)scope_id;
}

void symbol_table_end_scope(SymbolTable* table, int scope_depth) {
    if (!table->entries || table->capacity == 0) return;
    
    // Remove all variables from the ending scope
    for (size_t i = 0; i < table->capacity; i++) {
        SymbolEntry* entry = &table->entries[i];
        if (entry->name && !entry->is_tombstone && entry->scope_depth == scope_depth) {
            free((char*)entry->name);
            entry->name = NULL;
            entry->is_tombstone = true;
            table->count--;
        }
    }
}

bool symbol_table_get_in_scope(SymbolTable* table, const char* name, int scope_depth, int scope_id, int* out_index, bool* out_is_mutable) {
    if (!table->entries || table->capacity == 0) return false;
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry_with_scope(table->entries, table->capacity, hash, name, scope_depth);
    
    if (!entry || !entry->name) return false;
    if (entry->scope_id > scope_id) return false; // Variable declared in inner scope
    
    if (out_index) *out_index = entry->index;
    if (out_is_mutable) *out_is_mutable = entry->is_mutable;
    return true;
}

// Get symbol only if it exists in exact scope depth (not outer scopes)
bool symbol_table_get_exact_scope(SymbolTable* table, const char* name, int exact_scope_depth, int* out_index) {
    if (!table->entries || table->capacity == 0) return false;
    uint64_t hash = fnv1a_hash(name);
    size_t index = hash & (table->capacity - 1);
    
    for (size_t attempts = 0; attempts < table->capacity; attempts++) {
        SymbolEntry* entry = &table->entries[index];
        if (!entry->name && !entry->is_tombstone) {
            break; // Empty slot, no more entries
        }
        if (entry->name && entry->hash == hash && strcmp(entry->name, name) == 0) {
            // Found matching name, check if it's in the exact scope
            if (entry->scope_depth == exact_scope_depth) {
                if (out_index) *out_index = entry->index;
                return true;
            }
        }
        index = (index + 1) & (table->capacity - 1);
    }
    return false;
}
