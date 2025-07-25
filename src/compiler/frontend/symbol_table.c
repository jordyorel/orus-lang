// Orus Language Compiler - Symbol Table Implementation



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
    
    // Initialize scope stack
    table->scope_stack_capacity = 16;
    table->scope_stack_size = 0;
    table->scope_stack = calloc(table->scope_stack_capacity, sizeof(int));
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
    if (table->scope_stack) {
        free(table->scope_stack);
    }
    table->entries = NULL;
    table->scope_stack = NULL;
    table->capacity = 0;
    table->count = 0;
    table->scope_stack_size = 0;
    table->scope_stack_capacity = 0;
}

bool symbol_table_set(SymbolTable* table, const char* name, int index, int scope_depth) {
    if (!name) return false;
    if ((double)(table->count + 1) / (double)table->capacity > MAX_LOAD_FACTOR) {
        adjust_capacity(table, table->capacity * 2);
    }
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry(table->entries, table->capacity, hash, name);
    bool is_new = entry->name == NULL;
    
    // Conservative duplicate detection: only prevent obvious duplicates in the same basic scope
    // Due to architectural limitations, all variables are currently globals, so we need to be careful
    // about when to report duplicates vs. allow variable updates/shadowing
    if (!is_new && entry->scope_depth == scope_depth && scope_depth == 0) {
        // Only prevent duplicates at the top-level global scope
        // This catches obvious cases like: x = 42; x = 100
        return false;
    }
    // Allow all other cases (loop variables, block variables, etc.)
    // This is a conservative approach that avoids false positives
    
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
    entry->is_tombstone = false;
    
    // Return true for successful set operations
    // Return false only if we detected a true duplicate at global scope
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

// Helper function to check if a scope is still active
__attribute__((unused)) static bool is_scope_active(SymbolTable* table, int scope_depth) {
    for (int i = 0; i < table->scope_stack_size; i++) {
        if (table->scope_stack[i] == scope_depth) {
            return true;
        }
    }
    return false;
}

// Scope management functions
void symbol_table_begin_scope(SymbolTable* table, int scope_depth) {
    // Add scope to the active scope stack
    if (table->scope_stack_size >= table->scope_stack_capacity) {
        table->scope_stack_capacity *= 2;
        table->scope_stack = realloc(table->scope_stack, 
                                   table->scope_stack_capacity * sizeof(int));
    }
    table->scope_stack[table->scope_stack_size++] = scope_depth;
}

void symbol_table_end_scope(SymbolTable* table, int scope_depth) {
    if (!table->entries || table->capacity == 0) return;
    
    // Remove scope from active scope stack
    for (int i = 0; i < table->scope_stack_size; i++) {
        if (table->scope_stack[i] == scope_depth) {
            // Shift remaining scopes down
            for (int j = i; j < table->scope_stack_size - 1; j++) {
                table->scope_stack[j] = table->scope_stack[j + 1];
            }
            table->scope_stack_size--;
            break;
        }
    }
    
    // NON-DESTRUCTIVE: Only remove variables from the ending scope 
    // if they are not accessible from any remaining active scope
    for (size_t i = 0; i < table->capacity; i++) {
        SymbolEntry* entry = &table->entries[i];
        if (entry->name && !entry->is_tombstone && entry->scope_depth == scope_depth) {
            // Check if this variable's scope is still accessible from remaining active scopes
            bool still_accessible = false;
            for (int j = 0; j < table->scope_stack_size; j++) {
                if (table->scope_stack[j] >= scope_depth) {
                    still_accessible = true;
                    break;
                }
            }
            
            // Only destroy if truly inaccessible
            if (!still_accessible) {
                free((char*)entry->name);
                entry->name = NULL;
                entry->is_tombstone = true;
                table->count--;
            }
        }
    }
}

bool symbol_table_get_in_scope(SymbolTable* table, const char* name, int scope_depth, int* out_index) {
    if (!table->entries || table->capacity == 0) return false;
    uint64_t hash = fnv1a_hash(name);
    SymbolEntry* entry = find_entry_with_scope(table->entries, table->capacity, hash, name, scope_depth);
    if (!entry || !entry->name) return false;
    if (out_index) *out_index = entry->index;
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
