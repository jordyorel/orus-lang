/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/register_cache.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares helpers managing cached register state between VM frames.
 */

// register_cache.h - Phase 4: Advanced Register Optimizations
#ifndef REGISTER_CACHE_H
#define REGISTER_CACHE_H

#include "vm/vm.h"
#include <stdint.h>
#include <stdbool.h>

// Phase 4: Register cache configuration
#define L1_CACHE_SIZE 64           // L1 cache for hot registers
#define L2_CACHE_SIZE 256          // L2 cache for warm registers
#define PREFETCH_LOOKAHEAD 8       // Number of registers to prefetch ahead
#define CACHE_LINE_SIZE 64         // CPU cache line size

// Phase 4: Cache entry structure
typedef struct CacheEntry {
    uint16_t register_id;          // Register ID being cached
    Value value;                   // Cached value
    uint64_t access_count;         // Number of times accessed
    uint64_t last_access_time;     // Last access timestamp
    bool is_dirty;                 // Whether value needs writeback
    bool is_valid;                 // Whether entry is valid
} CacheEntry;

// Phase 4: Register cache structure
typedef struct RegisterCache {
    // L1 cache (direct-mapped, very fast)
    CacheEntry l1_cache[L1_CACHE_SIZE];
    uint64_t l1_hits;
    uint64_t l1_misses;
    
    // L2 cache (associative, larger)
    CacheEntry l2_cache[L2_CACHE_SIZE];
    uint64_t l2_hits;
    uint64_t l2_misses;
    
    // Prefetch buffer
    struct {
        uint16_t register_ids[PREFETCH_LOOKAHEAD];
        Value values[PREFETCH_LOOKAHEAD];
        bool valid[PREFETCH_LOOKAHEAD];
        uint8_t head;              // Next position to fill
        uint8_t tail;              // Next position to consume
    } prefetch_buffer;
    
    // Cache statistics
    uint64_t total_accesses;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t writebacks;
    uint64_t prefetch_hits;
    
    // Cache management
    uint64_t current_time;         // Virtual time counter
    bool caching_enabled;          // Whether caching is active
} RegisterCache;

// Phase 4: Register cache lifecycle
RegisterCache* create_register_cache(void);
void free_register_cache(RegisterCache* cache);
void reset_register_cache(RegisterCache* cache);

// Phase 4: Cache access functions
Value* cached_get_register(RegisterCache* cache, RegisterFile* rf, uint16_t id);
void cached_set_register(RegisterCache* cache, RegisterFile* rf, uint16_t id, Value value);

// Phase 4: Cache management
void flush_register_cache(RegisterCache* cache, RegisterFile* rf);
void invalidate_cache_entry(RegisterCache* cache, uint16_t register_id);
void prefetch_registers(RegisterCache* cache, RegisterFile* rf, uint16_t* register_ids, uint8_t count);

// Phase 4: Cache optimization
void update_access_pattern(RegisterCache* cache, uint16_t register_id);
void predict_next_access(RegisterCache* cache, RegisterFile* rf, uint16_t current_id);

// Phase 4: Cache statistics
void get_cache_stats(RegisterCache* cache, uint64_t* hit_rate, uint64_t* miss_rate, uint64_t* prefetch_effectiveness);
void print_cache_stats(RegisterCache* cache);

// Phase 4: Cache tuning
void tune_cache_parameters(RegisterCache* cache, uint64_t instruction_count);
bool should_cache_register(RegisterCache* cache, uint16_t register_id);

#endif // REGISTER_CACHE_H