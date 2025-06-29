#ifndef GENERIC_ARRAY_H
#define GENERIC_ARRAY_H

#include <stddef.h>

// Forward declaration of the memory reallocator implemented in memory.c.
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// Defines a dynamic array type and inline operations for the given C type.
// Example:
//   DEFINE_ARRAY_TYPE(int, Int)
// creates IntArray with initIntArray, writeIntArray and freeIntArray.
#define DEFINE_ARRAY_TYPE(Type, Name)                                            \
    typedef struct {                                                             \
        int capacity;                                                            \
        int count;                                                               \
        Type* values;                                                            \
    } Name##Array;                                                               \
                                                                                \
    static inline void init##Name##Array(Name##Array* array) {                   \
        array->capacity = 0;                                                     \
        array->count = 0;                                                        \
        array->values = NULL;                                                    \
    }                                                                           \
                                                                                \
    static inline void write##Name##Array(Name##Array* array, Type value) {      \
        if (array->capacity < array->count + 1) {                                \
            int oldCapacity = array->capacity;                                   \
            int newCapacity = oldCapacity < 8 ? 8 : oldCapacity * 2;             \
            array->values = (Type*)reallocate(array->values,                     \
                                             sizeof(Type) * oldCapacity,         \
                                             sizeof(Type) * newCapacity);        \
            array->capacity = newCapacity;                                       \
        }                                                                        \
        array->values[array->count++] = value;                                   \
    }                                                                           \
                                                                                \
    static inline void free##Name##Array(Name##Array* array) {                   \
        reallocate(array->values, sizeof(Type) * array->capacity, 0);            \
        init##Name##Array(array);                                                \
    }

#endif // GENERIC_ARRAY_H
