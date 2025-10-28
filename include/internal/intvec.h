//  Orus Language Project

#ifndef ORUS_INTVEC_H
#define ORUS_INTVEC_H

#include "runtime/memory.h"

typedef struct {
    int* data;
    int count;
    int capacity;
} IntVec;

static inline IntVec intvec_new() {
    IntVec vec;
    vec.data = NULL;
    vec.count = 0;
    vec.capacity = 0;
    return vec;
}

static inline void intvec_free(IntVec* vec) {
    if (vec->data) {
        FREE_ARRAY(int, vec->data, vec->capacity);
    }
    vec->data = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

static inline void intvec_push(IntVec* vec, int value) {
    if (vec->capacity < vec->count + 1) {
        int oldCapacity = vec->capacity;
        vec->capacity = GROW_CAPACITY(oldCapacity);
        vec->data = GROW_ARRAY(int, vec->data, oldCapacity, vec->capacity);
    }
    vec->data[vec->count++] = value;
}

#endif // ORUS_INTVEC_H
