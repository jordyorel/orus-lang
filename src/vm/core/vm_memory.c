/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/core/vm_memory.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements the VM memory allocator and heap management routines.
 */

#include "runtime/memory.h"
#include "vm/vm.h"
#include "vm/vm_string_ops.h"
#include <stdlib.h>
#include <string.h>

static size_t gcThreshold = 0;
static const double GC_HEAP_GROW_FACTOR = 2.0;

static void freeObject(Obj* object);
static Obj* freeLists[OBJ_TYPE_COUNT] = {NULL};
static bool finalizing = false;

void initMemory() {
    vm.bytesAllocated = 0;
    vm.objects = NULL;
    vm.gcPaused = false;
    gcThreshold = 1024 * 1024;
    for (int i = 0; i < OBJ_TYPE_COUNT; i++) freeLists[i] = NULL;
}

void freeObjects() {
    finalizing = true;
    Obj* object = vm.objects;
    while (object) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    vm.objects = NULL;
    finalizing = false;
    for (int i = 0; i < OBJ_TYPE_COUNT; i++) {
        Obj* obj = freeLists[i];
        while (obj) {
            Obj* next = obj->next;
            free(obj);
            obj = next;
        }
        freeLists[i] = NULL;
    }
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        vm.bytesAllocated -= oldSize;
        free(pointer);
        return NULL;
    }
    if (newSize > oldSize) {
        vm.bytesAllocated += newSize - oldSize;
    } else {
        vm.bytesAllocated -= oldSize - newSize;
    }

    void* result = realloc(pointer, newSize);
    if (!result) exit(1);
    return result;
}

static void* allocateObject(size_t size, ObjType type) {
    if (!vm.gcPaused && vm.bytesAllocated > gcThreshold) {
        collectGarbage();
        gcThreshold = (size_t)(vm.bytesAllocated * GC_HEAP_GROW_FACTOR);
    }

    Obj* object = NULL;
    if (freeLists[type]) {
        object = freeLists[type];
        freeLists[type] = freeLists[type]->next;
    } else {
        object = (Obj*)malloc(size);
        if (!object) exit(1);
        vm.bytesAllocated += size;
    }
    object->type = type;
    object->isMarked = false;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

ObjString* allocateString(const char* chars, int length) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = (char*)reallocate(NULL, 0, (size_t)length + 1);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->rope = rope_from_buffer(string->chars, (size_t)length, false);
    string->hash = 0;
    return string;
}

ObjString* allocateStringFromBuffer(char* buffer, size_t capacity, int length) {
    if (!buffer) return NULL;

    size_t desired = (size_t)length + 1;
    if (capacity != desired) {
        buffer = (char*)reallocate(buffer, capacity, desired);
        capacity = desired;
    }

    buffer[length] = '\0';

    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = buffer;
    string->rope = rope_from_buffer(buffer, (size_t)length, false);
    string->hash = 0;
    return string;
}

ObjArray* allocateArray(int capacity) {
    ObjArray* array = (ObjArray*)allocateObject(sizeof(ObjArray), OBJ_ARRAY);
    array->length = 0;
    array->capacity = capacity > 0 ? capacity : 8;
    array->elements = (Value*)reallocate(NULL, 0, sizeof(Value) * array->capacity);
    return array;
}

ObjArrayIterator* allocateArrayIterator(ObjArray* array) {
    ObjArrayIterator* iterator =
        (ObjArrayIterator*)allocateObject(sizeof(ObjArrayIterator), OBJ_ARRAY_ITERATOR);
    iterator->array = array;
    iterator->index = 0;
    return iterator;
}

void arrayEnsureCapacity(ObjArray* array, int minCapacity) {
    if (!array) {
        return;
    }

    if (minCapacity <= array->capacity) {
        return;
    }

    int newCapacity = array->capacity;
    while (newCapacity < minCapacity) {
        newCapacity = newCapacity < 8 ? 8 : newCapacity * 2;
        if (newCapacity < 0) {
            newCapacity = minCapacity;
            break;
        }
    }

    Value* newElements = (Value*)reallocate(
        array->elements,
        sizeof(Value) * array->capacity,
        sizeof(Value) * newCapacity);
    if (!newElements) {
        return;
    }

    array->elements = newElements;
    array->capacity = newCapacity;
}

