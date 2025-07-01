// register_vm.h - Register-based VM header
#ifndef REGISTER_VM_H
#define REGISTER_VM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Register-based VM configuration
#define REGISTER_COUNT 256
#define FRAMES_MAX 64
#define STACK_INIT_CAPACITY 256
#define TRY_MAX 16
#define MAX_NATIVES 256
#define UINT8_COUNT 256

// Value types
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_I32,
    VAL_I64,
    VAL_U32,
    VAL_U64,
    VAL_F64,
    VAL_STRING,
    VAL_ARRAY,
    VAL_ERROR,
    VAL_RANGE_ITERATOR
} ValueType;

// Forward declarations
typedef struct ObjString ObjString;
typedef struct ObjArray ObjArray;
typedef struct ObjError ObjError;
typedef struct ObjRangeIterator ObjRangeIterator;
typedef struct Obj Obj;

// Value representation
typedef struct {
    ValueType type;
    union {
        bool boolean;
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        double f64;
        Obj* obj;
    } as;
} Value;

// Object types
typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_ERROR,
    OBJ_RANGE_ITERATOR
} ObjType;

#define OBJ_TYPE_COUNT 4

// Object header
struct Obj {
    ObjType type;
    struct Obj* next;
    bool isMarked;
};

// String object
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

// Array object
struct ObjArray {
    Obj obj;
    int length;
    int capacity;
    Value* elements;
};

// Error object
typedef enum {
    ERROR_RUNTIME,
    ERROR_TYPE,
    ERROR_NAME,
    ERROR_INDEX,
    ERROR_KEY,
    ERROR_VALUE,
    ERROR_ARGUMENT,
    ERROR_IMPORT,
    ERROR_ATTRIBUTE,
    ERROR_UNIMPLEMENTED,
    ERROR_SYNTAX,
    ERROR_INDENT,
    ERROR_TAB,
    ERROR_RECURSION,
    ERROR_IO,
    ERROR_OS,
    ERROR_EOF
} ErrorType;

struct ObjError {
    Obj obj;
    ErrorType type;
    ObjString* message;
    struct {
        const char* file;
        int line;
        int column;
    } location;
};

// Range iterator
struct ObjRangeIterator {
    Obj obj;
    int64_t current;
    int64_t end;
};

// Source location
typedef struct {
    const char* file;
    int line;
    int column;
} SrcLocation;

// Type system
typedef enum {
    TYPE_UNKNOWN,
    TYPE_I32,
    TYPE_I64,
    TYPE_U32,
    TYPE_U64,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_VOID,
    TYPE_NIL,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_ERROR,
    TYPE_ANY
} TypeKind;

typedef struct Type Type;
struct Type {
    TypeKind kind;
    union {
        struct {
            Type* elementType;
        } array;
        struct {
            int arity;
            Type** paramTypes;
            Type* returnType;
        } function;
    } info;
};

// Chunk (bytecode container)
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    int* columns;
    struct {
        int count;
        int capacity;
        Value* values;
    } constants;
} Chunk;

// Function
typedef struct {
    int start;
    int arity;
    Chunk* chunk;
} Function;

// Native function
typedef Value (*NativeFn)(int argCount, Value* args);
typedef struct {
    ObjString* name;
    NativeFn function;
    int arity;
    Type* returnType;
} NativeFunction;

// Call frame for register-based VM
typedef struct {
    uint8_t* returnAddress;
    Chunk* previousChunk;
    uint8_t baseRegister;   // Base register for this frame
    uint8_t registerCount;  // Number of registers used by this function
    uint8_t functionIndex;
} CallFrame;

// Try frame
typedef struct {
    uint8_t* handler;
    uint8_t varIndex;
    int stackDepth;
} TryFrame;

// Module system
typedef struct {
    char* name;
    Value value;
    int index;
} Export;

typedef struct {
    char* name;
    char* module_name;
    Chunk* bytecode;
    Export exports[UINT8_COUNT];
    int export_count;
    bool executed;
    char* disk_path;
    long mtime;
    bool from_embedded;
} Module;

// AST node definitions are maintained separately
#include "ast.h"

// Variable info
typedef struct {
    ObjString* name;
    int length;
} VariableInfo;

