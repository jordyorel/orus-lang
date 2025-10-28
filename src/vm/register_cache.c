// Orus Language Project


#include "vm/register_cache.h"
#include "vm/register_file.h"
#include "runtime/memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// Forward declarations for register file internal functions
Value* get_register_internal(RegisterFile* rf, uint16_t id);
void set_register_internal(RegisterFile* rf, uint16_t id, Value value);

// Phase 4: Create register cache
RegisterCache* create_register_cache(void) {
    RegisterCache* cache = (RegisterCache*)malloc(sizeof(RegisterCache));
    if (!cache) return NULL;
    
    // Initialize L1 cache
    memset(cache->l1_cache, 0, sizeof(cache->l1_cache));
    cache->l1_hits = 0;
    cache->l1_misses = 0;
    
    // Initialize L2 cache
    memset(cache->l2_cache, 0, sizeof(cache->l2_cache));
    cache->l2_hits = 0;
    cache->l2_misses = 0;
    
    // Initialize prefetch buffer
    memset(cache->prefetch_buffer.register_ids, 0, sizeof(cache->prefetch_buffer.register_ids));
    memset(cache->prefetch_buffer.values, 0, sizeof(cache->prefetch_buffer.values));
    memset(cache->prefetch_buffer.valid, false, sizeof(cache->prefetch_buffer.valid));
    cache->prefetch_buffer.head = 0;
    cache->prefetch_buffer.tail = 0;
    
    // Initialize statistics
    cache->total_accesses = 0;
    cache->cache_hits = 0;
    cache->cache_misses = 0;
    cache->writebacks = 0;
    cache->prefetch_hits = 0;
    cache->current_time = 0;
    cache->caching_enabled = true;
    
    return cache;
}

// Phase 4: Free register cache
void free_register_cache(RegisterCache* cache) {
    if (cache) {
        free(cache);
    }
}

// Phase 4: Reset cache state
void reset_register_cache(RegisterCache* cache) {
    if (!cache) return;
    
    // Invalidate all cache entries
    for (int i = 0; i < L1_CACHE_SIZE; i++) {
        cache->l1_cache[i].is_valid = false;
        cache->l1_cache[i].is_dirty = false;
    }
    
    for (int i = 0; i < L2_CACHE_SIZE; i++) {
        cache->l2_cache[i].is_valid = false;
        cache->l2_cache[i].is_dirty = false;
    }
    
    // Clear prefetch buffer
    memset(cache->prefetch_buffer.valid, false, sizeof(cache->prefetch_buffer.valid));
    cache->prefetch_buffer.head = 0;
    cache->prefetch_buffer.tail = 0;
}

// Phase 4: Check prefetch buffer first
static Value* check_prefetch_buffer(RegisterCache* cache, uint16_t register_id) {
    for (int i = 0; i < PREFETCH_LOOKAHEAD; i++) {
        if (cache->prefetch_buffer.valid[i] && 
            cache->prefetch_buffer.register_ids[i] == register_id) {
            cache->prefetch_hits++;
            return &cache->prefetch_buffer.values[i];
        }
    }
    return NULL;
}

// Phase 4: L1 cache lookup
static CacheEntry* l1_cache_lookup(RegisterCache* cache, uint16_t register_id) {
    uint32_t index = register_id % L1_CACHE_SIZE;
    CacheEntry* entry = &cache->l1_cache[index];
    
    if (entry->is_valid && entry->register_id == register_id) {
        cache->l1_hits++;
        entry->last_access_time = cache->current_time++;
        entry->access_count++;
        return entry;
    }
    
    cache->l1_misses++;
    return NULL;
}

// Phase 4: L2 cache lookup (simple associative)
static CacheEntry* l2_cache_lookup(RegisterCache* cache, uint16_t register_id) {
    for (int i = 0; i < L2_CACHE_SIZE; i++) {
        CacheEntry* entry = &cache->l2_cache[i];
        if (entry->is_valid && entry->register_id == register_id) {
            cache->l2_hits++;
            entry->last_access_time = cache->current_time++;
            entry->access_count++;
            return entry;
        }
    }
    
    cache->l2_misses++;
    return NULL;
}

