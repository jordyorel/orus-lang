/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/operations/vm_string_ops.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Provides helper functions implementing string bytecode operations.
 */

#include <stddef.h>
// Forward declarations for HashMap and HashMapEntry
typedef struct HashMap HashMap;
typedef struct HashMapEntry HashMapEntry;
// HashMapEntry definition (copied from type_representation.c)
struct HashMapEntry {
    int key;
    void* value;
    struct HashMapEntry* next;
};
// HashMap definition (copied from type_representation.c)
struct HashMap {
    HashMapEntry** buckets;
    size_t capacity;
    size_t count;
};
// vm_string_ops.c - String operations and optimizations


#include <stddef.h>
#include "vm/vm_string_ops.h"
#include "runtime/memory.h"
#include "vm/vm_constants.h"
#include "type/type.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations for HashMap and HashMapEntry
typedef struct HashMap HashMap;
typedef struct HashMapEntry HashMapEntry;

static bool buffer_is_ascii(const char* data, size_t len) {
    if (!data) return true;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)data[i] >= 0x80) {
            return false;
        }
    }
    return true;
}

StringBuilder* createStringBuilder(size_t initial_capacity) {
    size_t cap = initial_capacity > 0 ? initial_capacity : VM_SMALL_STRING_BUFFER;
    StringBuilder* sb = (StringBuilder*)reallocate(NULL, 0, sizeof(StringBuilder));
    sb->capacity = cap;
    sb->buffer = (char*)reallocate(NULL, 0, sb->capacity);
    sb->length = 0;
    return sb;
}

void appendToStringBuilder(StringBuilder* sb, const char* str, size_t len) {
    if (sb->length + len + 1 > sb->capacity) {
        size_t newCap = (sb->length + len + 1) * 2;
        sb->buffer = (char*)reallocate(sb->buffer, sb->capacity, newCap);
        sb->capacity = newCap;
    }
    memcpy(sb->buffer + sb->length, str, len);
    sb->length += len;
}

ObjString* stringBuilderToString(StringBuilder* sb) {
    sb->buffer[sb->length] = '\0';
    ObjString* result = intern_string(sb->buffer, (int)sb->length);
    return result;
}

ObjString* stringBuilderToOwnedString(StringBuilder* sb) {
    if (!sb) return NULL;

    sb->buffer[sb->length] = '\0';

    ObjString* result = allocateStringFromBuffer(sb->buffer, sb->capacity, (int)sb->length);

    // Release the builder structure but keep the buffer (now owned by the ObjString).
    reallocate(sb, sizeof(StringBuilder), 0);
    return result;
}

void freeStringBuilder(StringBuilder* sb) {
    reallocate(sb->buffer, sb->capacity, 0);
    reallocate(sb, sizeof(StringBuilder), 0);
}

StringRope* rope_from_cstr(const char* str, size_t len) {
    StringRope* r = (StringRope*)reallocate(NULL, 0, sizeof(StringRope));
    r->kind = ROPE_LEAF;
    r->as.leaf.data = (char*)reallocate(NULL, 0, len + 1);
    memcpy(r->as.leaf.data, str, len);
    r->as.leaf.data[len] = '\0';
    r->as.leaf.len = len;
    r->as.leaf.is_ascii = buffer_is_ascii(r->as.leaf.data, len);
    r->as.leaf.is_interned = false;
    r->as.leaf.owns_data = true;
    r->hash_valid = false;
    r->hash_cache = 0;
    return r;
}

StringRope* rope_from_buffer(char* buffer, size_t len, bool owns_data) {
    if (!buffer) return NULL;
    StringRope* r = (StringRope*)reallocate(NULL, 0, sizeof(StringRope));
    r->kind = ROPE_LEAF;
    r->as.leaf.data = buffer;
    r->as.leaf.len = len;
    r->as.leaf.is_ascii = buffer_is_ascii(buffer, len);
    r->as.leaf.is_interned = false;
    r->as.leaf.owns_data = owns_data;
    r->hash_valid = false;
    r->hash_cache = 0;
    return r;
}

static size_t rope_length_internal(const StringRope* rope) {
    if (!rope) return 0;
    switch (rope->kind) {
        case ROPE_LEAF:
            return rope->as.leaf.len;
        case ROPE_CONCAT:
            return rope_length_internal(rope->as.concat.left) +
                   rope_length_internal(rope->as.concat.right);
        case ROPE_SUBSTRING:
            return rope->as.substring.len;
    }
    return 0;
}

size_t rope_length(const StringRope* rope) { return rope_length_internal(rope); }

static bool rope_char_at_internal(const StringRope* rope, size_t index, char* out) {
    if (!rope) return false;
    switch (rope->kind) {
        case ROPE_LEAF:
            if (index >= rope->as.leaf.len) {
                return false;
            }
            *out = rope->as.leaf.data[index];
            return true;
        case ROPE_CONCAT: {
            size_t left_len = rope_length_internal(rope->as.concat.left);
            if (index < left_len) {
                return rope_char_at_internal(rope->as.concat.left, index, out);
            }
            return rope_char_at_internal(rope->as.concat.right, index - left_len, out);
        }
        case ROPE_SUBSTRING:
            if (index >= rope->as.substring.len) {
                return false;
            }
            return rope_char_at_internal(rope->as.substring.base,
                                         rope->as.substring.start + index, out);
    }
    return false;
}