// Register-based opcodes
typedef enum {
    // Constants and literals
    OP_LOAD_CONST,  // reg, const_index - Load constant into register
    OP_LOAD_NIL,    // reg - Load nil into register
    OP_LOAD_TRUE,   // reg - Load true into register
    OP_LOAD_FALSE,  // reg - Load false into register

    // Register operations
    OP_MOVE,          // dst, src - Move value between registers
    OP_LOAD_GLOBAL,   // reg, global_index - Load global into register
    OP_STORE_GLOBAL,  // global_index, reg - Store register to global

    // Arithmetic - register-based (dst, src1, src2)
    OP_ADD_I32_R,
    OP_SUB_I32_R,
    OP_MUL_I32_R,
    OP_DIV_I32_R,
    OP_MOD_I32_R,
    OP_INC_I32_R,
    OP_DEC_I32_R,

    OP_ADD_I64_R,
    OP_SUB_I64_R,
    OP_MUL_I64_R,
    OP_DIV_I64_R,
    OP_MOD_I64_R,

    OP_ADD_U32_R,
    OP_SUB_U32_R,
    OP_MUL_U32_R,
    OP_DIV_U32_R,
    OP_MOD_U32_R,

    OP_ADD_U64_R,
    OP_SUB_U64_R,
    OP_MUL_U64_R,
    OP_DIV_U64_R,
    OP_MOD_U64_R,

    OP_ADD_F64_R,
    OP_SUB_F64_R,
    OP_MUL_F64_R,
    OP_DIV_F64_R,

    // Bitwise operations
    OP_AND_I32_R,
    OP_OR_I32_R,
    OP_XOR_I32_R,
    OP_NOT_I32_R,
    OP_SHL_I32_R,
    OP_SHR_I32_R,

    // Comparison (dst, src1, src2)
    OP_EQ_R,
    OP_NE_R,
    OP_LT_I32_R,
    OP_LE_I32_R,
    OP_GT_I32_R,
    OP_GE_I32_R,

    OP_LT_I64_R,
    OP_LE_I64_R,
    OP_GT_I64_R,
    OP_GE_I64_R,

    OP_LT_F64_R,
    OP_LE_F64_R,
    OP_GT_F64_R,
    OP_GE_F64_R,

    OP_LT_U32_R,
    OP_LE_U32_R,
    OP_GT_U32_R,
    OP_GE_U32_R,

    OP_LT_U64_R,
    OP_LE_U64_R,
    OP_GT_U64_R,
    OP_GE_U64_R,

    // Logical operations
    OP_AND_BOOL_R,
    OP_OR_BOOL_R,
    OP_NOT_BOOL_R,

    // Type conversions (dst, src)
    OP_I32_TO_F64_R,
    OP_I32_TO_I64_R,
    OP_I64_TO_I32_R,
    OP_I64_TO_F64_R,
    OP_F64_TO_I32_R,
    OP_F64_TO_I64_R,
    OP_BOOL_TO_I32_R,
    OP_I32_TO_BOOL_R,
    OP_I32_TO_U32_R,
    OP_U32_TO_I32_R,
    OP_F64_TO_U32_R,
    OP_U32_TO_F64_R,
    OP_I32_TO_U64_R,
    OP_I64_TO_U64_R,
    OP_U64_TO_I32_R,
    OP_U64_TO_I64_R,
    OP_U32_TO_U64_R,
    OP_U64_TO_U32_R,
    OP_F64_TO_U64_R,
    OP_U64_TO_F64_R,

    // String operations
    OP_CONCAT_R,
    OP_TO_STRING_R,

    // Array operations
    OP_MAKE_ARRAY_R,  // dst, start_reg, count
    OP_ARRAY_GET_R,   // dst, array_reg, index_reg
    OP_ARRAY_SET_R,   // array_reg, index_reg, value_reg
    OP_ARRAY_LEN_R,   // dst, array_reg

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_R,      // condition_reg, offset
    OP_JUMP_IF_NOT_R,  // condition_reg, offset
    OP_LOOP,
    OP_GET_ITER_R,     // dst_iter, iterable_reg
    OP_ITER_NEXT_R,    // dst_value, iter_reg, has_value_reg

    // Function calls
    OP_CALL_R,         // func_reg, first_arg_reg, arg_count, result_reg
    OP_CALL_NATIVE_R,  // native_index, first_arg_reg, arg_count, result_reg
    OP_RETURN_R,       // value_reg (or no operand for void)
    OP_RETURN_VOID,

    // I/O
    OP_PRINT_MULTI_R,  // first_reg, count, newline_flag
    OP_PRINT_R,        // reg
    OP_PRINT_NO_NL_R,  // reg

    // Short jump optimizations (1-byte offset instead of 2)
    OP_JUMP_SHORT,         // 1-byte forward jump (0-255)
    OP_JUMP_BACK_SHORT,    // 1-byte backward jump (0-255)
    OP_JUMP_IF_NOT_SHORT,  // Conditional short jump
    OP_LOOP_SHORT,         // Short loop (backward jump)

    // Typed register operations for performance (bypass Value boxing)
    OP_ADD_I32_TYPED,      // dst_reg, left_reg, right_reg
    OP_SUB_I32_TYPED,
    OP_MUL_I32_TYPED,
    OP_DIV_I32_TYPED,
    OP_MOD_I32_TYPED,
    
    OP_ADD_I64_TYPED,
    OP_SUB_I64_TYPED,
    OP_MUL_I64_TYPED,
    OP_DIV_I64_TYPED,
    
    OP_ADD_F64_TYPED,
    OP_SUB_F64_TYPED,
    OP_MUL_F64_TYPED,
    OP_DIV_F64_TYPED,
    
    // Typed comparisons
    OP_LT_I32_TYPED,
    OP_LE_I32_TYPED,
    OP_GT_I32_TYPED,
    OP_GE_I32_TYPED,
    
    // Typed loads
    OP_LOAD_I32_CONST,     // reg, value
    OP_LOAD_I64_CONST,     // reg, value
    OP_LOAD_F64_CONST,     // reg, value
    
    // Typed moves
    OP_MOVE_I32,           // dst_reg, src_reg
    OP_MOVE_I64,           // dst_reg, src_reg
    OP_MOVE_F64,           // dst_reg, src_reg

    // Other
    OP_IMPORT_R,
    OP_GC_PAUSE,
    OP_GC_RESUME,
    OP_HALT
} OpCode;

