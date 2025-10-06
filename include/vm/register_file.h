// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/register_file.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares the VM register file abstraction backing execution contexts.


// register_file.h - Phase 1 & 2: Register File Architecture Header
#ifndef REGISTER_FILE_H
#define REGISTER_FILE_H

#include "vm/vm.h"
#include "vm/register_cache.h"
#include <stdint.h>
#include <stdbool.h>

// Phase 1: Register file management functions
void init_register_file(RegisterFile* rf);
void free_register_file(RegisterFile* rf);

// Phase 1: Register allocation functions
uint16_t allocate_frame_register(RegisterFile* rf);
uint16_t allocate_temp_register(RegisterFile* rf);

// Phase 1: Register access functions (defined in register_file.c)
Value* get_register(RegisterFile* rf, uint16_t id);
void set_register(RegisterFile* rf, uint16_t id, Value value);

// Phase 1: Frame management functions
CallFrame* allocate_frame(RegisterFile* rf);
void deallocate_frame(RegisterFile* rf);

// Phase 1: Register type checking
bool is_global_register(uint16_t id);
bool is_frame_register(uint16_t id);
bool is_temp_register(uint16_t id);
bool is_spilled_register(uint16_t id);

// Phase 2: Spilling functions
void spill_register(RegisterFile* rf, uint16_t id);
void unspill_register(RegisterFile* rf, uint16_t id);
bool register_file_needs_spilling(RegisterFile* rf);
uint16_t allocate_spilled_register(RegisterFile* rf, Value value);

// Phase 2: Register file statistics
void get_register_file_stats(RegisterFile* rf, size_t* global_used, size_t* frame_used, size_t* temp_used, size_t* spilled_count);

// Phase 4: Cache integration functions
void enable_register_caching(RegisterFile* rf);
void disable_register_caching(RegisterFile* rf);
void flush_register_file_cache(RegisterFile* rf);
void print_register_cache_stats(RegisterFile* rf);

#endif // REGISTER_FILE_H