bool arrayPush(ObjArray* array, Value value) {
    if (!array) {
        return false;
    }

    if (array->length >= array->capacity) {
        arrayEnsureCapacity(array, array->length + 1);
        if (array->length >= array->capacity) {
            return false;
        }
    }

    array->elements[array->length++] = value;
    return true;
}

bool arrayPop(ObjArray* array, Value* outValue) {
    if (!array || array->length == 0) {
        return false;
    }

    array->length--;
    if (outValue) {
        *outValue = array->elements[array->length];
    }
    return true;
}

bool arrayGet(const ObjArray* array, int index, Value* outValue) {
    if (!array || index < 0 || index >= array->length) {
        return false;
    }

    if (outValue) {
        *outValue = array->elements[index];
    }
    return true;
}

bool arraySet(ObjArray* array, int index, Value value) {
    if (!array || index < 0 || index >= array->length) {
        return false;
    }

    array->elements[index] = value;
    return true;
}

ObjError* allocateError(ErrorType type, const char* message, SrcLocation location) {
    ObjError* error = (ObjError*)allocateObject(sizeof(ObjError), OBJ_ERROR);
    error->type = type;
    error->message = intern_string(message, (int)strlen(message));
    error->location.file = location.file;
    error->location.line = location.line;
    error->location.column = location.column;
    return error;
}

ObjRangeIterator* allocateRangeIterator(int64_t start, int64_t end, int64_t step) {
    ObjRangeIterator* it = (ObjRangeIterator*)allocateObject(sizeof(ObjRangeIterator), OBJ_RANGE_ITERATOR);
    it->current = start;
    it->end = end;
    it->step = step;
    return it;
}

