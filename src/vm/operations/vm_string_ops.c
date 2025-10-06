// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/operations/vm_string_ops.c
// Author: Jordy Orel KONDA
// Copyright (c) 2023 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Provides helper functions implementing string bytecode operations.


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

static void rope_init_leaf(StringRope* rope, size_t len) {
    rope->kind = ROPE_LEAF;
    rope->total_len = len;
    rope->depth = 1;
    rope->refcount = 1;
    rope->hash_cache = 0;
    rope->hash_valid = false;
}

static void rope_init_common(StringRope* rope, RopeKind kind, size_t total_len, uint32_t depth) {
    rope->kind = kind;
    rope->total_len = total_len;
    rope->depth = depth;
    rope->refcount = 1;
    rope->hash_cache = 0;
    rope->hash_valid = false;
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
    rope_init_leaf(r, len);
    r->as.leaf.data = (char*)reallocate(NULL, 0, len + 1);
    memcpy(r->as.leaf.data, str, len);
    r->as.leaf.data[len] = '\0';
    r->as.leaf.len = len;
    r->as.leaf.is_ascii = buffer_is_ascii(r->as.leaf.data, len);
    r->as.leaf.is_interned = false;
    r->as.leaf.owns_data = true;
    return r;
}

StringRope* rope_from_buffer(char* buffer, size_t len, bool owns_data) {
    if (!buffer) return NULL;
    StringRope* r = (StringRope*)reallocate(NULL, 0, sizeof(StringRope));
    rope_init_leaf(r, len);
    r->as.leaf.data = buffer;
    r->as.leaf.len = len;
    r->as.leaf.is_ascii = buffer_is_ascii(buffer, len);
    r->as.leaf.is_interned = false;
    r->as.leaf.owns_data = owns_data;
    return r;
}

void rope_retain(StringRope* rope) {
    if (!rope) {
        return;
    }
    rope->refcount++;
}

void rope_release(StringRope* rope) {
    if (!rope) {
        return;
    }

    if (--rope->refcount > 0) {
        return;
    }

    switch (rope->kind) {
        case ROPE_LEAF:
            if (rope->as.leaf.owns_data && rope->as.leaf.data) {
                free(rope->as.leaf.data);
            }
            break;
        case ROPE_CONCAT:
            rope_release(rope->as.concat.left);
            rope_release(rope->as.concat.right);
            break;
        case ROPE_SUBSTRING:
            rope_release(rope->as.substring.base);
            break;
    }

    reallocate(rope, sizeof(StringRope), 0);
}

static uint32_t rope_child_depth(const StringRope* rope) {
    return rope ? rope->depth : 0;
}

static size_t rope_child_length(const StringRope* rope) {
    return rope ? rope->total_len : 0;
}

StringRope* rope_concat(StringRope* left, StringRope* right) {
    if (!left && !right) {
        return NULL;
    }
    if (!left) {
        rope_retain(right);
        return right;
    }
    if (!right) {
        rope_retain(left);
        return left;
    }

    StringRope* node = (StringRope*)reallocate(NULL, 0, sizeof(StringRope));
    size_t total_len = rope_child_length(left) + rope_child_length(right);
    uint32_t depth = 1 + (rope_child_depth(left) > rope_child_depth(right)
                              ? rope_child_depth(left)
                              : rope_child_depth(right));
    rope_init_common(node, ROPE_CONCAT, total_len, depth);
    node->as.concat.left = left;
    node->as.concat.right = right;
    rope_retain(left);
    rope_retain(right);
    return node;
}

static size_t rope_length_internal(const StringRope* rope) {
    return rope ? rope->total_len : 0;
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

static char* rope_copy_range(const StringRope* rope, size_t start, size_t len, char* dest);

static char* rope_copy_all(const StringRope* rope, char* dest) {
    if (!rope) {
        return dest;
    }

    switch (rope->kind) {
        case ROPE_LEAF:
            memcpy(dest, rope->as.leaf.data, rope->as.leaf.len);
            return dest + rope->as.leaf.len;
        case ROPE_CONCAT:
            dest = rope_copy_all(rope->as.concat.left, dest);
            return rope_copy_all(rope->as.concat.right, dest);
        case ROPE_SUBSTRING:
            return rope_copy_range(rope->as.substring.base, rope->as.substring.start,
                                   rope->as.substring.len, dest);
    }
    return dest;
}

static char* rope_copy_range(const StringRope* rope, size_t start, size_t len, char* dest) {
    if (!rope || len == 0) {
        return dest;
    }

    switch (rope->kind) {
        case ROPE_LEAF: {
            if (start >= rope->as.leaf.len) {
                return dest;
            }
            size_t copy_len = len;
            if (start + copy_len > rope->as.leaf.len) {
                copy_len = rope->as.leaf.len - start;
            }
            memcpy(dest, rope->as.leaf.data + start, copy_len);
            return dest + copy_len;
        }
        case ROPE_CONCAT: {
            size_t left_len = rope_length_internal(rope->as.concat.left);
            if (start < left_len) {
                size_t left_copy = left_len - start;
                if (left_copy > len) {
                    left_copy = len;
                }
                dest = rope_copy_range(rope->as.concat.left, start, left_copy, dest);
                if (left_copy < len) {
                    dest = rope_copy_range(rope->as.concat.right, 0, len - left_copy, dest);
                }
                return dest;
            }
            return rope_copy_range(rope->as.concat.right, start - left_len, len, dest);
        }
        case ROPE_SUBSTRING:
            return rope_copy_range(rope->as.substring.base,
                                   rope->as.substring.start + start,
                                   len, dest);
    }
    return dest;
}

char* rope_to_cstr(StringRope* rope) {
    if (!rope) return NULL;
    size_t len = rope_length(rope);
    char* buffer = (char*)reallocate(NULL, 0, len + 1);
    char* end = rope_copy_all(rope, buffer);
    *end = '\0';
    return buffer;
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

const char* string_get_chars(ObjString* string) {
    if (!string) {
        return NULL;
    }

    if (string->chars) {
        return string->chars;
    }

    if (!string->rope) {
        return NULL;
    }

    size_t len = rope_length(string->rope);
    char* buffer = (char*)reallocate(NULL, 0, len + 1);
    char* end = rope_copy_all(string->rope, buffer);
    *end = '\0';

    StringRope* old_rope = string->rope;
    string->chars = buffer;
    string->length = (int)len;
    string->rope = rope_from_buffer(buffer, len, false);
    rope_release(old_rope);

    return string->chars;
}

ObjString* rope_concat_strings(ObjString* left, ObjString* right) {
    if (!left && !right) {
        return allocateString("", 0);
    }

    StringRope* left_rope = left ? left->rope : NULL;
    if (left && !left_rope && left->chars) {
        left_rope = rope_from_buffer(left->chars, (size_t)left->length, false);
        left->rope = left_rope;
    }

    StringRope* right_rope = right ? right->rope : NULL;
    if (right && !right_rope && right->chars) {
        right_rope = rope_from_buffer(right->chars, (size_t)right->length, false);
        right->rope = right_rope;
    }

    StringRope* combined = rope_concat(left_rope, right_rope);
    if (!combined) {
        return allocateString("", 0);
    }

    return allocateStringFromRope(combined);
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
    rope_release(rope);
}

void free_string_table(StringInternTable* table) {
    if (!table || !table->interned) return;

    hashmap_free(table->interned);
    table->interned = NULL;
    table->total_interned = 0;
}