bool rope_char_at(const StringRope* rope, size_t index, char* out) {
    if (!out) {
        return false;
    }
    return rope_char_at_internal(rope, index, out);
}

ObjString* string_char_at(ObjString* string, size_t index) {
    if (!string || !string->rope) {
        return NULL;
    }
    char ch;
    if (!rope_char_at_internal(string->rope, index, &ch)) {
        return NULL;
    }
    char buffer[2];
    buffer[0] = ch;
    buffer[1] = '\0';
    return allocateString(buffer, 1);
}

static void rope_flatten(StringRope* rope, StringBuilder* sb) {
    if (!rope) return;
    switch (rope->kind) {
        case ROPE_LEAF:
            appendToStringBuilder(sb, rope->as.leaf.data, rope->as.leaf.len);
            break;
        case ROPE_CONCAT:
            rope_flatten(rope->as.concat.left, sb);
            rope_flatten(rope->as.concat.right, sb);
            break;
        case ROPE_SUBSTRING:
            appendToStringBuilder(sb,
                                  rope->as.substring.base->as.leaf.data + rope->as.substring.start,
                                  rope->as.substring.len);
            break;
    }
}

char* rope_to_cstr(StringRope* rope) {
    if (!rope) return NULL;
    StringBuilder* sb = createStringBuilder(rope->kind == ROPE_LEAF ? rope->as.leaf.len + 1 : 16);
    rope_flatten(rope, sb);
    sb->buffer[sb->length] = '\0';
    char* result = sb->buffer;
    free(sb); // only free the struct, keep buffer
    return result;
}

ObjString* rope_index_to_string(StringRope* rope, size_t index) {
    char ch = '\0';
    if (!rope_char_at(rope, index, &ch)) {
        return NULL;
    }

    char buffer[2];
    buffer[0] = ch;
    buffer[1] = '\0';
    return allocateString(buffer, 1);
}

StringInternTable globalStringTable;

void init_string_table(StringInternTable* table) {
    table->interned = hashmap_new();
    table->threshold = 32;
    table->total_interned = 0;
}

ObjString* intern_string(const char* chars, int length) {
    size_t hash = 5381;
    for (int i = 0; i < length; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)chars[i];
    }
    char keybuf[32];
    snprintf(keybuf, sizeof(keybuf), "%zu", hash);
    ObjString* existing = (ObjString*)hashmap_get(globalStringTable.interned, keybuf);
    if (existing && existing->length == length && memcmp(existing->chars, chars, length) == 0) {
        return existing;
    }
    ObjString* s = allocateString(chars, length);
    s->rope->as.leaf.is_interned = true;
    hashmap_set(globalStringTable.interned, keybuf, s);
    globalStringTable.total_interned++;
    return s;
}


void free_rope(StringRope* rope) {
    if (!rope) return;
    switch (rope->kind) {
        case ROPE_LEAF:
            if (rope->as.leaf.owns_data && rope->as.leaf.data) {
                free(rope->as.leaf.data);
            }
            break;
        case ROPE_CONCAT:
            free_rope(rope->as.concat.left);
            free_rope(rope->as.concat.right);
            break;
        case ROPE_SUBSTRING:
            free_rope(rope->as.substring.base);
            break;
    }
    free(rope);
}

// --- HashMapIterator stub implementation ---
typedef struct {
    size_t bucket_idx;
    void* current_entry;
    void* map;
} HashMapIterator;

static void hashmap_iterator_init(HashMap* map, HashMapIterator* it) {
    it->map = map;
    it->bucket_idx = 0;
    it->current_entry = NULL;
}

static int hashmap_iterator_has_next(HashMapIterator* it) {
    HashMap* map = (HashMap*)it->map;
    if (!map) return 0;
    while (it->bucket_idx < map->capacity) {
        HashMapEntry* entry = (HashMapEntry*)it->current_entry;
        if (!entry) entry = map->buckets[it->bucket_idx];
        if (entry) {
            it->current_entry = entry;
            return 1;
        }
        it->bucket_idx++;
        it->current_entry = NULL;
    }
    return 0;
}

static void* hashmap_iterator_next_value(HashMapIterator* it) {
    HashMapEntry* entry = (HashMapEntry*)it->current_entry;
    if (!entry) return NULL;
    void* value = entry->value;
    it->current_entry = entry->next;
    if (!it->current_entry) it->bucket_idx++;
    return value;
}

void free_string_table(StringInternTable* table) {
    if (!table || !table->interned) return;
    HashMapIterator it;
    hashmap_iterator_init(table->interned, &it);
    bool skip_object_free = vm.isShuttingDown;
    while (hashmap_iterator_has_next(&it)) {
        ObjString* s = (ObjString*)hashmap_iterator_next_value(&it);
        if (s) {
            if (!skip_object_free) {
                if (s->rope) free_rope(s->rope);
                free(s->chars);
                free(s);
            }
        }
    }
    hashmap_free(table->interned);
    table->interned = NULL;
    table->total_interned = 0;
}