// Phase 4: Find LRU entry in L2 cache
static CacheEntry* find_lru_l2_entry(RegisterCache* cache) {
    CacheEntry* lru_entry = &cache->l2_cache[0];
    uint64_t oldest_time = lru_entry->last_access_time;
    
    for (int i = 1; i < L2_CACHE_SIZE; i++) {
        CacheEntry* entry = &cache->l2_cache[i];
        if (!entry->is_valid) {
            return entry; // Found empty slot
        }
        if (entry->last_access_time < oldest_time) {
            oldest_time = entry->last_access_time;
            lru_entry = entry;
        }
    }
    
    return lru_entry;
}

// Phase 4: Cache a register value in L1
static void cache_in_l1(RegisterCache* cache, uint16_t register_id, Value value) {
    uint32_t index = register_id % L1_CACHE_SIZE;
    CacheEntry* entry = &cache->l1_cache[index];
    
    // If entry is dirty, it needs writeback (would go to actual register file)
    if (entry->is_valid && entry->is_dirty) {
        cache->writebacks++;
    }
    
    entry->register_id = register_id;
    entry->value = value;
    entry->access_count = 1;
    entry->last_access_time = cache->current_time++;
    entry->is_dirty = false;
    entry->is_valid = true;
}

// Phase 4: Cache a register value in L2
__attribute__((unused)) static void cache_in_l2(RegisterCache* cache, uint16_t register_id, Value value) {
    CacheEntry* entry = find_lru_l2_entry(cache);
    
    // If entry is dirty, it needs writeback
    if (entry->is_valid && entry->is_dirty) {
        cache->writebacks++;
    }
    
    entry->register_id = register_id;
    entry->value = value;
    entry->access_count = 1;
    entry->last_access_time = cache->current_time++;
    entry->is_dirty = false;
    entry->is_valid = true;
}

// Phase 4: Cached register get with multi-level cache
Value* cached_get_register(RegisterCache* cache, RegisterFile* rf, uint16_t id) {
    if (!cache || !cache->caching_enabled) {
        return get_register(rf, id);
    }
    
    cache->total_accesses++;
    
    // 1. Check prefetch buffer first
    Value* prefetch_hit = check_prefetch_buffer(cache, id);
    if (prefetch_hit) {
        return prefetch_hit;
    }
    
    // 2. Check L1 cache
    CacheEntry* l1_entry = l1_cache_lookup(cache, id);
    if (l1_entry) {
        cache->cache_hits++;
        return &l1_entry->value;
    }
    
    // 3. Check L2 cache
    CacheEntry* l2_entry = l2_cache_lookup(cache, id);
    if (l2_entry) {
        cache->cache_hits++;
        // Promote to L1
        cache_in_l1(cache, id, l2_entry->value);
        return &l2_entry->value;
    }
    
    // 4. Cache miss - get from register file
    cache->cache_misses++;
    Value* actual_value = get_register_internal(rf, id);
    if (actual_value) {
        // Cache the value
        if (should_cache_register(cache, id)) {
            cache_in_l1(cache, id, *actual_value);
        }
        
        // Trigger predictive prefetching
        predict_next_access(cache, rf, id);
    }
    
    return actual_value;
}

// Phase 4: Cached register set
void cached_set_register(RegisterCache* cache, RegisterFile* rf, uint16_t id, Value value) {
    if (!cache || !cache->caching_enabled) {
        set_register(rf, id, value);
        return;
    }
    
    // Update actual register file
    set_register_internal(rf, id, value);
    
    // Update cache if present
    CacheEntry* l1_entry = l1_cache_lookup(cache, id);
    if (l1_entry) {
        l1_entry->value = value;
        l1_entry->is_dirty = true;
        return;
    }
    
    CacheEntry* l2_entry = l2_cache_lookup(cache, id);
    if (l2_entry) {
        l2_entry->value = value;
        l2_entry->is_dirty = true;
        // Promote to L1
        cache_in_l1(cache, id, value);
        return;
    }
    
    // Not in cache - cache it if it's worth caching
    if (should_cache_register(cache, id)) {
        cache_in_l1(cache, id, value);
    }
}

