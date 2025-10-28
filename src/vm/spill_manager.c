// Orus Language Project

// spill_manager.c - Register Spilling Implementation
// HashMap-based register spilling for unlimited variable support

#include "vm/spill_manager.h"
#include "runtime/memory.h"
#include "vm/vm_constants.h"
#include <stdlib.h>
#include <string.h>

#define SPILL_INITIAL_CAPACITY 16
#define SPILL_MAX_LOAD_FACTOR 0.75

// Spill entry structure
typedef struct {
    uint16_t register_id;     // Register ID being spilled
    Value value;              // Spilled value
    bool is_tombstone;        // For deletion handling
    uint8_t last_used;        // LRU tracking
} SpillEntry;

// Spill manager structure  
struct SpillManager {
    SpillEntry* entries;      // Hash table entries
    size_t capacity;          // Hash table capacity
    size_t count;             // Number of active entries
    size_t tombstones;        // Number of tombstone entries
    uint32_t next_spill_id;   // Next available spill ID
    uint8_t lru_counter;      // LRU counter for eviction
};

static SpillEntry* find_spill_entry(SpillEntry* entries, size_t capacity, uint16_t register_id) {
    size_t index = register_id & (capacity - 1);
    SpillEntry* tombstone = NULL;
    
    for (;;) {
        SpillEntry* entry = &entries[index];
        if (entry->register_id == 0) {  // Empty slot
            if (!entry->is_tombstone) {
                return tombstone ? tombstone : entry;
            } else if (!tombstone) {
                tombstone = entry;
            }
        } else if (entry->register_id == register_id) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void resize_spill_table(SpillManager* manager) {
    size_t old_capacity = manager->capacity;
    SpillEntry* old_entries = manager->entries;
    
    manager->capacity = old_capacity * 2;
    manager->entries = (SpillEntry*)calloc(manager->capacity, sizeof(SpillEntry));
    manager->count = 0;
    manager->tombstones = 0;
    
    // Rehash all non-tombstone entries
    for (size_t i = 0; i < old_capacity; i++) {
        SpillEntry* entry = &old_entries[i];
        if (entry->register_id != 0 && !entry->is_tombstone) {
            SpillEntry* dest = find_spill_entry(manager->entries, manager->capacity, entry->register_id);
            *dest = *entry;
            manager->count++;
        }
    }
    
    free(old_entries);
}

// Create spill manager
SpillManager* create_spill_manager(void) {
    SpillManager* manager = (SpillManager*)malloc(sizeof(SpillManager));
    if (!manager) return NULL;
    
    manager->capacity = SPILL_INITIAL_CAPACITY;
    manager->entries = (SpillEntry*)calloc(manager->capacity, sizeof(SpillEntry));
    manager->count = 0;
    manager->tombstones = 0;
    manager->next_spill_id = SPILL_REG_START;
    manager->lru_counter = 0;
    
    return manager;
}

// Free spill manager
void free_spill_manager(SpillManager* manager) {
    if (manager) {
        free(manager->entries);
        free(manager);
    }
}

// Spill a register value
uint16_t spill_register_value(SpillManager* manager, Value value) {
    // Check if we need to resize
    if ((manager->count + manager->tombstones + 1) * 4 > manager->capacity * 3) {
        resize_spill_table(manager);
    }
    
    uint16_t spill_id = manager->next_spill_id++;
    SpillEntry* entry = find_spill_entry(manager->entries, manager->capacity, spill_id);
    
    if (entry->is_tombstone) {
        manager->tombstones--;
    }
    
    entry->register_id = spill_id;
    entry->value = value;
    entry->is_tombstone = false;
    entry->last_used = manager->lru_counter++;
    
    manager->count++;
    return spill_id;
}

// Set a spill register value with explicit register ID
bool set_spill_register_value(SpillManager* manager, uint16_t register_id, Value value) {
    if (!manager) {
        return false;
    }
    
    // Resize if needed (75% load factor)
    if (manager->count + manager->tombstones >= manager->capacity * 3 / 4) {
        resize_spill_table(manager);
    }
    
    SpillEntry* entry = find_spill_entry(manager->entries, manager->capacity, register_id);
    
    if (entry->is_tombstone) {
        manager->tombstones--;
    }
    
    bool is_new = (entry->register_id != register_id || entry->is_tombstone);
    entry->register_id = register_id;
    entry->value = value;
    entry->is_tombstone = false;
    entry->last_used = manager->lru_counter++;
    
    if (is_new) {
        manager->count++;
    }
    
    return true;
}

// Reserve a spill slot with explicit register ID (for parameters)
void reserve_spill_slot(SpillManager* manager, uint16_t register_id) {
    if (!manager) {
        return;
    }
    
    // Resize if needed (75% load factor)
    if (manager->count + manager->tombstones >= manager->capacity * 3 / 4) {
        resize_spill_table(manager);
    }
    
    SpillEntry* entry = find_spill_entry(manager->entries, manager->capacity, register_id);
    
    if (entry->is_tombstone) {
        manager->tombstones--;
    }
    
    bool is_new = (entry->register_id != register_id || entry->is_tombstone);
    entry->register_id = register_id;
    entry->value = BOOL_VAL(false); // Initialize to nil
    entry->is_tombstone = false;
    entry->last_used = manager->lru_counter++;
    
    if (is_new) {
        manager->count++;
    }
    
    // Ensure next_spill_id doesn't conflict with reserved parameter IDs
    if (register_id >= manager->next_spill_id) {
        manager->next_spill_id = register_id + 1;
    }
}

// Unspill a register value
bool unspill_register_value(SpillManager* manager, uint16_t register_id, Value* value) {
    SpillEntry* entry = find_spill_entry(manager->entries, manager->capacity, register_id);
    
    if (entry->register_id != register_id || entry->is_tombstone) {
        return false; // Not found
    }
    
    *value = entry->value;
    entry->last_used = manager->lru_counter++;
    return true;
}

// Remove spilled register
void remove_spilled_register(SpillManager* manager, uint16_t register_id) {
    SpillEntry* entry = find_spill_entry(manager->entries, manager->capacity, register_id);

    if (entry->register_id == register_id && !entry->is_tombstone) {
        entry->is_tombstone = true;
        entry->register_id = 0;
        manager->count--;
        manager->tombstones++;
    }
}

void spill_manager_visit_entries(SpillManager* manager, SpillEntryVisitor visitor, void* user_data) {
    if (!manager || !visitor) {
        return;
    }

    for (size_t i = 0; i < manager->capacity; i++) {
        SpillEntry* entry = &manager->entries[i];
        if (entry->register_id != 0 && !entry->is_tombstone) {
            visitor(entry->register_id, &entry->value, user_data);
        }
    }
}

// Check if spilling is needed (pressure analysis)
bool needs_spilling(SpillManager* manager) {
    (void)manager; // Not used in simple implementation
    // Simple heuristic: spill when we have more than threshold active entries
    // In a more sophisticated implementation, this would analyze register pressure
    return false; // For now, spilling is manual
}

// Get spill statistics
void get_spill_stats(SpillManager* manager, size_t* active_spills, size_t* total_capacity) {
    if (active_spills) *active_spills = manager->count;
    if (total_capacity) *total_capacity = manager->capacity;
}

// Find least recently used spill for eviction
uint16_t find_lru_spill(SpillManager* manager) {
    uint8_t oldest_time = 255;
    uint16_t lru_id = 0;

    for (size_t i = 0; i < manager->capacity; i++) {
        SpillEntry* entry = &manager->entries[i];
        if (entry->register_id != 0 && !entry->is_tombstone) {
            if (entry->last_used < oldest_time) {
                oldest_time = entry->last_used;
                lru_id = entry->register_id;
            }
        }
    }

    return lru_id;
}

void spill_manager_iterate(SpillManager* manager, SpillEntryVisitor visitor, void* user_data) {
    if (!manager || !visitor) {
        return;
    }

    for (size_t i = 0; i < manager->capacity; i++) {
        SpillEntry* entry = &manager->entries[i];
        if (entry->register_id != 0 && !entry->is_tombstone) {
            visitor(entry->register_id, &entry->value, user_data);
        }
    }
}
