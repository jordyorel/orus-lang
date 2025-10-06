// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/runtime/memory.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares runtime memory management helpers and allocation APIs.


#ifndef ORUS_MEMORY_H
#define ORUS_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

#include "vm/vm.h"

void initMemory(void);
void collectGarbage(void);
void freeObjects(void);
void pauseGC(void);
void resumeGC(void);

ObjString* allocateString(const char* chars, int length);
ObjString* allocateStringFromBuffer(char* buffer, size_t capacity, int length);
ObjString* allocateStringFromRope(StringRope* rope);
ObjArray* allocateArray(int capacity);
ObjArrayIterator* allocateArrayIterator(ObjArray* array);
void arrayEnsureCapacity(ObjArray* array, int minCapacity);
bool arrayPush(ObjArray* array, Value value);
bool arrayPop(ObjArray* array, Value* outValue);
bool arrayGet(const ObjArray* array, int index, Value* outValue);
bool arraySet(ObjArray* array, int index, Value value);
ObjError* allocateError(ErrorType type, const char* message, SrcLocation location);
ObjRangeIterator* allocateRangeIterator(int64_t start, int64_t end, int64_t step);
ObjFunction* allocateFunction(void);
ObjClosure* allocateClosure(ObjFunction* function);
ObjEnumInstance* allocateEnumInstance(ObjString* typeName, ObjString* variantName, int variantIndex, ObjArray* payload);
char* copyString(const char* chars, int length);

#endif