// Loop context for break/continue statements
#include "intvec.h"
#include "jumptable.h"

typedef struct {
    JumpTable breakJumps;     // Patches for break statements
    JumpTable continueJumps;  // Jump targets for continue
    int continueTarget;    // Target for continue (loop start)
    int scopeDepth;        // Scope depth when loop was entered
    const char* label;     // Optional loop label
} LoopContext;

// Compiler state for register allocation
typedef struct {
    Chunk* chunk;
    const char* fileName;
    const char* source;
    uint8_t nextRegister;  // Next available register
    uint8_t maxRegisters;  // Maximum registers used
    struct {
        char* name;
        uint8_t reg;  // Register allocation for local
        bool isActive;
        int depth;
        bool isMutable;
        ValueType type; // Type of the variable
    } locals[REGISTER_COUNT];
    int localCount;
    int scopeDepth;
    LoopContext loopStack[16];  // Stack of nested loop contexts
    int loopDepth;              // Current loop nesting depth
    JumpTable pendingJumps;     // Track all pending forward jumps for cascade updates
    bool hadError;
} Compiler;

// Typed registers for optimization (unboxed values)
typedef struct {
    // Numeric registers (unboxed for performance)
    int32_t i32_regs[32];
    int64_t i64_regs[32];
    uint32_t u32_regs[32];
    uint64_t u64_regs[32];
    double f64_regs[32];
    bool bool_regs[32];

    // Heap object registers (boxed)
    Value heap_regs[32];

    // Register type tracking (for debugging/safety)
    uint8_t reg_types[256];  // Track which register bank each logical register maps to
} TypedRegisters;

// Register type enum
typedef enum {
    REG_TYPE_NONE = 0,
    REG_TYPE_I32,
    REG_TYPE_I64,
    REG_TYPE_U32,
    REG_TYPE_U64,
    REG_TYPE_F64,
    REG_TYPE_BOOL,
    REG_TYPE_HEAP
} RegisterType;

