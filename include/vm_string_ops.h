#ifndef ORUS_VM_STRING_OPS_H
#define ORUS_VM_STRING_OPS_H

#include <stddef.h>
#include "vm.h"

// StringBuilder facilitates efficient string concatenation.
typedef struct {
    char* buffer;
    size_t capacity;
    size_t length;
} StringBuilder;

StringBuilder* createStringBuilder(size_t initial_capacity);
void appendToStringBuilder(StringBuilder* sb, const char* str, size_t len);
ObjString* stringBuilderToString(StringBuilder* sb);
void freeStringBuilder(StringBuilder* sb);

#endif // ORUS_VM_STRING_OPS_H