// Phase 4: Simple predictive prefetching
void predict_next_access(RegisterCache* cache, RegisterFile* rf, uint16_t current_id) {
    if (!cache || !cache->caching_enabled) return;
    
    // Simple sequential prefetching - assume next register might be accessed
    uint16_t next_ids[PREFETCH_LOOKAHEAD];
    int prefetch_count = 0;
    
    // Prefetch next few registers in sequence
    for (int i = 1; i <= PREFETCH_LOOKAHEAD && prefetch_count < PREFETCH_LOOKAHEAD; i++) {
        uint16_t next_id = current_id + i;
        
        // Don't prefetch if already in cache
        if (!l1_cache_lookup(cache, next_id) && !l2_cache_lookup(cache, next_id)) {
            next_ids[prefetch_count++] = next_id;
        }
    }
    
    if (prefetch_count > 0) {
        prefetch_registers(cache, rf, next_ids, prefetch_count);
    }
}

// Phase 4: Prefetch specific registers
void prefetch_registers(RegisterCache* cache, RegisterFile* rf, uint16_t* register_ids, uint8_t count) {
    if (!cache || !rf || count == 0) return;
    
    for (uint8_t i = 0; i < count && i < PREFETCH_LOOKAHEAD; i++) {
        uint8_t buffer_index = (cache->prefetch_buffer.head + i) % PREFETCH_LOOKAHEAD;
        
        cache->prefetch_buffer.register_ids[buffer_index] = register_ids[i];
        Value* reg_value = get_register_internal(rf, register_ids[i]);
        if (reg_value) {
            cache->prefetch_buffer.values[buffer_index] = *reg_value;
            cache->prefetch_buffer.valid[buffer_index] = true;
        }
    }
    
    cache->prefetch_buffer.head = (cache->prefetch_buffer.head + count) % PREFETCH_LOOKAHEAD;
}

// Phase 4: Determine if register should be cached
bool should_cache_register(RegisterCache* cache, uint16_t register_id) {
    (void)cache;
    
    // Cache global and frame registers (hot), skip temps and spilled (cold)
    return is_global_register(register_id) || is_frame_register(register_id);
}

// Phase 4: Get cache statistics
void get_cache_stats(RegisterCache* cache, uint64_t* hit_rate, uint64_t* miss_rate, uint64_t* prefetch_effectiveness) {
    if (!cache) return;
    
    if (hit_rate && cache->total_accesses > 0) {
        *hit_rate = (cache->cache_hits * 100) / cache->total_accesses;
    }
    
    if (miss_rate && cache->total_accesses > 0) {
        *miss_rate = (cache->cache_misses * 100) / cache->total_accesses;
    }
    
    if (prefetch_effectiveness && cache->cache_hits > 0) {
        *prefetch_effectiveness = (cache->prefetch_hits * 100) / cache->cache_hits;
    }
}

// Phase 4: Print cache statistics
void print_cache_stats(RegisterCache* cache) {
    if (!cache) return;
    
    printf("=== Register Cache Statistics ===\n");
    printf("Total Accesses: %" PRIu64 "\n", cache->total_accesses);
    printf("Cache Hits: %" PRIu64 "\n", cache->cache_hits);
    printf("Cache Misses: %" PRIu64 "\n", cache->cache_misses);
    printf("L1 Hits: %" PRIu64 "\n", cache->l1_hits);
    printf("L1 Misses: %" PRIu64 "\n", cache->l1_misses);
    printf("L2 Hits: %" PRIu64 "\n", cache->l2_hits);
    printf("L2 Misses: %" PRIu64 "\n", cache->l2_misses);
    printf("Prefetch Hits: %" PRIu64 "\n", cache->prefetch_hits);
    printf("Writebacks: %" PRIu64 "\n", cache->writebacks);
    
    if (cache->total_accesses > 0) {
        uint64_t hit_rate = (cache->cache_hits * 100) / cache->total_accesses;
        printf("Hit Rate: %" PRIu64 "%%\n", hit_rate);
    }
}

