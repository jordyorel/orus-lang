// Orus Language Project

// spill_manager.h - Phase 2: Register Spilling Header
#ifndef SPILL_MANAGER_H
#define SPILL_MANAGER_H

#include "vm/vm.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct SpillManager SpillManager;

typedef void (*SpillEntryVisitor)(uint16_t register_id, Value* value, void* user_data);

// Spill manager lifecycle
SpillManager* create_spill_manager(void);
void free_spill_manager(SpillManager* manager);

// Spilling operations
uint16_t spill_register_value(SpillManager* manager, Value value);
bool set_spill_register_value(SpillManager* manager, uint16_t register_id, Value value);
void reserve_spill_slot(SpillManager* manager, uint16_t register_id);
bool unspill_register_value(SpillManager* manager, uint16_t register_id, Value* value);
void remove_spilled_register(SpillManager* manager, uint16_t register_id);

// Spill entry iteration (used by GC root scanning)
void spill_manager_iterate(SpillManager* manager, SpillEntryVisitor visitor, void* user_data);
void spill_manager_visit_entries(SpillManager* manager, SpillEntryVisitor visitor, void* user_data);

// Pressure analysis
bool needs_spilling(SpillManager* manager);
uint16_t find_lru_spill(SpillManager* manager);

// Statistics and debugging
void get_spill_stats(SpillManager* manager, size_t* active_spills, size_t* total_capacity);

#endif // SPILL_MANAGER_H