// VM state
typedef struct {
    // Registers (traditional boxed)
    Value registers[REGISTER_COUNT];
    
    // Typed registers for performance optimization
    TypedRegisters typed_regs;

    // Call frames
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    // Bytecode execution
    Chunk* chunk;
    uint8_t* ip;

    // Globals
    Value globals[UINT8_COUNT];
    Type* globalTypes[UINT8_COUNT];
    bool publicGlobals[UINT8_COUNT];
    VariableInfo variableNames[UINT8_COUNT];
    int variableCount;

    // Functions
    Function functions[UINT8_COUNT];
    Type* functionDecls[UINT8_COUNT];
    int functionCount;

    // Native functions
    NativeFunction nativeFunctions[MAX_NATIVES];
    int nativeFunctionCount;

    // Error handling
    TryFrame tryFrames[TRY_MAX];
    int tryFrameCount;
    Value lastError;

    // Module system
    ObjString* loadedModules[UINT8_COUNT];
    int moduleCount;

    // Memory management
    Obj* objects;
    size_t bytesAllocated;
    size_t gcCount;
    bool gcPaused;

    // Execution state
    uint64_t instruction_count;
    ASTNode* astRoot;
    const char* filePath;
    int currentLine;
    int currentColumn;

    double lastExecutionTime;

    // Configuration
    bool trace;
    const char* stdPath;
    const char* cachePath;
    bool devMode;
    bool suppressWarnings;
    bool promotionHints;
} VM;

// Global VM instance
extern VM vm;

// Interpretation results
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// Value macros
#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.i32 = 0}})
#define I32_VAL(value) ((Value){VAL_I32, {.i32 = value}})
#define I64_VAL(value) ((Value){VAL_I64, {.i64 = value}})
#define U32_VAL(value) ((Value){VAL_U32, {.u32 = value}})
#define U64_VAL(value) ((Value){VAL_U64, {.u64 = value}})
#define F64_VAL(value) ((Value){VAL_F64, {.f64 = value}})
#define STRING_VAL(value) ((Value){VAL_STRING, {.obj = (Obj*)value}})
#define ARRAY_VAL(obj) ((Value){VAL_ARRAY, {.obj = (Obj*)obj}})
#define ERROR_VAL(object) ((Value){VAL_ERROR, {.obj = (Obj*)object}})

#define AS_BOOL(value) ((value).as.boolean)
#define AS_I32(value) ((value).as.i32)
#define AS_I64(value) ((value).as.i64)
#define AS_U32(value) ((value).as.u32)
#define AS_U64(value) ((value).as.u64)
#define AS_F64(value) ((value).as.f64)
#define AS_OBJ(value) ((value).as.obj)
#define AS_STRING(value) ((ObjString*)(value).as.obj)
#define AS_ARRAY(value) ((ObjArray*)(value).as.obj)
#define AS_ERROR(value) ((ObjError*)(value).as.obj)
#define AS_RANGE_ITERATOR(value) ((ObjRangeIterator*)(value).as.obj)

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_I32(value) ((value).type == VAL_I32)
#define IS_I64(value) ((value).type == VAL_I64)
#define IS_U32(value) ((value).type == VAL_U32)
#define IS_U64(value) ((value).type == VAL_U64)
#define IS_F64(value) ((value).type == VAL_F64)
#define IS_STRING(value) ((value).type == VAL_STRING)
#define IS_ARRAY(value) ((value).type == VAL_ARRAY)
#define IS_ERROR(value) ((value).type == VAL_ERROR)
#define IS_RANGE_ITERATOR(value) ((value).type == VAL_RANGE_ITERATOR)

// Function declarations
void initVM(void);
void freeVM(void);
InterpretResult interpret(const char* source);
InterpretResult interpret_module(const char* path);

// Chunk operations
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line, int column);
int addConstant(Chunk* chunk, Value value);

// Value operations
void printValue(Value value);
bool valuesEqual(Value a, Value b);

// Object allocation
ObjString* allocateString(const char* chars, int length);
ObjArray* allocateArray(int capacity);
ObjError* allocateError(ErrorType type, const char* message,
                        SrcLocation location);

// Memory management
void collectGarbage(void);
void freeObjects(void);

// Type system
void initTypeSystem(void);
Type* getPrimitiveType(TypeKind kind);

// Debug functions
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif  // REGISTER_VM_H