//  Orus Language Project

#ifndef clox_value_h
#define clox_value_h

#include "common.h"
#include <stdio.h>
#include <string.h>

// Forward declarations used for object references
typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjArray ObjArray;
typedef struct ObjIntArray ObjIntArray;
typedef struct ObjError ObjError;
typedef struct ObjRangeIterator ObjRangeIterator;
typedef struct ObjArrayIterator ObjArrayIterator;
typedef struct ObjEnumInstance ObjEnumInstance;
typedef struct ObjFile ObjFile;
typedef struct Value Value;

// Base object type for the garbage collector
typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_BYTEBUFFER,
    OBJ_INT_ARRAY,
    OBJ_AST,
    OBJ_TYPE,
    OBJ_ERROR,
    OBJ_RANGE_ITERATOR,
    OBJ_ARRAY_ITERATOR,
    OBJ_ENUM_INSTANCE,
    OBJ_FILE,
} ObjType;

struct Obj {
    ObjType type;
    bool marked;
    Obj* next;
};

typedef enum {
    VAL_I32,
    VAL_I64,
    VAL_U32,
    VAL_U64,
    VAL_F64,
    VAL_BOOL,
    VAL_STRING,
    VAL_BYTES,
    VAL_ARRAY,
    VAL_ENUM,
    VAL_ERROR,
    VAL_RANGE_ITERATOR,
    VAL_ARRAY_ITERATOR,
    VAL_FILE,
} ValueType;

typedef struct ObjString {
    Obj obj;
    int length;
    char* chars;
} ObjString;

typedef struct ObjArray {
    Obj obj;
    int length;
    int capacity;
    Value* elements;
} ObjArray;

typedef struct ObjByteBuffer {
    Obj obj;
    size_t length;
    size_t capacity;
    uint8_t* data;
} ObjByteBuffer;

typedef struct ObjIntArray {
    Obj obj;
    int length;
    int64_t* elements;
} ObjIntArray;

typedef struct ObjRangeIterator {
    Obj obj;
    int64_t current;
    int64_t end;
    int64_t step;
} ObjRangeIterator;

typedef struct ObjArrayIterator {
    Obj obj;
    ObjArray* array;
    int index;
} ObjArrayIterator;

typedef struct ObjEnumInstance {
    Obj obj;
    ObjString* typeName;
    ObjString* variantName;
    int variantIndex;
    ObjArray* payload;
} ObjEnumInstance;

typedef struct ObjFile {
    Obj obj;
    FILE* handle;
    ObjString* path;
    bool ownsHandle;
    bool isClosed;
} ObjFile;

typedef ObjString String;
typedef ObjArray Array;

typedef struct Value {
    ValueType type;
    union {
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        double f64;
        bool boolean;
        Obj* obj;
        ObjString* string;
        ObjByteBuffer* bytes;
        ObjArray* array;
        ObjError* error;
        ObjRangeIterator* rangeIter;
        ObjArrayIterator* arrayIter;
        ObjFile* file;
    } as;
} Value;

// Value creation macros
#define I32_VAL(value)   ((Value){VAL_I32, {.i32 = value}})
#define I64_VAL(value)   ((Value){VAL_I64, {.i64 = value}})
#define U32_VAL(value)   ((Value){VAL_U32, {.u32 = value}})
#define U64_VAL(value)   ((Value){VAL_U64, {.u64 = value}})
#define F64_VAL(value)   ((Value){VAL_F64, {.f64 = value}})
#define BOOL_VAL(value)  ((Value){VAL_BOOL, {.boolean = value}})
#define STRING_VAL(value) ((Value){VAL_STRING, {.string = value}})
#define BYTES_VAL(obj)   ((Value){VAL_BYTES, {.bytes = obj}})
#define ARRAY_VAL(obj)   ((Value){VAL_ARRAY, {.array = obj}})
#define ENUM_VAL(enumObj)    ((Value){VAL_ENUM, {.obj = (Obj*)enumObj}})
#define ERROR_VAL(obj)   ((Value){VAL_ERROR, {.error = obj}})
#define RANGE_ITERATOR_VAL(obj) ((Value){VAL_RANGE_ITERATOR, {.rangeIter = obj}})
#ifndef ARRAY_ITERATOR_VAL
#define ARRAY_ITERATOR_VAL(iteratorObj) ((Value){VAL_ARRAY_ITERATOR, {.obj = (Obj*)iteratorObj}})
#endif
#define FILE_VAL(fileObj) ((Value){VAL_FILE, {.obj = (Obj*)fileObj}})

// Value checking macros
#define IS_I32(value)    ((value).type == VAL_I32)
#define IS_I64(value)    ((value).type == VAL_I64)
#define IS_U32(value)    ((value).type == VAL_U32)
#define IS_U64(value)    ((value).type == VAL_U64)
#define IS_F64(value)    ((value).type == VAL_F64)
#define IS_BOOL(value)   ((value).type == VAL_BOOL)
#define IS_STRING(value) ((value).type == VAL_STRING)
#define IS_BYTES(value)  ((value).type == VAL_BYTES)
#define IS_ARRAY(value)  ((value).type == VAL_ARRAY)
#define IS_ENUM(value)   ((value).type == VAL_ENUM)
#define IS_ERROR(value)  ((value).type == VAL_ERROR)
#define IS_RANGE_ITERATOR(value) ((value).type == VAL_RANGE_ITERATOR)
#ifndef IS_ARRAY_ITERATOR
#define IS_ARRAY_ITERATOR(value) ((value).type == VAL_ARRAY_ITERATOR)
#endif
#define IS_FILE(value)   ((value).type == VAL_FILE)

// Value extraction macros
#define AS_I32(value)    ((value).as.i32)
#define AS_I64(value)    ((value).as.i64)
#define AS_U32(value)    ((value).as.u32)
#define AS_U64(value)    ((value).as.u64)
#define AS_F64(value)    ((value).as.f64)
#define AS_BOOL(value)   ((value).as.boolean)
#define AS_STRING(value) ((value).as.string)
#define AS_BYTES(value)  ((value).as.bytes)
#define AS_ARRAY(value)  ((value).as.array)
#define AS_ENUM(value)   ((ObjEnumInstance*)(value).as.obj)
#define AS_ERROR(value)  ((value).as.error)
#define AS_RANGE_ITERATOR(value) ((value).as.rangeIter)
#ifndef AS_ARRAY_ITERATOR
#define AS_ARRAY_ITERATOR(value) ((ObjArrayIterator*)(value).as.obj)
#endif
#define AS_FILE(value)   ((ObjFile*)(value).as.obj)

// Generic dynamic array implementation used for storing Values.
// #include "generic_array.h"

// DEFINE_ARRAY_TYPE(Value, Value)
// void printValue(Value value);
// bool valuesEqual(Value a, Value b);

#endif
