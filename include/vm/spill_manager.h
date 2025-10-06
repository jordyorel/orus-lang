// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/spill_manager.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares spill management helpers that handle register pressure during execution.

// spill_manager.h - Phase 2: Register Spilling Header
#ifndef SPILL_MANAGER_H
#define SPILL_MANAGER_H

#include "vm/vm.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct SpillManager SpillManager;

typedef void (*SpillEntryVisitor)(uint16_t register_id, Value* value, void* user_data);

// Phase 2: Spill manager lifecycle
SpillManager* create_spill_manager(void);
void free_spill_manager(SpillManager* manager);

// Phase 2: Spilling operations
uint16_t spill_register_value(SpillManager* manager, Value value);
bool set_spill_register_value(SpillManager* manager, uint16_t register_id, Value value);
void reserve_spill_slot(SpillManager* manager, uint16_t register_id);
bool unspill_register_value(SpillManager* manager, uint16_t register_id, Value* value);
void remove_spilled_register(SpillManager* manager, uint16_t register_id);

// Spill entry iteration (used by GC root scanning)
void spill_manager_iterate(SpillManager* manager, SpillEntryVisitor visitor, void* user_data);
void spill_manager_visit_entries(SpillManager* manager, SpillEntryVisitor visitor, void* user_data);

// Phase 2: Pressure analysis
bool needs_spilling(SpillManager* manager);
uint16_t find_lru_spill(SpillManager* manager);

// Phase 2: Statistics and debugging
void get_spill_stats(SpillManager* manager, size_t* active_spills, size_t* total_capacity);

#endif // SPILL_MANAGER_H
