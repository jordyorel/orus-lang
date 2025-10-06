// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/vm_string_ops.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares string operation helpers and opcode integrations for the VM.


#ifndef ORUS_VM_STRING_OPS_H
#define ORUS_VM_STRING_OPS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct ObjString;
typedef struct ObjString ObjString;
typedef struct HashMap HashMap;

// StringBuilder facilitates efficient string concatenation.
typedef struct {
    char* buffer;
    size_t capacity;
    size_t length;
} StringBuilder;

// Rope node kinds for zero-copy strings
typedef enum {
    ROPE_LEAF,
    ROPE_CONCAT,
    ROPE_SUBSTRING
} RopeKind;

typedef struct StringRope {
    RopeKind kind;
    size_t total_len;
    uint32_t depth;
    uint32_t refcount;
    union {
        struct {
            char* data;
            size_t len;
            bool is_ascii;
            bool is_interned;
            bool owns_data;
        } leaf;
        struct {
            struct StringRope* left;
            struct StringRope* right;
        } concat;
        struct {
            struct StringRope* base;
            size_t start;
            size_t len;
        } substring;
    } as;
    uint32_t hash_cache;
    bool hash_valid;
} StringRope;

typedef struct {
    HashMap* interned;
    size_t threshold;
    size_t total_interned;
} StringInternTable;

extern StringInternTable globalStringTable;

StringBuilder* createStringBuilder(size_t initial_capacity);
void appendToStringBuilder(StringBuilder* sb, const char* str, size_t len);
ObjString* stringBuilderToString(StringBuilder* sb);
ObjString* stringBuilderToOwnedString(StringBuilder* sb);
void freeStringBuilder(StringBuilder* sb);

// Rope helpers
StringRope* rope_from_cstr(const char* str, size_t len);
StringRope* rope_from_buffer(char* buffer, size_t len, bool owns_data);
StringRope* rope_concat(StringRope* left, StringRope* right);
void rope_retain(StringRope* rope);
void rope_release(StringRope* rope);
char* rope_to_cstr(StringRope* rope);
size_t rope_length(const StringRope* rope);
bool rope_char_at(const StringRope* rope, size_t index, char* out);
ObjString* string_char_at(ObjString* string, size_t index);

ObjString* rope_index_to_string(StringRope* rope, size_t index);

// String helpers
const char* string_get_chars(ObjString* string);
ObjString* allocateStringFromRope(StringRope* rope);
ObjString* rope_concat_strings(ObjString* left, ObjString* right);


// Interning
void init_string_table(StringInternTable* table);
ObjString* intern_string(const char* chars, int length);

// Cleanup routines
void free_rope(StringRope* rope);
void free_string_table(StringInternTable* table);

#endif // ORUS_VM_STRING_OPS_H
