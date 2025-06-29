#include "memory.h"
#include "vm.h"
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
    string->chars = (char*)reallocate(NULL, 0, length + 1);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = 0;
    vm.bytesAllocated += length + 1;
    return string;
}

ObjArray* allocateArray(int capacity) {
    ObjArray* array = (ObjArray*)allocateObject(sizeof(ObjArray), OBJ_ARRAY);
    array->length = 0;
    array->capacity = capacity > 0 ? capacity : 8;
    array->elements = (Value*)reallocate(NULL, 0, sizeof(Value) * array->capacity);
    return array;
}

ObjError* allocateError(ErrorType type, const char* message, SrcLocation location) {
    ObjError* error = (ObjError*)allocateObject(sizeof(ObjError), OBJ_ERROR);
    error->type = type;
    error->message = allocateString(message, (int)strlen(message));
    error->location.file = location.file;
    error->location.line = location.line;
    error->location.column = location.column;
    return error;
}

ObjRangeIterator* allocateRangeIterator(int64_t start, int64_t end) {
    ObjRangeIterator* it = (ObjRangeIterator*)allocateObject(sizeof(ObjRangeIterator), OBJ_RANGE_ITERATOR);
    it->current = start;
    it->end = end;
    return it;
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
    }
}

void markValue(Value value) {
    switch (value.type) {
        case VAL_STRING:
        case VAL_ARRAY:
        case VAL_ERROR:
        case VAL_RANGE_ITERATOR:
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
    for (int i = 0; i < vm.variableCount; i++) {
        markValue(vm.globals[i]);
    }
    markValue(vm.lastError);
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
            vm.bytesAllocated -= sizeof(ObjString) + s->length + 1;
            free(s->chars);
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

// Chunk operations moved from vm.c
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->columns = NULL;
    chunk->constants.count = 0;
    chunk->constants.capacity = 0;
    chunk->constants.values = NULL;
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    FREE_ARRAY(int, chunk->columns, chunk->capacity);
    FREE_ARRAY(Value, chunk->constants.values, chunk->constants.capacity);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line, int column) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code =
            GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines =
            GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
        chunk->columns =
            GROW_ARRAY(int, chunk->columns, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->columns[chunk->count] = column;
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