// Phase 4: Flush all cache entries to register file
void flush_register_cache(RegisterCache* cache, RegisterFile* rf) {
    if (!cache || !rf) return;
    
    // Flush dirty L1 entries
    for (int i = 0; i < L1_CACHE_SIZE; i++) {
        CacheEntry* entry = &cache->l1_cache[i];
        if (entry->is_valid && entry->is_dirty) {
            set_register_internal(rf, entry->register_id, entry->value);
            entry->is_dirty = false;
            cache->writebacks++;
        }
    }
    
    // Flush dirty L2 entries
    for (int i = 0; i < L2_CACHE_SIZE; i++) {
        CacheEntry* entry = &cache->l2_cache[i];
        if (entry->is_valid && entry->is_dirty) {
            set_register_internal(rf, entry->register_id, entry->value);
            entry->is_dirty = false;
            cache->writebacks++;
        }
    }
}

// Phase 4: Invalidate specific cache entry
void invalidate_cache_entry(RegisterCache* cache, uint16_t register_id) {
    if (!cache) return;
    
    // Invalidate in L1 cache
    uint32_t l1_index = register_id % L1_CACHE_SIZE;
    CacheEntry* l1_entry = &cache->l1_cache[l1_index];
    if (l1_entry->is_valid && l1_entry->register_id == register_id) {
        l1_entry->is_valid = false;
        l1_entry->is_dirty = false;
    }
    
    // Invalidate in L2 cache
    for (int i = 0; i < L2_CACHE_SIZE; i++) {
        CacheEntry* l2_entry = &cache->l2_cache[i];
        if (l2_entry->is_valid && l2_entry->register_id == register_id) {
            l2_entry->is_valid = false;
            l2_entry->is_dirty = false;
            break;
        }
    }
    
    // Invalidate in prefetch buffer
    for (int i = 0; i < PREFETCH_LOOKAHEAD; i++) {
        if (cache->prefetch_buffer.valid[i] && 
            cache->prefetch_buffer.register_ids[i] == register_id) {
            cache->prefetch_buffer.valid[i] = false;
        }
    }
}

// Phase 4: Update access pattern (simple frequency tracking)
void update_access_pattern(RegisterCache* cache, uint16_t register_id) {
    if (!cache) return;
    
    // Simple implementation - could be enhanced with more sophisticated tracking
    // For now, just increment access count if found in cache
    uint32_t l1_index = register_id % L1_CACHE_SIZE;
    CacheEntry* l1_entry = &cache->l1_cache[l1_index];
    if (l1_entry->is_valid && l1_entry->register_id == register_id) {
        l1_entry->access_count++;
        l1_entry->last_access_time = cache->current_time++;
    }
}

// Phase 4: Tune cache parameters based on instruction count
void tune_cache_parameters(RegisterCache* cache, uint64_t instruction_count) {
    if (!cache) return;
    
    // Simple adaptive tuning based on performance metrics
    if (cache->total_accesses > 1000) {
        uint64_t hit_rate = (cache->cache_hits * 100) / cache->total_accesses;
        
        // If hit rate is low, consider disabling caching for cold workloads
        if (hit_rate < 20) {
            cache->caching_enabled = false;
        } else if (hit_rate > 80) {
            // High hit rate - could increase cache aggressiveness
            cache->caching_enabled = true;
        }
    }
    
    // Could add more sophisticated tuning based on instruction patterns
    (void)instruction_count; // Unused for now
}