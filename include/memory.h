#ifndef ORUS_MEMORY_H
#define ORUS_MEMORY_H

#include <stddef.h>

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

#include "vm.h"

void initMemory(void);
void collectGarbage(void);
void freeObjects(void);
void pauseGC(void);
void resumeGC(void);

ObjString* allocateString(const char* chars, int length);
ObjArray* allocateArray(int capacity);
ObjError* allocateError(ErrorType type, const char* message, SrcLocation location);
ObjRangeIterator* allocateRangeIterator(int64_t start, int64_t end);
ObjFunction* allocateFunction(void);
ObjClosure* allocateClosure(ObjFunction* function);
char* copyString(const char* chars, int length);

#endif
