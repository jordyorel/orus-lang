#include "compiler/symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    
    printf("[SYMBOL_TABLE] Created symbol table (depth=%d, capacity=%d)\n", 
           table->scope_depth, table->capacity);
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
            free(symbol);
            symbol = next;
        }
    }
    
    free(table->symbols);
    free(table);
    
    printf("[SYMBOL_TABLE] Freed symbol table\n");
}

Symbol* declare_symbol(SymbolTable* table, const char* name, Type* type, 
                      bool is_mutable, int register_id) {
    if (!table || !name) return NULL;
    
    // Check if symbol already exists in local scope
    if (resolve_symbol_local_only(table, name)) {
        printf("[SYMBOL_TABLE] Error: Symbol '%s' already declared in current scope\n", name);
        return NULL;
    }
    
    // Calculate hash and find bucket
    unsigned int hash = hash_string(name);
    int index = hash % table->capacity;
    
    // Create new symbol
    Symbol* symbol = malloc(sizeof(Symbol));
    if (!symbol) return NULL;
    
    symbol->name = strdup(name);
    symbol->register_id = register_id;
    symbol->type = type;
    symbol->is_mutable = is_mutable;
    symbol->is_initialized = true;  // Assume initialized when declared
    
    // Insert at head of chain
    symbol->next = table->symbols[index];
    table->symbols[index] = symbol;
    table->symbol_count++;
    
    printf("[SYMBOL_TABLE] Declared symbol '%s' -> R%d (%s, %s)\n", 
           name, register_id, 
           is_mutable ? "mutable" : "immutable",
           type ? "typed" : "untyped");
    
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
        printf("[SYMBOL_TABLE] Resolved symbol '%s' -> R%d (local scope)\n", 
               name, symbol->register_id);
        return symbol;
    }
    
    // Try parent scopes
    if (table->parent) {
        symbol = resolve_symbol(table->parent, name);
        if (symbol) {
            printf("[SYMBOL_TABLE] Resolved symbol '%s' -> R%d (parent scope)\n", 
                   name, symbol->register_id);
            return symbol;
        }
    }
    
    printf("[SYMBOL_TABLE] Symbol '%s' not found in any scope\n", name);
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
            printf("- %s -> R%d (%s)\n", 
                   symbol->name, symbol->register_id,
                   symbol->is_mutable ? "mut" : "immut");
            symbol = symbol->next;
        }
    }
}

int symbol_table_size(SymbolTable* table) {
    return table ? table->symbol_count : 0;
}