ObjFunction* allocateFunction(void) {
    ObjFunction* function = (ObjFunction*)allocateObject(sizeof(ObjFunction), OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->chunk = NULL;
    function->name = NULL;
    return function;
}

ObjClosure* allocateClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = (ObjUpvalue**)reallocate(NULL, 0, sizeof(ObjUpvalue*) * function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }
    
    ObjClosure* closure = (ObjClosure*)allocateObject(sizeof(ObjClosure), OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue* allocateUpvalue(Value* slot) {
    ObjUpvalue* upvalue = (ObjUpvalue*)allocateObject(sizeof(ObjUpvalue), OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = BOOL_VAL(false); // Default value instead of NIL_VAL
    upvalue->next = NULL;
    return upvalue;
}

ObjEnumInstance* allocateEnumInstance(ObjString* typeName, ObjString* variantName, int variantIndex, ObjArray* payload) {
    ObjEnumInstance* instance = (ObjEnumInstance*)allocateObject(sizeof(ObjEnumInstance), OBJ_ENUM_INSTANCE);
    instance->typeName = typeName;
    instance->variantName = variantName;
    instance->variantIndex = variantIndex;
    instance->payload = payload;
    return instance;
}

void markValue(Value value);

void markObject(Obj* object) {
    if (!object || object->isMarked) return;
    object->isMarked = true;

    switch (object->type) {
        case OBJ_STRING:
            break;
        case OBJ_ARRAY: {
            ObjArray* arr = (ObjArray*)object;
            for (int i = 0; i < arr->length; i++) markValue(arr->elements[i]);
            break;
        }
        case OBJ_ERROR: {
            ObjError* err = (ObjError*)object;
            markObject((Obj*)err->message);
            break;
        }
        case OBJ_RANGE_ITERATOR:
            break;
        case OBJ_ARRAY_ITERATOR: {
            ObjArrayIterator* it = (ObjArrayIterator*)object;
            if (it->array) {
                markObject((Obj*)it->array);
            }
            break;
        }
        case OBJ_ENUM_INSTANCE: {
            ObjEnumInstance* inst = (ObjEnumInstance*)object;
            if (inst->typeName) markObject((Obj*)inst->typeName);
            if (inst->variantName) markObject((Obj*)inst->variantName);
            if (inst->payload) markObject((Obj*)inst->payload);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* func = (ObjFunction*)object;
            markObject((Obj*)func->name);
            // Note: chunk is not a heap object, so we don't mark it
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_UPVALUE: {
            ObjUpvalue* upvalue = (ObjUpvalue*)object;
            markValue(upvalue->closed);
            break;
        }
    }
}

void markValue(Value value) {
    switch (value.type) {
        case VAL_STRING:
        case VAL_ARRAY:
        case VAL_ENUM:
        case VAL_ERROR:
        case VAL_RANGE_ITERATOR:
        case VAL_ARRAY_ITERATOR:
        case VAL_FUNCTION:
        case VAL_CLOSURE:
            markObject(value.as.obj);
            break;
        default:
            break;
    }
}

static void markRoots() {
    for (int i = 0; i < REGISTER_COUNT; i++) {
        markValue(vm.registers[i]);
    }

    // Mark any values stored in active VM call frames. During a call we copy
    // the caller's frame and temporary registers into the frame's
    // savedRegisters array so the callee can reuse the primary windows. If the
    // garbage collector runs while a call is in progress those saved values are
    // the only roots for the caller's arrays/strings. Failing to mark them lets
    // the collector free live data, which later manifests as corrupted strings
    // or "Value is not an array" errors when the caller resumes.
    for (int frameIndex = 0; frameIndex < vm.frameCount; frameIndex++) {
        CallFrame* frame = &vm.frames[frameIndex];

        for (int reg = 0; reg < FRAME_REGISTERS; reg++) {
            markValue(frame->registers[reg]);
        }

        int savedCount = frame->savedRegisterCount;
        if (savedCount > FRAME_REGISTERS + TEMP_REGISTERS) {
            savedCount = FRAME_REGISTERS + TEMP_REGISTERS;
        }
        for (int saved = 0; saved < savedCount; saved++) {
            markValue(frame->savedRegisters[saved]);
        }
    }

    // The register file maintains its own linked list of frames for the
    // high-level ENTER/EXIT_FRAME instructions. These frames can also contain
    // live values that must be treated as GC roots.
    for (CallFrame* frame = vm.register_file.frame_stack; frame != NULL; frame = frame->next) {
        for (int reg = 0; reg < FRAME_REGISTERS; reg++) {
            markValue(frame->registers[reg]);
        }

        int savedCount = frame->savedRegisterCount;
        if (savedCount > FRAME_REGISTERS + TEMP_REGISTERS) {
            savedCount = FRAME_REGISTERS + TEMP_REGISTERS;
        }
        for (int saved = 0; saved < savedCount; saved++) {
            markValue(frame->savedRegisters[saved]);
        }
    }

    if (vm.chunk) {
        for (int i = 0; i < vm.chunk->constants.count; i++) {
            markValue(vm.chunk->constants.values[i]);
        }
    }

    for (int i = 0; i < vm.functionCount; i++) {
        Chunk* chunk = vm.functions[i].chunk;
        if (!chunk) continue;
        for (int c = 0; c < chunk->constants.count; c++) {
            markValue(chunk->constants.values[c]);
        }
    }

    for (int i = 0; i < vm.variableCount; i++) {
        markValue(vm.globals[i]);
    }
    markValue(vm.lastError);

    for (int i = 0; i < vm.moduleCount; i++) {
        if (vm.loadedModules[i]) {
            markObject((Obj*)vm.loadedModules[i]);
        }
    }
    for (int i = 0; i < vm.loadingModuleCount; i++) {
        if (vm.loadingModules[i]) {
            markObject((Obj*)vm.loadingModules[i]);
        }
    }

    for (int i = 0; i < vm.nativeFunctionCount; i++) {
        if (vm.nativeFunctions[i].name) {
            markObject((Obj*)vm.nativeFunctions[i].name);
        }
    }
    
    // Mark open upvalues
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }
}

static void sweep() {
    Obj** object = &vm.objects;
    while (*object) {
        if (!(*object)->isMarked) {
            Obj* unreached = *object;
            *object = unreached->next;
            freeObject(unreached);
        } else {
            (*object)->isMarked = false;
            object = &(*object)->next;
        }
    }
}

void collectGarbage() {
    if (vm.gcPaused) return;

    markRoots();
    sweep();
    vm.gcCount++;
}

static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* s = (ObjString*)object;
            vm.bytesAllocated -= sizeof(ObjString);
            if (s->chars) {
                reallocate(s->chars, (size_t)s->length + 1, 0);
            }
            if (s->rope) free_rope(s->rope);
            break;
        }
        case OBJ_ARRAY: {
            ObjArray* a = (ObjArray*)object;
            vm.bytesAllocated -= sizeof(ObjArray) + sizeof(Value) * a->capacity;
            FREE_ARRAY(Value, a->elements, a->capacity);
            break;
        }
        case OBJ_ERROR: {
            vm.bytesAllocated -= sizeof(ObjError);
            break;
        }
        case OBJ_RANGE_ITERATOR:
            vm.bytesAllocated -= sizeof(ObjRangeIterator);
            break;
        case OBJ_ARRAY_ITERATOR:
            vm.bytesAllocated -= sizeof(ObjArrayIterator);
            break;
        case OBJ_ENUM_INSTANCE:
            vm.bytesAllocated -= sizeof(ObjEnumInstance);
            break;
        case OBJ_FUNCTION: {
            ObjFunction* func = (ObjFunction*)object;
            if (func->chunk != NULL) {
                freeChunk(func->chunk);
                free(func->chunk);
            }
            vm.bytesAllocated -= sizeof(ObjFunction);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            vm.bytesAllocated -= sizeof(ObjClosure) + sizeof(ObjUpvalue*) * closure->upvalueCount;
            free(closure->upvalues);
            break;
        }
        case OBJ_UPVALUE: {
            vm.bytesAllocated -= sizeof(ObjUpvalue);
            break;
        }
    }
    if (finalizing) {
        free(object);
    } else {
        object->next = freeLists[object->type];
        freeLists[object->type] = object;
    }
}

