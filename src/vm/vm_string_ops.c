// vm_string_ops.c - String operations and optimizations
#include "vm_string_ops.h"
#include "memory.h"
#include "vm_constants.h"
#include <string.h>

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
    ObjString* result = allocateString(sb->buffer, (int)sb->length);
    return result;
}

void freeStringBuilder(StringBuilder* sb) {
    reallocate(sb->buffer, sb->capacity, 0);
    reallocate(sb, sizeof(StringBuilder), 0);
}