void pauseGC() { vm.gcPaused = true; }
void resumeGC() { vm.gcPaused = false; }

char* copyString(const char* chars, int length) {
    char* copy = (char*)malloc(length + 1);
    memcpy(copy, chars, length);
    copy[length] = '\0';
    return copy;
}

ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
    
    ObjUpvalue* createdUpvalue = allocateUpvalue(local);
    createdUpvalue->next = upvalue;
    
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    
    return createdUpvalue;
}

void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

// Chunk operations moved from vm.c
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->columns = NULL;
    chunk->files = NULL;
    chunk->monotonic_range_flags = NULL;
    chunk->constants.count = 0;
    chunk->constants.capacity = 0;
    chunk->constants.values = NULL;
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    FREE_ARRAY(int, chunk->columns, chunk->capacity);
    free(chunk->files);
    free(chunk->monotonic_range_flags);
    FREE_ARRAY(Value, chunk->constants.values, chunk->constants.capacity);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line, int column, const char* file) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code =
            GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines =
            GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
        chunk->columns =
            GROW_ARRAY(int, chunk->columns, oldCapacity, chunk->capacity);
        chunk->files =
            GROW_ARRAY(const char*, chunk->files, oldCapacity, chunk->capacity);
        chunk->monotonic_range_flags =
            GROW_ARRAY(uint8_t, chunk->monotonic_range_flags, oldCapacity, chunk->capacity);
        if (chunk->monotonic_range_flags) {
            memset(chunk->monotonic_range_flags + oldCapacity, 0,
                   (size_t)(chunk->capacity - oldCapacity) * sizeof(uint8_t));
        }
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->columns[chunk->count] = column;
    if (chunk->files) {
        chunk->files[chunk->count] = file;
    }
    if (chunk->monotonic_range_flags) {
        chunk->monotonic_range_flags[chunk->count] = 0;
    }
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    if (chunk->constants.capacity < chunk->constants.count + 1) {
        int oldCapacity = chunk->constants.capacity;
        chunk->constants.capacity = GROW_CAPACITY(oldCapacity);
        chunk->constants.values =
            GROW_ARRAY(Value, chunk->constants.values, oldCapacity,
                       chunk->constants.capacity);
    }

    chunk->constants.values[chunk->constants.count] = value;
    return chunk->constants.count++;
}
